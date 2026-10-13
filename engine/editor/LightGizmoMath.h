#pragma once
#include "../render/SceneLights.h"
#include "../ecs/MathTypes.h"
#include <optional>
namespace editor
{
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
}
