#version 450
layout(push_constant) uniform PC {mat4 mvp;vec4 tint;vec4 base;} pc;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=0) out vec2 texcoord;
layout(location=1) out vec4 color;
vec3 Decode(vec3 c){c=clamp(c,0,1);return mix(pow((c+.055)/1.055,vec3(2.4)),c/12.92,lessThanEqual(c,vec3(.04045)));}
void main(){gl_Position=pc.mvp*vec4(position,1);texcoord=uv;color=vec4(Decode(pc.tint.rgb)*pc.base.rgb,pc.tint.a*pc.base.a);}
