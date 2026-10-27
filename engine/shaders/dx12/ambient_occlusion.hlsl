// Non-reversed D32, left-handed view space (+Z). Geometric normals are
// reconstructed with the nearest-depth neighbor on each axis, avoiding the
// large foreground/background triangles produced by naive derivatives.
Texture2D<float> Depth:register(t0);
Texture2D<float4> Raw:register(t1);
Texture2D<float> Filtered:register(t2);
cbuffer Constants:register(b0){
    float4 Projection; // P00,P11,P22,P32
    float4 Dimensions; // width,height,P23,P33
    float4 Settings;   // world radius,intensity,world bias,sample count
};
struct Varying{float4 position:SV_Position;};
Varying VS(uint id:SV_VertexID){Varying v;v.position=float4(id==2?3:-1,id==1?3:-1,0,1);return v;}
bool Inside(int2 p){return all(p>=0)&&all(p<int2(Dimensions.xy));}
float3 Position(int2 p){
    float d=Depth.Load(int3(clamp(p,0,int2(Dimensions.xy)-1),0));
    float z=(Projection.w-d*Dimensions.w)/(d*Dimensions.z-Projection.z);
    float2 ndc=(float2(p)+.5)/Dimensions.xy*float2(2,-2)+float2(-1,1);
    return float3(ndc*(z*Dimensions.z+Dimensions.w)/Projection.xy,z);
}
float3 Normal(int2 p,float3 P){
    int2 l=max(p-int2(1,0),0),r=min(p+int2(1,0),int2(Dimensions.xy)-1);
    int2 t=max(p-int2(0,1),0),b=min(p+int2(0,1),int2(Dimensions.xy)-1);
    float3 L=Position(l),R=Position(r),T=Position(t),B=Position(b);
    float3 dx=abs(L.z-P.z)<abs(R.z-P.z)?P-L:R-P;
    float3 dy=abs(T.z-P.z)<abs(B.z-P.z)?P-T:B-P;
    float3 n=cross(dx,dy);float n2=dot(n,n);
    n=n2>1e-16?n*rsqrt(n2):float3(0,0,-1);
    float3 viewRay=Dimensions.z>.5?P:float3(0,0,1);
    return dot(n,viewRay)>0?-n:n;
}
float4 RawPS(Varying v):SV_Target{
    int2 pixel=int2(v.position.xy);
    if(Depth.Load(int3(pixel,0))>=1)return float4(1,0,0,0);
    float3 P=Position(pixel),N=Normal(pixel,P);
    float3 tangent=normalize(cross(abs(N.z)<.95?float3(0,0,1):float3(0,1,0),N));
    float3 bitangent=cross(N,tangent);
    // Static interleaved rotation, no frame jitter or temporal history.
    float rotation=frac(dot(float2(pixel%4),float2(.754877666,.569840296)))*6.2831853;
    float occupied=0;
    uint count=(uint)Settings.w;
    [loop]for(uint i=0;i<count;++i){
        float u=(i+.5)/count,azimuth=i*2.39996323+rotation;
        float2 disk=float2(cos(azimuth),sin(azimuth))*sqrt(u);
        float3 direction=tangent*disk.x+bitangent*disk.y+N*sqrt(1-u);
        float scale=.15+.85*frac((i+.5)*.618033989);
        float3 sampleP=P+direction*Settings.x*scale;
        if(sampleP.z<=0)continue;
        float w=sampleP.z*Dimensions.z+Dimensions.w;
        float2 uv=sampleP.xy*Projection.xy/w*float2(.5,-.5)+.5;
        int2 q=int2(uv*Dimensions.xy);
        if(!Inside(q)||Depth.Load(int3(q,0))>=1)continue; // missing screen data is unoccluded
        float3 surface=Position(q),delta=surface-P;
        float distance=length(delta);
        // Reject far discontinuities (halo protection) and coplanar samples.
        float rangeWeight=saturate(1-distance/Settings.x);
        occupied+=(surface.z<sampleP.z-Settings.z&&dot(delta,N)>Settings.z)?rangeWeight:0;
    }
    return float4(saturate(1-Settings.y*occupied/count),N);
}
float FilterPS(Varying v):SV_Target{
    int2 p=int2(v.position.xy);float4 center=Raw.Load(int3(p,0));
    if(Depth.Load(int3(p,0))>=1)return 1;
    float3 P=Position(p);float sum=0,weight=0;
    [unroll]for(int y=-2;y<=2;++y)[unroll]for(int x=-2;x<=2;++x){
        int2 q=p+int2(x,y);if(!Inside(q)||Depth.Load(int3(q,0))>=1)continue;
        float4 other=Raw.Load(int3(q,0));
        float normal=pow(saturate(dot(center.yzw,other.yzw)),16);
        float depth=exp(-abs(Position(q).z-P.z)/max(Settings.x*.1,.001));
        float spatial=exp(-float(x*x+y*y)/4);
        float w=normal*depth*spatial;sum+=other.x*w;weight+=w;
    }
    return saturate(weight>1e-6?sum/weight:center.x);
}
// Debug is a literal occlusion-factor display after scene output conversion,
// before runtime UI: exposure, fog and grading must not alter measurements.
float4 DebugRawPS(Varying v):SV_Target{return float4(Raw.Load(int3(int2(v.position.xy),0)).xxx,1);}
float4 DebugFilteredPS(Varying v):SV_Target{return float4(Filtered.Load(int3(int2(v.position.xy),0)).xxx,1);}
