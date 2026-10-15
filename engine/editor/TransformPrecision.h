#pragma once
#include "TransformMath.h"
#include <limits>
namespace editor {
enum class TransformPrecision {Compact,Precise,Full};
inline const char* TransformFormat(int mode){return mode==0?"%.3f":mode==1?"%.6f":"%.9g";}
inline float Degrees(float radians){return float(double(radians)*180.0/3.14159265358979323846);}
inline float Radians(float degrees){return float(double(degrees)*3.14159265358979323846/180.0);}
// Compose a pure rotation, not a scaled model matrix: local R*Q, world Q*R.
inline std::optional<ecs::Vec3> RotateQuarterTurn(const ecs::TransformComponent& pose,int axis,int turns,bool world){
 if(axis<0||axis>2)return {};turns=((turns%4)+4)%4;double c=turns%2?0:turns==0?1:-1,s=turns==1?1:turns==3?-1:0;
 auto delta=IdentityMatrix();int a=(axis+1)%3,b=(axis+2)%3;delta[a*4+a]=c;delta[b*4+b]=c;delta[b*4+a]=-s;delta[a*4+b]=s;
 ecs::RenderPose p=ecs::RenderPose::From(pose);p.position={};p.scale={1,1,1};std::array<float,16> rotation;ecs::BuildRenderModelMatrix(rotation.data(),p);ImportMatrix r;for(int i=0;i<16;++i)r[i]=rotation[i];
 auto result=DecomposeTransform(world?MultiplyMatrix(delta,r):MultiplyMatrix(r,delta));if(!result)return {};
 auto v=result->rotation;for(float* x:{&v.x,&v.y,&v.z})if(std::abs(*x)<1e-6f)*x=0;return v;
}
}
