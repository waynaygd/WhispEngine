// Scene background plus editor-only grid. Unnormalized world ray: plane t is view-space Z.
cbuffer Editor : register(b0) {
    float4 camera;
    float4 rightTan;
    float4 upTan;
    float4 forwardNear;
    float4 clip; // far, width, height, linear HDR target
    float4 grid; // minor, major, opacity, fade distance
    float4 top;
    float4 horizon;
    float4 bottom;
    float4 background; // mode, intensity, blend exponent, rotation radians
    float4 solid;
    float4 sunDirection; // xyz observer-to-sun, w cos angular radius (2 = hidden)
    float4 sunColor; // rgb linear, w independent visible disc intensity
    float4 night; // linear RGB, night intensity
    float4 weather; // horizon glow, sunset warmth
};
TextureCube<float4> sky : register(t0);
SamplerState skySampler : register(s0);
struct Vertex {float4 position:SV_POSITION;float2 ndc:TEXCOORD0;};
Vertex VS(uint id:SV_VertexID){
    Vertex o;float2 uv=float2((id<<1)&2,id&2);
    o.ndc=uv*2-1;o.position=float4(o.ndc,1,1);return o;
}
float3 Ray(float2 ndc){return forwardNear.xyz+rightTan.xyz*(ndc.x*rightTan.w)+upTan.xyz*(ndc.y*upTan.w);}
float3 Output(float3 c){
    c=max(c,0);if(clip.w>0.5)return c;
    return lerp(12.92*c,1.055*pow(c,1.0/2.4)-.055,step(.0031308,c));
}
float4 SkyPS(Vertex v):SV_TARGET {
    float3 d=normalize(Ray(v.ndc)),worldRay=d,c;
    if(background.x>2.5){
        float elevation=sunDirection.y,day=smoothstep(-.08,.18,elevation);
        float twilight=smoothstep(-.22,-.02,elevation)*(1-smoothstep(.05,.35,elevation));
        float3 zenith=lerp(night.rgb*night.w,top.rgb,day),edge=lerp(night.rgb*night.w*.6,horizon.rgb,day);
        c=lerp(edge,d.y>=0?zenith:lerp(night.rgb*night.w*.3,bottom.rgb,day),pow(saturate(abs(d.y)),background.z));
        float2 toward=normalize(sunDirection.xz+float2(1e-6,0));float azimuth=pow(saturate(dot(normalize(d.xz+float2(1e-6,0)),toward)),12);
        c+=float3(1,.25,.055)*weather.y*twilight*exp(-abs(d.y)*8)*(.15+weather.x*azimuth);
    }else if(background.x>1.5){
        float sn=sin(background.w),cs=cos(background.w);
        d.xz=float2(cs*d.x+sn*d.z,-sn*d.x+cs*d.z);
        c=sky.SampleLevel(skySampler,d,0).rgb;
    }else if(background.x>.5){
        float blend=pow(saturate(abs(d.y)),background.z);
        c=lerp(horizon.rgb,d.y>=0?top.rgb:bottom.rgb,blend);
    }else c=solid.rgb;
    c*=background.y;
    [branch] if(sunDirection.w<1.5){
        float alignment=clamp(dot(worldRay,sunDirection.xyz),-1,1);
        float elevation=sunDirection.y,visible=smoothstep(-.05,.02,elevation);
        float warmth=1-smoothstep(0,.4,elevation);
        float3 temperature=lerp(float3(1,.95,.82),float3(1,.38,.09),warmth);
        float attenuation=lerp(.35,1,smoothstep(0,.3,elevation))*visible;
        float glow=exp(-max(0,2*(1-alignment))/max(weather.z*weather.z*.25,1e-8))*weather.w;
        float3 radiance=sunColor.rgb*temperature*sunColor.w*attenuation;
        // HDR uses the existing scene output tone mapper. SDR helper luminance is bounded.
        float3 displaySun=top.w>.5?radiance:radiance/(1+radiance);
        // Continuous scattering over the background: no opaque legacy disc or
        // hole in the halo. Legacy diameter/visibility fields remain loadable.
        float3 halo=displaySun*glow*attenuation;
        c=top.w>.5?c+halo:1-(1-saturate(c))*(1-saturate(halo));
    }
    return float4(Output(c),1);
}
float Coverage(float2 p,float spacing,float2 footprint,float width){
    float2 nearest=abs(frac(p/spacing+.5)-.5)*spacing;
    float2 pixels=nearest/max(footprint,1e-5);
    float coverage=1-smoothstep(width*.5,width*.5+1,min(pixels.x,pixels.y));
    // Fade subpixel periods before they alias at the horizon.
    return coverage*(1-smoothstep(.25,1.5,max(footprint.x,footprint.y)/spacing));
}
struct GridPixel {float4 color:SV_TARGET;float depth:SV_DEPTH;};
GridPixel GridPS(Vertex v){
    float3 ray=Ray(v.ndc);
    float safeY=abs(ray.y)<1e-6?(ray.y<0?-1e-6:1e-6):ray.y;
    float t=-camera.y/safeY;
    float3 p=camera.xyz+ray*t;
    float2 footprint=max(fwidth(p.xz),1e-5);
    float minor=Coverage(p.xz,grid.x,footprint,.65);
    float major=Coverage(p.xz,grid.y,footprint,1.1);
    float xAxis=1-smoothstep(.7,1.7,abs(p.z)/footprint.y);
    float zAxis=1-smoothstep(.7,1.7,abs(p.x)/footprint.x);
    float fadeEnd=min(grid.w,clip.x*.95);
    float fade=(1-smoothstep(fadeEnd*.55,fadeEnd,length(p-camera.xyz)))*saturate(abs(normalize(ray).y)*30);
    float alpha=max(max(minor*.45,major*.85),max(xAxis,zAxis))*grid.z*fade;
    if(abs(ray.y)<1e-5||t<=forwardNear.w||t>=clip.x||alpha<.001)discard;
    float3 c=lerp(float3(.23,.26,.30),float3(.4,.44,.5),major);
    c=lerp(c,float3(.7,.10,.07),xAxis);c=lerp(c,float3(.07,.28,.8),zAxis);
    GridPixel o;o.color=float4(Output(c),alpha);
    // Same DX projection as scene, slightly behind coplanar surfaces. Never write DSV.
    o.depth=saturate(clip.x/(clip.x-forwardNear.w)-(clip.x*forwardNear.w)/(clip.x-forwardNear.w)/t+1e-7);
    return o;
}
