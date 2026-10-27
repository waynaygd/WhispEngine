#define WHISP_FOG_REGISTER b1
#include "fog_common.hlsli"

Texture2D<float4> Image:register(t0);SamplerState ImageSampler:register(s0);
struct Vertex{float4 position:POSITION;float2 uv:TEXCOORD;float4 color:COLOR;};
struct Pixel{float4 position:SV_Position;float2 uv:TEXCOORD;float4 color:COLOR;};
Pixel VS(Vertex v){Pixel p;p.position=v.position;p.uv=v.uv;p.color=v.color;return p;}
float3 Decode(float3 c){return float3(c.x<=.04045?c.x/12.92:pow((c.x+.055)/1.055,2.4),c.y<=.04045?c.y/12.92:pow((c.y+.055)/1.055,2.4),c.z<=.04045?c.z/12.92:pow((c.z+.055)/1.055,2.4));}
float3 Encode(float3 c){return float3(c.x<=.0031308?12.92*c.x:1.055*pow(c.x,1/2.4)-.055,c.y<=.0031308?12.92*c.y:1.055*pow(c.y,1/2.4)-.055,c.z<=.0031308?12.92*c.z:1.055*pow(c.z,1/2.4)-.055);}
float4 PSHdr(Pixel p):SV_Target{float4 t=Image.Sample(ImageSampler,p.uv);return float4(Decode(t.rgb)*Decode(saturate(p.color.rgb)),t.a*p.color.a);}
float4 PSLdr(Pixel p):SV_Target{float4 c=PSHdr(p);return float4(Encode(c.rgb),c.a);}

float4 PSFogAlpha(Pixel p):SV_Target{float4 c=PSHdr(p);if(c.a<=0)return c;float4 fog=FogAtDepth(int2(p.position.xy),p.position.z);return float4(c.rgb*fog.a+fog.rgb,c.a);}
float4 PSFogAdditive(Pixel p):SV_Target{float4 c=PSHdr(p);if(c.a<=0)return c;float4 fog=FogAtDepth(int2(p.position.xy),p.position.z);return float4(c.rgb*fog.a,c.a);}
