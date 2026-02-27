cbuffer CB0 : register(b0)
{
    float4x4 gMVP;
};

struct VSIn
{
    float3 pos : POSITION;
    float3 col : COLOR;
};

struct VSOut
{
    float4 pos : SV_Position;
    float3 col : COLOR;
};

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.pos = mul(gMVP, float4(i.pos, 1.0));
    o.col = i.col;
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    return float4(i.col, 1.0);
}
