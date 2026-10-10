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
    nointerpolation float4 tint : COLOR0;
};

VSOut VSMain(VSIn input)
{
    VSOut output;
    output.pos = mul(gMVP, float4(input.pos, 1.0));
    output.uv = input.uv;
    output.tint = gTint;
    return output;
}

float4 PSMain(VSOut input) : SV_Target
{
    const float4 sampled = gTexture.Sample(gSampler, input.uv);
    const float3 color = sampled.rgb * input.tint.rgb;
    return float4(color, 1.0);
}

// WHISP_INSTANCE_LAYOUT_V1: column-major prepared MVP + per-instance tint.
struct VSInstanceIn
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float4 mvp0 : INSTANCE_MVP0;
    float4 mvp1 : INSTANCE_MVP1;
    float4 mvp2 : INSTANCE_MVP2;
    float4 mvp3 : INSTANCE_MVP3;
    float4 tint : INSTANCE_TINT0;
};
VSOut VSInstancedMain(VSInstanceIn input)
{
    VSOut output;
    const float4x4 mvp = transpose(float4x4(input.mvp0,input.mvp1,input.mvp2,input.mvp3));
    output.pos = mul(mvp,float4(input.pos,1.0));
    output.uv = input.uv;
    output.tint = input.tint;
    return output;
}
