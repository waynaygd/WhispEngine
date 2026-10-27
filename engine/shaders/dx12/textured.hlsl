// Classify at shader input, before raster interpolators can clamp INF outputs.
bool Finite(float x) { return (asuint(x)&0x7f800000u)!=0x7f800000u; }
bool Finite3(float3 x) { return Finite(x.x)&&Finite(x.y)&&Finite(x.z); }
float3 SanitizeSurface(float3 x) {
    return float3(Finite(x.x)?saturate(x.x):0,Finite(x.y)?saturate(x.y):.5,Finite(x.z)?x.z:0);
}
cbuffer CB0 : register(b0)
{
    float4x4 gMVP;
    float4 gTint;
    float4x4 gModel;
    float4 gBaseColor;
};

cbuffer LightingCB : register(b1)
{
    float4 gDirectionEnabled;
    float4 gLightColorIntensity;
    float4 gAmbientColorIntensity;
};
cbuffer NormalCB : register(b2)
{
    float4 gNormal0;
    float4 gNormal1;
    float4 gNormal2;
};
float3 TransformNormal(float3 n, float3 row0, float3 row1, float3 row2)
{
    float3 world = float3(dot(row0,n),dot(row1,n),dot(row2,n));
    if(!Finite3(world))return 0;
    float magnitude=max(max(abs(world.x),abs(world.y)),abs(world.z));
    if(!Finite(magnitude)||magnitude<=0)return 0;
    world/=magnitude;return world*rsqrt(dot(world,world));
}
cbuffer ShadowCB : register(b3) {
    float4x4 gLightVP;
    float4 gShadowParameters;
    float4 gShadowSampling;
    float4 gShadowCoverage;
};
Texture2D<float> gShadowMap : register(t1);
SamplerComparisonState gShadowSampler : register(s1);
cbuffer MaterialCB : register(b4) { float gReceiveShadows; float3 gSurface; float4 gTextureParameters; };
cbuffer CameraCB : register(b5) { float4 gCameraPosition; };
float Visibility(float3 position,float3 normal) {
    if(gReceiveShadows<.5 || gShadowParameters.x<.5)return 1; // uniform per draw
    // World-space normal offset, scaled in shadow texels and by grazing angle.
    float cosine=saturate(dot(normal,-gDirectionEnabled.xyz));
    position+=normal*(gShadowSampling.z*sqrt(saturate(1-cosine*cosine)));
    float4 clip=mul(gLightVP,float4(position,1));
    float3 ndc=clip.xyz/clip.w;
    float2 uv=float2(ndc.x*.5+.5,.5-ndc.y*.5);
    // Evaluate derivatives before per-pixel out-of-frustum branches. Correct the
    // depth at the actual POINT sample's texel center, including every PCF tap.
    float3 dx=ddx(float3(uv,ndc.z)),dy=ddy(float3(uv,ndc.z));
    float determinant=dx.x*dy.y-dx.y*dy.x;
    float2 gradient=0;
    if(abs(determinant)>1e-12 && gShadowCoverage.z>.5)
        gradient=float2(dy.y*dx.z-dx.y*dy.z,dx.x*dy.z-dy.x*dx.z)/determinant;
    if(clip.w<=0)return 1;
    if(any(uv<0)||any(uv>1)||ndc.z<0||ndc.z>1)return 1;
    // Preserve the original projection's world-space bias after caster extension.
    // A few local D32 ULPs still cover raster/transform roundoff when extension
    // compresses that offset below depth precision; avoid a fixed 5e-6 floor.
    float precisionBias=asfloat(asuint(ndc.z)+4u)-ndc.z;
    float depth=ndc.z-max(gShadowSampling.y,precisionBias);
    float visibility=0;
    if(gShadowParameters.z>.5 && gShadowCoverage.w>4 && gShadowSampling.w>0) {
        // Continuous tent weights anchored to texel centers, no randomized taps.
        // Every point comparison receives its own geometric receiver-plane correction.
        float2 texel=uv/gShadowSampling.x-.5;
        float2 origin=floor(texel+.5),fraction=texel-origin;
        float radius=clamp(gShadowSampling.w+.5,.5,2.5),weightSum=0;
        [unroll] for(int y=-2;y<=2;++y) [unroll] for(int x=-2;x<=2;++x){
            float2 delta=float2(x,y)-fraction;
            float2 weights=max(0,radius-abs(delta));float weight=weights.x*weights.y;
            float2 center=(origin+float2(x,y)+.5)*gShadowSampling.x;
            float correction=dot(gradient,center-uv);
            visibility+=weight*(any(center<0)||any(center>1)?1:gShadowMap.SampleCmpLevelZero(gShadowSampler,center,depth+correction));weightSum+=weight;
        }
        visibility/=max(weightSum,1e-6);
    } else if(gShadowParameters.z>.5) {
        [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x) {
            float2 tap=uv+float2(x,y)*gShadowSampling.x*gShadowSampling.w;
            float2 center=(floor(tap/gShadowSampling.x)+.5)*gShadowSampling.x;
            float correction=dot(gradient,center-uv);
            visibility+=(any(tap<0)||any(tap>1)?1:gShadowMap.SampleCmpLevelZero(gShadowSampler,tap,depth+correction))/9;
        }
    } else {
        float2 center=(floor(uv/gShadowSampling.x)+.5)*gShadowSampling.x;
        visibility=gShadowMap.SampleCmpLevelZero(gShadowSampler,uv,depth+dot(gradient,center-uv));
    }
    float border=min(min(uv.x,1-uv.x),min(uv.y,1-uv.y));
    float fade=gShadowCoverage.x>0?smoothstep(0,gShadowCoverage.x,border):1;
    fade*=gShadowCoverage.y>0?smoothstep(0,gShadowCoverage.y,min(ndc.z,1-ndc.z)):1;
    return lerp(1,visibility,gShadowParameters.y*fade);
}
Texture2D gTexture : register(t0);
Texture2D gNormalTexture : register(t2);
Texture2D gMetallicRoughnessTexture : register(t3);
Texture2D gOcclusionTexture : register(t4);
SamplerState gSampler : register(s0);

