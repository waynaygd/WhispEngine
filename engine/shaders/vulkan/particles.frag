#version 450
layout(set=0,binding=0) uniform sampler2D image;
layout(constant_id=0) const bool SwapchainSrgb=false;
layout(location=0) in vec2 texcoord;
layout(location=1) in vec4 tint;
layout(location=0) out vec4 result;
vec3 Decode(vec3 c){return mix(pow((c+.055)/1.055,vec3(2.4)),c/12.92,lessThanEqual(c,vec3(.04045)));}
vec3 Encode(vec3 c){return mix(1.055*pow(c,vec3(1.0/2.4))-.055,12.92*c,lessThanEqual(c,vec3(.0031308)));}
void main(){vec4 t=texture(image,texcoord);vec3 rgb=Decode(t.rgb)*Decode(clamp(tint.rgb,0,1));if(!SwapchainSrgb)rgb=Encode(rgb);result=vec4(rgb,t.a*tint.a);}
