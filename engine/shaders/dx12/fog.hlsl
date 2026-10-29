// Opaque fog before world transparency and output tone mapping/FXAA.
#include "fog_common.hlsli"
Texture2D<float4> scene:register(t0);
Texture2D<float> depth:register(t1);
Texture2D<float4> volumeField:register(t4);
struct Out{float4 pos:SV_Position;float2 uv:TEXCOORD;};
Out VS(uint id:SV_VertexID){Out o;o.uv=float2((id<<1)&2,id&2);o.pos=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}
float4 VolumePS(Out i):SV_Target{
    uint w,h;depth.GetDimensions(w,h);int2 pixel=min(int2(i.pos.xy)*2+1,int2(w,h)-1);
    float z=ViewDepth(depth.Load(int3(pixel,0)));float3 ray=Ray(pixel);
    return float4(Scattering(Origin(pixel),ray,RayLength(z,ray),uint2(pixel)),z);
}
float NeighborDepth(int2 pixel,int2 last){return ViewDepth(depth.Load(int3(clamp(pixel,int2(0,0),last),0)));}
float Slope(float center,float low,float high){
    float a=low<0?1e10:center-low,b=high<0?1e10:high-center;
    float s=abs(a)<abs(b)?a:b;return abs(s)<max(.05,center*.02)?s:0;
}
float4 CompositePS(Out i):SV_Target{
    int2 pixel=int2(i.pos.xy);float4 color=scene.Load(int3(pixel,0));float z=ViewDepth(depth.Load(int3(pixel,0)));float3 ray=Ray(pixel);float length=RayLength(z,ray);
    if(volume.z*distanceFog.z<=0)return color;
    uint w,h;depth.GetDimensions(w,h);int2 last=int2(w,h)-1;
    float2 slope=0;if(z>=0)slope=float2(Slope(z,NeighborDepth(pixel-int2(1,0),last),NeighborDepth(pixel+int2(1,0),last)),Slope(z,NeighborDepth(pixel-int2(0,1),last),NeighborDepth(pixel+int2(0,1),last)));
    uint vw,vh;volumeField.GetDimensions(vw,vh);float3 sum=0;float weights=0;
    // Filter scattering only. Scene Color is never blurred and transmittance is
    // reconstructed analytically for this exact full-resolution surface depth.
    [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){
        int2 sample=clamp(pixel/2+int2(x,y),int2(0,0),int2(vw,vh)-1);
        int2 anchor=min(sample*2+1,last);float2 delta=float2(anchor-pixel);float4 value=volumeField.Load(int3(sample,0));
        bool compatible=(z<0&&value.a<0)||(z>=0&&value.a>=0&&abs(value.a-(z+dot(slope,delta)))<max(.025,z*.003));
        if(compatible){float weight=exp(-dot(delta,delta)*.25);sum+=value.rgb*weight;weights+=weight;}
    }
    // Thin foreground objects may have no half-res representative. Integrating
    // their own ray avoids borrowing background fog across a silhouette.
    float3 integrated;[branch]if(weights>1e-6)integrated=sum/weights;else integrated=Scattering(Origin(pixel),ray,length,uint2(pixel));
    return float4(color.rgb*exp(-Tau(Origin(pixel),ray,length))+integrated,color.a);
}
float4 PS(Out i):SV_Target{
    int2 pixel=int2(i.pos.xy);float4 color=scene.Load(int3(pixel,0));
    float4 fog=FogAtDepth(pixel,depth.Load(int3(pixel,0)));
    return float4(color.rgb*fog.a+fog.rgb,color.a);
}