struct VSIn
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float4 tangent : TANGENT;
};

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
    nointerpolation float4 tint : COLOR0;
    float3 worldNormal : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
    nointerpolation float3 surface : TEXCOORD3;
    nointerpolation float4 baseColor : COLOR1;
    float4 worldTangent : TEXCOORD4;
    nointerpolation float4 textureParameters : TEXCOORD5;
};

float3 SafeUnit(float3 v) {
    if(!Finite3(v))return 0;
    float magnitude=max(max(abs(v.x),abs(v.y)),abs(v.z));
    if(!Finite(magnitude)||magnitude<=0)return 0;
    v/=magnitude;return v*rsqrt(dot(v,v));
}
float4 SafeTextureParameters(float4 p) {
    return float4(Finite(p.x)?clamp(p.x,0,16):1,Finite(p.y)?saturate(p.y):1,Finite(p.z)?clamp(p.z,0,7):0,p.w);
}
float4 TransformTangent(float4 tangent,float4x4 model,float3 normal) {
    if(!Finite3(tangent.xyz)||!Finite(tangent.w)||abs(tangent.w)<.5)return 0;
    float3 T=mul((float3x3)model,tangent.xyz);
    T=SafeUnit(T);T=SafeUnit(T-normal*dot(normal,T));
    float3 a=SafeUnit(float3(model[0][0],model[1][0],model[2][0]));
    float3 b=SafeUnit(float3(model[0][1],model[1][1],model[2][1]));
    float3 c=SafeUnit(float3(model[0][2],model[1][2],model[2][2]));
    float determinant=dot(cross(a,b),c);
    if(dot(T,T)<.5||!Finite(determinant)||abs(determinant)<1e-12)return 0;
    return float4(T,(tangent.w<0?-1:1)*(determinant<0?-1:1));
}
VSOut VSMain(VSIn input)
{
    VSOut output;
    output.pos = mul(gMVP, float4(input.pos, 1.0));
    output.uv = input.uv;
    output.tint = gTint;
    output.surface=SanitizeSurface(gSurface);output.baseColor=gBaseColor;
    output.worldPosition=mul(gModel,float4(input.pos,1)).xyz;
    output.worldNormal = TransformNormal(input.normal,gNormal0.xyz,gNormal1.xyz,gNormal2.xyz);
    output.worldTangent=TransformTangent(input.tangent,gModel,output.worldNormal);
    output.textureParameters=SafeTextureParameters(gTextureParameters);
    return output;
}

