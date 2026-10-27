// Shared linear fog model for opaque fullscreen fog and transparent particle fragments.
#ifndef WHISP_FOG_COMMON
#define WHISP_FOG_COMMON
#ifndef WHISP_FOG_REGISTER
#define WHISP_FOG_REGISTER b0
#endif
Texture2D<float> shadowMap:register(t2);
struct Light {float4 positionRange,directionType,colorIntensity,coneAttenuation,screenBounds;};
StructuredBuffer<Light> lights:register(t3);
cbuffer Fog:register(WHISP_FOG_REGISTER){
    float4 camera,right,up,forward,projection,fogColor,distanceFog,heightFog,scattering,volume;
    float4 sunDirection,sunColor,ambient;
    column_major float4x4 lightVP;
    float4 shadow;
    float4 fogViewport;
};
float AboveIntegral(float a,float b,float length){
    if(abs(b-a)<1e-4)return length*exp(-(a+b)*.5);
    return length*(exp(-a)-exp(-b))/(b-a);
}
float HeightIntegral(float y0,float y1,float length){
    float a=y0-heightFog.x,b=y1-heightFog.x,k=heightFog.y;
    if(k<1e-8||max(a,b)<=0)return length;
    if((a<0&&b>0)||(b<0&&a>0)){
        float fraction=max(a,b)/(abs(a)+abs(b));
        return length*(1-fraction)+AboveIntegral(0,max(a,b)*k,length*fraction);
    }
    return AboveIntegral(max(0,a)*k,max(0,b)*k,length);
}
float Phase(float cosine){float g=scattering.w,r=rsqrt(max(1e-5,1+g*g-2*g*cosine));return (1-g*g)*r*r*r*.07957747155;}
float Visibility(float3 position){
    if(shadow.x<.5)return 1;
    float4 clip=mul(lightVP,float4(position,1));float3 p=clip.xyz/clip.w;float2 uv=p.xy*float2(.5,-.5)+.5;
    if(any(uv<0)||any(uv>=1)||p.z<0||p.z>1)return 1;
    uint width,height;shadowMap.GetDimensions(width,height);
    float visibility=p.z-shadow.y<=shadowMap.Load(int3(int2(uv*float2(width,height)),0))?1:0;
    float edge=min(min(uv.x,1-uv.x),min(uv.y,1-uv.y));float fade=shadow.w>0?smoothstep(0,shadow.w,edge):1;
    return lerp(1,visibility,shadow.z*fade);
}
// Stratification is needed only for discontinuous primary shadow visibility.
float Stratum(uint2 pixel,uint step){uint n=pixel.x*1973u+pixel.y*9277u+step*26699u+89173u;n=(n^(n>>13))*1274126177u;return float((n>>8)&0x00ffffffu)/16777216.0;}
float Tau(float3 origin,float3 ray,float s){return volume.z*distanceFog.z*HeightIntegral(origin.y,origin.y+ray.y*s,s);}
float Medium(float3 p){return volume.z*distanceFog.z*exp(-heightFog.y*max(p.y-heightFog.x,0));}
float3 Origin(int2 pixel){uint w=(uint)fogViewport.x,h=(uint)fogViewport.y;float2 uv=(float2(pixel)+.5)/float2(w,h);return camera.xyz+forward.w*(right.xyz*((uv.x*2-1)/projection.x)+up.xyz*((1-uv.y*2)/projection.y));}
float3 Ray(int2 pixel){uint w=(uint)fogViewport.x,h=(uint)fogViewport.y;float2 uv=(float2(pixel)+.5)/float2(w,h);return forward.w>.5?forward.xyz:normalize(forward.xyz+right.xyz*((uv.x*2-1)/projection.x)+up.xyz*((1-uv.y*2)/projection.y));}
float ViewDepth(float d){return d>=1?-1:(forward.w>.5?(d-projection.w)/projection.z:projection.w/(d-projection.z));}
float RayLength(float z,float3 ray){return z<0?heightFog.w:min(heightFog.w,z/max(dot(ray,forward.xyz),1e-5));}
float3 LocalScattering(Light l,float3 origin,float3 ray,float length){
    // Conservative sphere/ray interval: no per-step work for a light whose
    // influence volume cannot intersect this visible ray segment.
    float3 offset=l.positionRange.xyz-origin;
    float center=dot(offset,ray);float3 perpendicular=offset-ray*center;
    float impact2=dot(perpendicular,perpendicular),range2=l.positionRange.w*l.positionRange.w;
    if(impact2>=range2)return 0;
    float extent=sqrt(range2-impact2),a=max(0,center-extent),b=min(length,center+extent);
    if(b<=a)return 0;
    // Importance quadrature in angular coordinates resolves the inverse-square
    // peak instead of randomly hitting it with long uniform distance steps.
    // The ratio below preserves the existing max(r^2, nearRadius^2) model.
    float near2=l.coneAttenuation.z*l.coneAttenuation.z,h=sqrt(max(impact2,near2));
    uint samples=max(8u,(uint)volume.x/2u);
    float theta0=atan((a-center)/h),theta1=atan((b-center)/h),dt=(theta1-theta0)/samples;
    float3 result=0;
    [loop]for(uint j=0;j<samples;++j){
        float s=clamp(center+h*tan(theta0+(j+.5)*dt),a,b);
        float3 position=origin+ray*s,toward=l.positionRange.xyz-position;
        float r2=dot(toward,toward),invR=rsqrt(max(r2,1e-10));
        float normalized=r2/range2,window=saturate(1-normalized*normalized),cone=1;
        if(l.directionType.w>1.5){float cosine=dot(-toward*invR,l.directionType.xyz);cone=saturate((cosine-l.coneAttenuation.y)/max(l.coneAttenuation.x-l.coneAttenuation.y,1e-5));cone=cone*cone*(3-2*cone);}
        if(cone<=0)continue;
        float transmission=exp(-Tau(origin,ray,s));if(transmission<1e-4)break;
        float ratio=(h*h+(s-center)*(s-center))/max(r2,near2);
        result+=Medium(position)*transmission*window*window*cone*Phase(dot(toward,ray)*invR)*ratio;
    }
    return result*(dt/h)*l.colorIntensity.rgb*l.colorIntensity.w;
}
float3 Scattering(float3 origin,float3 ray,float length,uint2 pixel){
    if(volume.z*distanceFog.z<=0||length<=0)return 0;
    float opacity=1-exp(-Tau(origin,ray,length));
    if(volume.y<.5)return fogColor.rgb*scattering.rgb*opacity;
    float3 result=ambient.rgb*ambient.w*sunDirection.w*opacity;
    float3 primary=sunColor.rgb*sunColor.w*sunDirection.w*Phase(dot(-sunDirection.xyz,ray));
    if(shadow.x<.5)result+=primary*opacity;
    else if(any(primary>0)){
        float ds=length/volume.x,transmission=1;
        [loop]for(uint step=0;step<(uint)volume.x;++step){
            float tau=Tau(origin,ray,(step+1)*ds),next=exp(-tau);
            float3 p=origin+ray*((step+Stratum(pixel,step))*ds);
            result+=primary*(transmission-next)*Visibility(p);
            transmission=next;if(transmission<1e-4)break;
        }
    }
    [loop]for(uint j=0;j<(uint)volume.w;++j){
        Light l=lights[j];if(l.colorIntensity.w<=0)continue;
        if(l.directionType.w<.5)result+=l.colorIntensity.rgb*l.colorIntensity.w*Phase(dot(-l.directionType.xyz,ray))*opacity;
        else result+=LocalScattering(l,origin,ray,length);
    }
    return result*scattering.rgb;
}
// RGB is integrated in-scattering; alpha is transmittance to this surface.
float4 FogAtDepth(int2 pixel,float deviceDepth){
    float3 ray=Ray(pixel),origin=Origin(pixel);
    float length=deviceDepth>=1?heightFog.w:ViewDepth(deviceDepth)/max(dot(ray,forward.xyz),1e-5);
    if(distanceFog.w>1.5){
        length=min(length,heightFog.w);
        return float4(Scattering(origin,ray,length,uint2(pixel)),exp(-Tau(origin,ray,length)));
    }
    float amount=distanceFog.z*saturate((length-distanceFog.x)/(distanceFog.y-distanceFog.x));
    if(distanceFog.w>.5){
        length=min(length,heightFog.w);
        amount=1-exp(-distanceFog.z*heightFog.z*HeightIntegral(origin.y,origin.y+ray.y*length,length));
    }
    return float4(fogColor.rgb*amount,1-amount);
}
#endif
