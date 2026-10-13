cbuffer LightCB : register(b0) { float4x4 lightVP; };
cbuffer ModelCB : register(b1) { float4x4 model; };
struct Vertex { float3 position : POSITION; };
float4 VSMain(Vertex v) : SV_Position { return mul(lightVP,mul(model,float4(v.position,1))); }
struct InstanceVertex {
    float3 position : POSITION;
    float4 model0 : INSTANCE_MODEL0;float4 model1 : INSTANCE_MODEL1;
    float4 model2 : INSTANCE_MODEL2;float4 model3 : INSTANCE_MODEL3;
};
float4 VSInstancedMain(InstanceVertex v) : SV_Position {
    float4x4 m=transpose(float4x4(v.model0,v.model1,v.model2,v.model3));
    return mul(lightVP,mul(m,float4(v.position,1)));
}
