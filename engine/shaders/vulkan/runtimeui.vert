#version 450
layout(push_constant) uniform PC {mat4 mvp;vec4 tint;vec4 base;} pc;
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 vertexColor;
layout(location=0) out vec2 texcoord;
layout(location=1) out vec4 color;
void main(){gl_Position=pc.mvp*vec4(position,0,1);texcoord=uv;color=vertexColor;}
