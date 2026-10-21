#pragma once
#include "components/TransformComponent.h"
#include <algorithm>
#include <cmath>
namespace ecs {
struct RenderQuaternion { float x=0, y=0, z=0, w=1; };
inline RenderQuaternion NormalizeQuaternion(RenderQuaternion q) {
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(n<1e-12f) return {};
    return {q.x/n,q.y/n,q.z/n,q.w/n};
}
// Matches the existing model matrix Rz * Ry * Rx, column vectors.
inline RenderQuaternion RenderRotation(Vec3 r) {
    const float x=std::sin(r.x*.5f), X=std::cos(r.x*.5f);
    const float y=std::sin(r.y*.5f), Y=std::cos(r.y*.5f);
    const float z=std::sin(r.z*.5f), Z=std::cos(r.z*.5f);
    return NormalizeQuaternion({x*Y*Z-X*y*z,X*y*Z+x*Y*z,X*Y*z-x*y*Z,X*Y*Z+x*y*z});
}
inline RenderQuaternion RenderSlerp(RenderQuaternion a,RenderQuaternion b,float t) {
    a=NormalizeQuaternion(a); b=NormalizeQuaternion(b);
    float d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(d<0) { b={-b.x,-b.y,-b.z,-b.w}; d=-d; }
    d=std::clamp(d,-1.f,1.f);
    float p=1-t,q=t;
    if(d<.9995f) { const float angle=std::acos(d),s=std::sin(angle);p=std::sin((1-t)*angle)/s;q=std::sin(t*angle)/s; }
    return NormalizeQuaternion({p*a.x+q*b.x,p*a.y+q*b.y,p*a.z+q*b.z,p*a.w+q*b.w});
}
struct RenderPose {
    Vec3 position{},scale{1,1,1}; RenderQuaternion rotation{};
    static RenderPose From(const TransformComponent& t);
};
inline void BuildRenderModelMatrix(float* m,const RenderPose& p) {
    const auto q=p.rotation;
    m[0]=(1-2*(q.y*q.y+q.z*q.z))*p.scale.x; m[1]=2*(q.x*q.y+q.w*q.z)*p.scale.x; m[2]=2*(q.x*q.z-q.w*q.y)*p.scale.x; m[3]=0;
    m[4]=2*(q.x*q.y-q.w*q.z)*p.scale.y; m[5]=(1-2*(q.x*q.x+q.z*q.z))*p.scale.y; m[6]=2*(q.y*q.z+q.w*q.x)*p.scale.y; m[7]=0;
    m[8]=2*(q.x*q.z+q.w*q.y)*p.scale.z; m[9]=2*(q.y*q.z-q.w*q.x)*p.scale.z; m[10]=(1-2*(q.x*q.x+q.y*q.y))*p.scale.z; m[11]=0;
    m[12]=p.position.x;m[13]=p.position.y;m[14]=p.position.z;m[15]=1;
}
inline RenderPose RenderPose::From(const TransformComponent& t){
    RenderPose pose{t.position,t.scale,RenderRotation(t.rotation)};
    if(t.pivot.x!=0||t.pivot.y!=0||t.pivot.z!=0){float m[16];BuildRenderModelMatrix(m,pose);const auto p=t.pivot;
        pose.position={t.position.x+p.x-(m[0]*p.x+m[4]*p.y+m[8]*p.z),t.position.y+p.y-(m[1]*p.x+m[5]*p.y+m[9]*p.z),t.position.z+p.z-(m[2]*p.x+m[6]*p.y+m[10]*p.z)};}
    return pose;
}

}
