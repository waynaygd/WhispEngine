#version 450

layout(push_constant) uniform PC
{
    mat4 uMVP;
    vec4 uTint;
} pc;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;

void main()
{
    gl_Position = pc.uMVP * vec4(inPos, 1.0);
}
