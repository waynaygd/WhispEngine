#include "WhispPostFX.hlsli"
float4 PostProcess(float2 uv)
{
    float2 centered = uv * 2 - 1;
    float2 warped = (centered * (1 + strength * dot(centered, centered)) + 1) * .5;
    return SampleSceneColor(saturate(warped));
}
