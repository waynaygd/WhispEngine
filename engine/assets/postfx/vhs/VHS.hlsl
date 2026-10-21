#include "WhispPostFX.hlsli"
float4 PostProcess(float2 uv){
 float2 shifted=uv;shifted.x+=sin(uv.y*40+Time*2)*tracking*.002;
 float4 color=SampleSceneColor(shifted);
 float stripe=.5+.5*sin(uv.y*ScreenSize.y*3.14159265*frequency*.5);
 color.rgb*=1-scanlineStrength*stripe;
 return color; // Engine applies EffectWeight once after this function.
}
