#pragma once
#include "../ecs/TransformMath.h"
namespace editor {
using ecs::ImportMatrix;using ecs::IdentityMatrix;using ecs::MultiplyMatrix;using ecs::DecomposeTransform;
inline ecs::Vec3 ScalePlane(ecs::Vec3 original,int plane,float factor,float snap=0){
 float values[3]={original.x,original.y,original.z};const int axes[3][2]={{0,1},{0,2},{1,2}};
 if(plane<0||plane>2||!std::isfinite(factor)||factor<=0)return original;
 if(snap>0)factor=std::max(.001f,std::round(factor/snap)*snap);
 for(int axis:axes[plane]){float magnitude=std::abs(values[axis])*factor;magnitude=std::clamp(magnitude,.001f,10000.f);values[axis]=std::copysign(magnitude,values[axis]);}return {values[0],values[1],values[2]};
}
}