// PBR_COLOR_PIPELINE_V1; PBR_TEXTURE_PIPELINE_V1. Base Color uses hardware sRGB SRV.
// Data maps use linear UNORM views of the same typeless RGBA8 storage.
float3 DecodeSRGB(float3 c) {
    return float3(c.x<=.04045?c.x/12.92:pow((c.x+.055)/1.055,2.4),
                  c.y<=.04045?c.y/12.92:pow((c.y+.055)/1.055,2.4),
                  c.z<=.04045?c.z/12.92:pow((c.z+.055)/1.055,2.4));
}
float3 EncodeSRGB(float3 c) {
    c=float3(Finite(c.x)?c.x:0,Finite(c.y)?c.y:0,Finite(c.z)?c.z:0);
    c=saturate(c); // Stage 1 is LDR: no exposure or tone mapping.
    return float3(c.x<=.0031308?c.x*12.92:1.055*pow(c.x,1/2.4)-.055,
                  c.y<=.0031308?c.y*12.92:1.055*pow(c.y,1/2.4)-.055,
                  c.z<=.0031308?c.z*12.92:1.055*pow(c.z,1/2.4)-.055);
}
float3 SampleBaseLinear(float2 uv) { return gTexture.Sample(gSampler,uv).rgb; }
// Exact separable Smith GGX with alpha = perceptual roughness squared.
// G1(x)=2x/(x+sqrt(alpha^2+(1-alpha^2)*x^2)). No Schlick k approximation.
float SmithG1(float x,float a2) {
    return 2*x/max(x+sqrt(a2+(1-a2)*x*x),1e-12);
}
float3 DirectBRDF(float3 base,float metallic,float roughness,float3 N,float3 V,float3 L) {
    float NoL=saturate(dot(N,L)),NoV=saturate(dot(N,V));
    if(NoL<=0||NoV<=0)return 0;
    float3 H=SafeUnit(V+L);if(dot(H,H)<.5)return 0;
    float NoH=saturate(dot(N,H)),VoH=saturate(dot(V,H));
    metallic=Finite(metallic)?saturate(metallic):0;
    roughness=Finite(roughness)?saturate(roughness):.5;
    float r=max(roughness,.04),alpha=r*r,a2=alpha*alpha;
    float denominator=(1-NoH*NoH)+NoH*NoH*a2;
    float D=a2/(3.141592653589793*max(denominator*denominator,1e-20));
    float G=SmithG1(NoL,a2)*SmithG1(NoV,a2);
    float3 F0=lerp(float3(.04,.04,.04),base,saturate(metallic));
    float3 F=F0+(1-F0)*pow(1-VoH,5);
    float3 diffuse=(1-F)*(1-saturate(metallic))*base/3.141592653589793;
    float3 specular=D*G*F/max(4*NoL*NoV,1e-20);
    return (diffuse+specular)*NoL;
}
// HDR_SCENE_V1: unclamped linear scene output, SDR transfer in separate pass.
cbuffer EnvironmentCB:register(b6){float4 gIbl;float4 gEnvironmentRotation;};
TextureCube<float4> gEnvironment:register(t5);
TextureCube<float4> gIrradiance:register(t6);
TextureCube<float4> gPrefiltered:register(t7);
Texture2D<float2> gBrdf:register(t8);
SamplerState gEnvironmentSampler:register(s2);
float3 RotateEnvironment(float3 d){return float3(gEnvironmentRotation.x*d.x-gEnvironmentRotation.y*d.z,d.y,gEnvironmentRotation.y*d.x+gEnvironmentRotation.x*d.z);}
float3 IndirectBRDF(float3 base,float m,float rough,float3 N,float3 V,float ao){
    float nv=saturate(dot(N,V));float3 f0=lerp(.04.xxx,base,m);
    float3 F=f0+(1-f0)*pow(1-nv,5);
    float3 diffuse=(1-F)*(1-m)*base*gIrradiance.SampleLevel(gEnvironmentSampler,RotateEnvironment(N),0).rgb;
    float3 R=reflect(-V,N);float r=clamp(rough,.04,1);
    float2 lut=gBrdf.SampleLevel(gEnvironmentSampler,float2(nv,r),0);
    float3 spec=gPrefiltered.SampleLevel(gEnvironmentSampler,RotateEnvironment(R),saturate(rough)*gEnvironmentRotation.z).rgb*(f0*lut.x+lut.y);
    // Scalar AO on both indirect terms; no added view-dependent specular occlusion.
    return (diffuse*gIbl.y+spec*gIbl.z)*gIbl.w*ao;
}
// WHISP_SCENE_LIGHTS_V1. Matches GpuSceneLight's 80-byte CPU layout.
struct SceneLight { float4 positionRange; float4 directionType; float4 colorIntensity; float4 coneAttenuation; float4 screenBounds; };
StructuredBuffer<SceneLight> gSceneLights:register(t9);
StructuredBuffer<uint2> gLightTiles:register(t10);
StructuredBuffer<uint> gLightIndices:register(t11);
cbuffer SceneLightsCB:register(b7){uint gLightCount,gTilesX,gTilesY,gTiled;uint gLightWidth,gLightHeight,gTileCapacity,gLightReserved;};
struct SpotShadowData{column_major float4x4 lightVP;float4 sampling;float4 config;};
StructuredBuffer<SpotShadowData> gSpotShadows:register(t12);
Texture2D<float> gSpotMap0:register(t13);
Texture2D<float> gSpotMap1:register(t14);
Texture2D<float> gSpotMap2:register(t15);
Texture2D<float> gSpotMap3:register(t16);
float SpotCompare(uint slot,float2 uv,float depth){
    if(slot==0)return gSpotMap0.SampleCmpLevelZero(gShadowSampler,uv,depth);
    if(slot==1)return gSpotMap1.SampleCmpLevelZero(gShadowSampler,uv,depth);
    if(slot==2)return gSpotMap2.SampleCmpLevelZero(gShadowSampler,uv,depth);
    return gSpotMap3.SampleCmpLevelZero(gShadowSampler,uv,depth);
}
float SpotVisibility(uint lightIndex,float3 position){
    if(gReceiveShadows<.5||gLightReserved==0)return 1;
    [unroll]for(uint slot=0;slot<4;++slot){
        SpotShadowData shadow=gSpotShadows[slot];if(shadow.config.x!=float(lightIndex+1))continue;
        float4 clip=mul(shadow.lightVP,float4(position,1));float3 ndc=clip.xyz/max(clip.w,1e-8);
        float2 uv=ndc.xy*float2(.5,-.5)+.5;
        float3 dx=ddx(float3(uv,ndc.z)),dy=ddy(float3(uv,ndc.z));float determinant=dx.x*dy.y-dx.y*dy.x;
        float2 gradient=abs(determinant)>1e-12?float2(dy.y*dx.z-dx.y*dy.z,dx.x*dy.z-dy.x*dx.z)/determinant:0;
        if(clip.w<=0||any(uv<0)||any(uv>1)||ndc.z<0||ndc.z>1)return 1;
        float receiverBias=shadow.sampling.y*shadow.config.z/(clip.w*clip.w);
        float depth=ndc.z-max(receiverBias,asfloat(asuint(ndc.z)+4u)-ndc.z),visibility=0;
        if(shadow.config.y>.5){
            [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){
                float2 tap=uv+float2(x,y)*shadow.sampling.x;
                float2 center=(floor(tap/shadow.sampling.x)+.5)*shadow.sampling.x;
                visibility+=(any(tap<0)||any(tap>1)?1:SpotCompare(slot,tap,depth+dot(gradient,center-uv)))/9;
            }
        }else{
            float2 center=(floor(uv/shadow.sampling.x)+.5)*shadow.sampling.x;
            visibility=SpotCompare(slot,uv,depth+dot(gradient,center-uv));
        }
        return lerp(1,visibility,shadow.sampling.z);
    }
    return 1;
}
float3 LocalContribution(uint lightIndex,SceneLight light,float3 position,float3 base,float m,float rough,float3 N,float3 V,bool pbr){
    float visibility=light.directionType.w>1.5?SpotVisibility(lightIndex,position):1;
    float3 L=-light.directionType.xyz;float attenuation=1;
    if(light.directionType.w>.5){
        float3 delta=light.positionRange.xyz-position;float d2=dot(delta,delta);
        if(d2<1e-12||d2>=light.positionRange.w*light.positionRange.w)return 0;
        float d=sqrt(d2);L=delta/d;float x=d/light.positionRange.w;
        float window=saturate(1-x*x*x*x);
        attenuation=window*window/max(d2,light.coneAttenuation.z*light.coneAttenuation.z);
        if(light.directionType.w>1.5){
            float cosine=dot(-L,light.directionType.xyz);
            float width=light.coneAttenuation.x-light.coneAttenuation.y;
            float t=width>1e-6?saturate((cosine-light.coneAttenuation.y)/width):(cosine>=light.coneAttenuation.y?1:0);
            attenuation*=t*t*(3-2*t);
        }
    }
    attenuation*=visibility;
    float3 radiance=light.colorIntensity.rgb*light.colorIntensity.w*attenuation;
    return (pbr?DirectBRDF(base,m,rough,N,V,L):base*saturate(dot(N,L)))*radiance;
}
float3 SceneDirect(float2 pixel,float3 position,float3 base,float m,float rough,float3 N,float3 V,bool pbr){
    if(gLightCount==0)return 0;
    uint count=gLightCount,start=0;bool tiled=false;
    if(gTiled!=0){
        uint2 xy=min(uint2(pixel)/16,uint2(gTilesX-1,gTilesY-1));uint tile=xy.y*gTilesX+xy.x;
        uint2 info=gLightTiles[tile];
        // Overflow explicitly falls back to every light, without double counting.
        if(info.y==0){count=min(info.x,gTileCapacity);start=tile*gTileCapacity;tiled=true;}
    }
    float3 sum=0;
    [loop]for(uint j=0;j<count;++j){uint index=tiled?gLightIndices[start+j]:j;
        if(index<gLightCount)sum+=LocalContribution(index,gSceneLights[index],position,base,m,rough,N,V,pbr);}
    return sum;
}
float4 Shade(VSOut input,bool hdr) {
    float3 base=SampleBaseLinear(input.uv)*DecodeSRGB(saturate(input.tint.rgb))*input.baseColor.rgb;
    float3 result=base;
    float3 geometryN=SafeUnit(input.worldNormal),N=geometryN;
    uint mapMask=(uint)input.textureParameters.z & (uint)gTextureParameters.w;
    if((mapMask&1)!=0 && abs(input.worldTangent.w)>.99) {
        float3 T=SafeUnit(input.worldTangent.xyz-geometryN*dot(geometryN,input.worldTangent.xyz));
        if(dot(T,T)>.5 && dot(geometryN,geometryN)>.5) {
            float3 mapped=gNormalTexture.Sample(gSampler,input.uv).xyz*2-1;
            mapped.xy*=input.textureParameters.x;mapped=SafeUnit(mapped);
            if(dot(mapped,mapped)>.5) {
                float3 B=cross(geometryN,T)*(input.worldTangent.w<0?-1:1);
                N=SafeUnit(T*mapped.x+B*mapped.y+geometryN*mapped.z);
            }
        }
    }
    float2 mr=(mapMask&2)!=0?gMetallicRoughnessTexture.Sample(gSampler,input.uv).bg:float2(1,1);
    float ao=(mapMask&4)!=0?lerp(1,gOcclusionTexture.Sample(gSampler,input.uv).r,input.textureParameters.y):1;
    if(gDirectionEnabled.w>.5 && dot(N,N)>.5) {
        float visibility=Visibility(input.worldPosition,geometryN);
        if(gShadowParameters.w>.5) {
            float3 ndc=mul(gLightVP,float4(input.worldPosition,1)).xyz;
            bool inside=all(abs(ndc.xy)<=1)&&ndc.z>=0&&ndc.z<=1;
            float3 debug=gShadowParameters.w>1.5?(inside?float3(0,.8,0):float3(1,0,0)):visibility.xxx;return float4(hdr?DecodeSRGB(debug):debug,1);
        }
        float3 ambient=gAmbientColorIntensity.rgb*gAmbientColorIntensity.w;
        float3 radiance=gLightColorIntensity.rgb*gLightColorIntensity.w;
        float3 L=-gDirectionEnabled.xyz;
        if(input.surface.z>.5) {
            float m=Finite(input.surface.x)?saturate(input.surface.x*mr.x):0;
            float3 V=SafeUnit(gCameraPosition.xyz-input.worldPosition);
            result=(gIbl.x>.5?IndirectBRDF(base,m,input.surface.y*mr.y,N,V,ao):base*(1-m)*ambient*ao)
                +DirectBRDF(base,m,input.surface.y*mr.y,N,V,L)*radiance*visibility
                +SceneDirect(input.pos.xy,input.worldPosition,base,m,input.surface.y*mr.y,N,V,true);
        } else {
            result=base*(ambient*ao+radiance*saturate(dot(N,L))*visibility)
                +SceneDirect(input.pos.xy,input.worldPosition,base,0,.5,N,SafeUnit(gCameraPosition.xyz-input.worldPosition),false);
        }
    }
    float3 hdrColor=float3(Finite(result.x)?max(result.x,0):(result.x>0?3.402823e38:0),Finite(result.y)?max(result.y,0):(result.y>0?3.402823e38:0),Finite(result.z)?max(result.z,0):(result.z>0?3.402823e38:0));
    return float4(hdr?hdrColor:EncodeSRGB(result),1);
}

