#version 450
layout(set=0,binding=0) uniform sampler2D image;
layout(constant_id=0) const bool SwapchainSrgb=false;
layout(constant_id=1) const bool RuntimeUI=false;
layout(location=0) in vec2 texcoord;
layout(location=1) in vec4 color;
layout(location=0) out vec4 result;
vec3 Decode(vec3 c){return mix(pow((c+.055)/1.055,vec3(2.4)),c/12.92,lessThanEqual(c,vec3(.04045)));}
vec3 Encode(vec3 c){c=clamp(c,0,1);return mix(1.055*pow(c,vec3(1.0/2.4))-.055,c*12.92,lessThanEqual(c,vec3(.0031308)));}
void main(){vec4 sampleColor=texture(image,texcoord);vec3 rgb;
 if(RuntimeUI){rgb=sampleColor.rgb*color.rgb;if(SwapchainSrgb)rgb=Decode(rgb);}
 else {rgb=Decode(sampleColor.rgb)*color.rgb;if(!SwapchainSrgb)rgb=Encode(rgb);}
 result=vec4(rgb,sampleColor.a*color.a);
}
