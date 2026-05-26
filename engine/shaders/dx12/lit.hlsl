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
    float3 normal : TEXCOORD1;
};

VSOut VSMain(VSIn input)
{
    VSOut output;
    output.pos = mul(gMVP, float4(input.pos, 1.0));
    output.uv = input.uv;
    output.normal = normalize(input.normal);
    return output;
}

float4 PSMain(VSOut input) : SV_Target
{
    float3 N = normalize(input.normal);
    float3 Ld = normalize(float3(-0.45, -0.8, -0.35));
    float3 Vd = normalize(float3(0.0, 0.0, -1.0));
    float3 H = normalize(-Ld + Vd);

    float NdotL = saturate(dot(N, -Ld));
    float spec = pow(saturate(dot(N, H)), 32.0);

    float3 ambient = float3(0.12, 0.14, 0.18);
    float3 directional = float3(1.0, 0.96, 0.88) * NdotL;
    float3 specular = float3(0.25, 0.25, 0.25) * spec;

    float4 sampled = gTexture.Sample(gSampler, input.uv);
    float3 albedo = sampled.rgb * gTint.rgb;
    float3 lit = albedo * (ambient + directional) + specular;

    return float4(saturate(lit), sampled.a * gTint.a);
}
