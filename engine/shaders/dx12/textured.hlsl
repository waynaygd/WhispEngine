cbuffer CB0 : register(b0)
{
    float4x4 gMVP;
    float4 gTint;
};

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VSIn
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};

VSOut VSMain(VSIn input)
{
    VSOut output;
    output.pos = mul(gMVP, float4(input.pos, 1.0));
    output.uv = input.uv;
    return output;
}

float4 PSMain(VSOut input) : SV_Target
{
    const float4 sampled = gTexture.Sample(gSampler, input.uv);
    const float3 color = sampled.rgb * gTint.rgb;
    return float4(color, 1.0);
}
