#version 450

layout(push_constant) uniform PC
{
    mat4 uMVP;
    vec4 uTint;
} pc;

layout(location=0) out vec4 outColor;

void main()
{
    outColor = pc.uTint;
}
