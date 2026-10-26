#pragma once
#include "../MathTypes.h"
#include <cmath>
#include <initializer_list>
namespace ecs {
// A visual override only; never a replacement for authored/physical Transform.
struct CameraAttachmentComponent {
    bool enabled=true;
    Vec3 localPosition{},localRotation{}; // Camera axes, Euler radians Rz*Ry*Rx.
};
inline bool ValidCameraAttachment(const CameraAttachmentComponent& c){
    for(float v:{c.localPosition.x,c.localPosition.y,c.localPosition.z,c.localRotation.x,c.localRotation.y,c.localRotation.z})
        if(!std::isfinite(v)||std::abs(v)>1000000.f)return false;
    return true;
}
}
