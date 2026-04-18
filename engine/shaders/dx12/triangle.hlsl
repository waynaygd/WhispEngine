cbuffer CB0 : register(b0)
{
    float4x4 gMVP;
    float4 gTint;
};

struct VSIn
{
    float3 pos : POSITION;
    float3 col : COLOR;
};

struct VSOut
{
    float4 pos : SV_Position;
};

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.pos = mul(gMVP, float4(i.pos, 1.0));
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    (void)i;
    return gTint;
}
