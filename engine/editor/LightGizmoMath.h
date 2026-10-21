#pragma once
#include "../render/SceneLights.h"
#include "../ecs/MathTypes.h"
#include "../ecs/RenderPose.h"
#include <optional>
namespace editor
{
// Gizmo orientation must be invariant under equivalent Euler representations.
// FPS spawn/camera pitch limits are not an editor-overlay transform.
inline ecs::Vec3 PlayerStartGizmoDirection(const ecs::TransformComponent& t){
    const auto q=ecs::RenderRotation(t.rotation);
    return {2*(q.x*q.z+q.w*q.y),2*(q.y*q.z-q.w*q.x),1-2*(q.x*q.x+q.y*q.y)};
}
inline LightCamera GizmoCamera(ecs::Vec3 p,float yaw,float pitch,float fov,float aspect,float nearZ,float farZ)
{
    LightCamera c;float sy=std::sin(yaw),cy=std::cos(yaw),sp=std::sin(pitch),cp=std::cos(pitch);
    c.view={cy,-sy*sp,sy*cp,0,0,cp,sp,0,-sy,-cy*sp,cy*cp,0,
            -(cy*p.x-sy*p.z),-(-sy*sp*p.x+cp*p.y-cy*sp*p.z),-(sy*cp*p.x+sp*p.y+cy*cp*p.z),1};
    float ys=1/std::tan(fov*.5f);c.projection={ys/aspect,0,0,0,0,ys,0,0,0,0,farZ/(farZ-nearZ),1,0,0,-nearZ*farZ/(farZ-nearZ),0};
    c.nearPlane=nearZ;c.farPlane=farZ;return c;
}
inline std::optional<std::array<float,3>> ProjectGizmo(ecs::Vec3 p,const LightCamera& c)
{
    auto& m=c.view;float x=m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],y=m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],z=m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14];
    if(!std::isfinite(z)||z<c.nearPlane||z>c.farPlane)return {};
    float u=.5f+.5f*c.projection[0]*x/z,v=.5f-.5f*c.projection[5]*y/z;
    if(!std::isfinite(u)||!std::isfinite(v)||u<0||u>1||v<0||v>1)return {};
    return std::array<float,3>{u,v,z};
}
inline bool HitLightIcon(float x,float y,float u,float v,float width,float height,float radius=12)
{
    float dx=x-u*width,dy=y-v*height;return dx*dx+dy*dy<=radius*radius;
}
inline std::optional<std::array<float,4>> ProjectEditorLine(ecs::Vec3 a,ecs::Vec3 b,const LightCamera& c){
    auto camera=[&](ecs::Vec3 p){const auto& m=c.view;return ecs::Vec3{m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};};
    a=camera(a);b=camera(b);double lo=0,hi=1,dz=double(b.z)-a.z;
    if(std::abs(dz)<1e-8f){if(a.z<c.nearPlane||a.z>c.farPlane)return {};}
    else {double t0=(double(c.nearPlane)-a.z)/dz,t1=(double(c.farPlane)-a.z)/dz;if(t0>t1)std::swap(t0,t1);lo=std::max(lo,t0);hi=std::min(hi,t1);if(lo>hi)return {};}
    auto point=[&](double t){double z=double(a.z)+double(dz)*t;return std::array<double,2>{.5f+.5f*c.projection[0]*(a.x+(b.x-a.x)*t)/z,.5f-.5f*c.projection[5]*(a.y+(b.y-a.y)*t)/z};};
    auto x=point(lo),y=point(hi);for(float v:{x[0],x[1],y[0],y[1]})if(!std::isfinite(v))return {};
    // Liang-Barsky in normalized viewport coordinates after near/far clipping.
    double enter=0,leave=1,dx=y[0]-x[0],dy=y[1]-x[1];
    auto clip=[&](double p,double q){if(std::abs(p)<1e-12f)return q>=0;double t=q/p;if(p<0)enter=std::max(enter,t);else leave=std::min(leave,t);return enter<=leave;};
    if(!clip(-dx,x[0])||!clip(dx,1-x[0])||!clip(-dy,x[1])||!clip(dy,1-x[1]))return {};
    return std::array<float,4>{float(std::clamp(x[0]+enter*dx,0.0,1.0)),float(std::clamp(x[1]+enter*dy,0.0,1.0)),float(std::clamp(x[0]+leave*dx,0.0,1.0)),float(std::clamp(x[1]+leave*dy,0.0,1.0))};
}
}
