#version 450

layout(push_constant) uniform PC
{
    mat4 uMVP;
} pc;

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 vColor;

void main()
{
    vColor = inColor;
    gl_Position = pc.uMVP * vec4(inPos, 0.0, 1.0);
}
