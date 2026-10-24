#pragma once
#include "../ecs/CameraState.h"
namespace editor {
struct CameraFrustum {
    std::array<ecs::Vec3,8> corners;
    bool shortened=false;
    float displayedFar=0;
};
inline ecs::Vec3 CameraPoint(const ecs::CameraState& c,float x,float y,float z){
    return {c.pose.position.x+c.right.x*x+c.up.x*y+c.forward.x*z,
        c.pose.position.y+c.right.y*x+c.up.y*y+c.forward.y*z,
        c.pose.position.z+c.right.z*x+c.up.z*y+c.forward.z*z};
}
inline CameraFrustum BuildCameraFrustum(const ecs::CameraState& c,float maximum=200.f){
    CameraFrustum result;result.displayedFar=std::min(c.settings.farClip,std::max(c.settings.nearClip,maximum));
    result.shortened=result.displayedFar<c.settings.farClip;
    for(int plane=0;plane<2;++plane){float z=plane?result.displayedFar:c.settings.nearClip;
        float k=c.settings.projection==ecs::CameraProjection::Perspective?z:1.f;
        float x=k/c.projection[0],y=k/c.projection[5];
        for(int j=0;j<4;++j)result.corners[plane*4+j]=CameraPoint(c,(j==0||j==3)?-x:x,j<2?-y:y,z);
    }
    return result;
}
}
