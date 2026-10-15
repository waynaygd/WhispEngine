// Single application in linear HDR before output tone mapping/FXAA.
Texture2D<float4> scene:register(t0);
Texture2D<float> depth:register(t1);
Texture2D<float> shadowMap:register(t2);
struct Light {float4 positionRange,directionType,colorIntensity,coneAttenuation,screenBounds;};
StructuredBuffer<Light> lights:register(t3);
cbuffer Fog:register(b0){
    float4 camera,right,up,forward,projection,fogColor,distanceFog,heightFog,scattering,volume;
    float4 sunDirection,sunColor,ambient;
    column_major float4x4 lightVP;
    float4 shadow;
};
struct Out{float4 pos:SV_Position;float2 uv:TEXCOORD;};
Out VS(uint id:SV_VertexID){Out o;o.uv=float2((id<<1)&2,id&2);o.pos=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}
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
float Phase(float cosine){float g=scattering.w;return (1-g*g)/(12.5663706144*pow(max(1e-5,1+g*g-2*g*cosine),1.5));}
float Visibility(float3 position){
    if(shadow.x<.5)return 1;
    float4 clip=mul(lightVP,float4(position,1));float3 p=clip.xyz/clip.w;float2 uv=p.xy*float2(.5,-.5)+.5;
    if(any(uv<0)||any(uv>=1)||p.z<0||p.z>1)return 1;
    uint width,height;shadowMap.GetDimensions(width,height);
    float visibility=p.z-shadow.y<=shadowMap.Load(int3(int2(uv*float2(width,height)),0))?1:0;
    float edge=min(min(uv.x,1-uv.x),min(uv.y,1-uv.y));float fade=shadow.w>0?smoothstep(0,shadow.w,edge):1;
    return lerp(1,visibility,shadow.z*fade);
}
float3 Incident(float3 position,float3 ray){
    if(volume.y<.5)return fogColor.rgb; // Explicit constant environment radiance option.
    float3 light=ambient.rgb*ambient.w*sunDirection.w;
    if(sunDirection.w>.5)light+=sunColor.rgb*sunColor.w*Phase(dot(-sunDirection.xyz,ray))*Visibility(position);
    [loop]for(uint j=0;j<(uint)volume.w;++j){
        Light l=lights[j];float3 toward;float attenuation=1;
        if(l.directionType.w<.5)toward=-l.directionType.xyz;
        else{
            float3 offset=l.positionRange.xyz-position;float distance=length(offset);
            if(distance>=l.positionRange.w)continue;
            toward=offset/max(distance,1e-5);float window=saturate(1-pow(distance/l.positionRange.w,4));
            attenuation=window*window/max(distance*distance,l.coneAttenuation.z*l.coneAttenuation.z);
            if(l.directionType.w>1.5){float cosine=dot(-toward,l.directionType.xyz);float cone=saturate((cosine-l.coneAttenuation.y)/max(l.coneAttenuation.x-l.coneAttenuation.y,1e-5));attenuation*=cone*cone*(3-2*cone);}
        }
        light+=l.colorIntensity.rgb*l.colorIntensity.w*attenuation*Phase(dot(toward,ray));
    }return light;
}
// Deterministic per-pixel stratification breaks coherent shadow slice bands.
// No frame-dependent jitter/history: stationary cameras reproduce identical output.
float Stratum(uint2 pixel,uint step){uint n=pixel.x*1973u+pixel.y*9277u+step*26699u+89173u;n=(n^(n>>13))*1274126177u;return float((n>>8)&0x00ffffffu)/16777216.0;}
float3 IntegrateVolume(float3 ray,float length,float3 sceneColor,uint2 pixel){
    uint samples=(uint)volume.x;float ds=length/samples,transmittance=1;float3 radiance=0;
    [loop]for(uint step=0;step<samples;++step){
        float3 position=camera.xyz+ray*((step+Stratum(pixel,step))*ds);
        float extinction=volume.z*distanceFog.z*exp(-heightFog.y*max(position.y-heightFog.x,0));
        float transmission=exp(-extinction*ds);
        radiance+=transmittance*(1-transmission)*scattering.rgb*Incident(position,ray);
        transmittance*=transmission;if(transmittance<1e-4)break;
    }
    return sceneColor*transmittance+radiance;
}
float4 PS(Out i):SV_Target{
    int2 pixel=int2(i.pos.xy);float4 color=scene.Load(int3(pixel,0));float d=depth.Load(int3(pixel,0));
    float3 ray=normalize(forward.xyz+right.xyz*((i.uv.x*2-1)/projection.x)+up.xyz*((1-i.uv.y*2)/projection.y));
    float length=d>=1?heightFog.w:(projection.w/(d-projection.z))/max(dot(ray,forward.xyz),1e-5);
    if(distanceFog.w>1.5)return float4(IntegrateVolume(ray,min(length,heightFog.w),color.rgb,uint2(pixel)),color.a);
    float amount=distanceFog.z*saturate((length-distanceFog.x)/(distanceFog.y-distanceFog.x));
    if(distanceFog.w>.5){
        length=min(length,heightFog.w);
        float opticalDepth=distanceFog.z*heightFog.z*HeightIntegral(camera.y,camera.y+ray.y*length,length);
        amount=1-exp(-opticalDepth);
    }
    return float4(lerp(color.rgb,fogColor.rgb,amount),color.a);
}
