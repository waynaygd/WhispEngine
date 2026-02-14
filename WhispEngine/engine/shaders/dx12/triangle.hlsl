struct VSIn
{
    float3 pos : POSITION;
    float3 color : COLOR0;
};

struct VSOut
{
    float4 pos : SV_Position;
    float3 color : COLOR0;
};

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.pos = float4(i.pos, 1.0);
    o.color = i.color;
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    return float4(i.color, 1.0);
}