float4 PSMain(VSOut input):SV_Target {return Shade(input,false);}
float4 PSMainHdr(VSOut input):SV_Target {return Shade(input,true);}

// WHISP_INSTANCE_LAYOUT_V1: preserved MVP/tint prefix + padded normal rows at byte 144.
struct VSInstanceIn
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float4 tangent : TANGENT;
    float4 model0 : INSTANCE_MODEL0;
    float4 model1 : INSTANCE_MODEL1;
    float4 model2 : INSTANCE_MODEL2;
    float4 model3 : INSTANCE_MODEL3;
    float4 mvp0 : INSTANCE_MVP0;
    float4 mvp1 : INSTANCE_MVP1;
    float4 mvp2 : INSTANCE_MVP2;
    float4 mvp3 : INSTANCE_MVP3;
    float4 tint : INSTANCE_TINT0;
    float4 normal0 : INSTANCE_NORMAL0;
    float4 normal1 : INSTANCE_NORMAL1;
    float4 normal2 : INSTANCE_NORMAL2;
    float4 surface : INSTANCE_SURFACE0;
    float4 baseColor : INSTANCE_BASECOLOR0;
    float4 textureParameters : INSTANCE_TEXTURE0;
};
VSOut VSInstancedMain(VSInstanceIn input)
{
    VSOut output;
    const float4x4 mvp = transpose(float4x4(input.mvp0,input.mvp1,input.mvp2,input.mvp3));
    output.pos = mul(mvp,float4(input.pos,1.0));
    output.uv = input.uv;
    output.tint = input.tint;
    output.surface=SanitizeSurface(input.surface.xyz);output.baseColor=input.baseColor;
    float4x4 model=transpose(float4x4(input.model0,input.model1,input.model2,input.model3));
    output.worldPosition=mul(model,float4(input.pos,1)).xyz;
    output.worldNormal = TransformNormal(input.normal,input.normal0.xyz,input.normal1.xyz,input.normal2.xyz);
    output.worldTangent=TransformTangent(input.tangent,model,output.worldNormal);
    output.textureParameters=SafeTextureParameters(input.textureParameters);
    return output;
}
