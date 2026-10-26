#pragma once
#include <cmath>
#include <stdexcept>
namespace ecs {
enum class CameraProjection { Perspective, Orthographic };
struct CameraComponent {
    bool enabled=true;
    CameraProjection projection=CameraProjection::Perspective;
    float fieldOfView=1.04719755f; // Vertical radians, matching the legacy camera.
    float orthographicSize=10.f; // Full vertical span in world units.
    float nearClip=.1f,farClip=2000.f;
    int priority=0;
};
inline bool ValidCamera(const CameraComponent& c) {
    return (c.projection==CameraProjection::Perspective||c.projection==CameraProjection::Orthographic)
        &&std::isfinite(c.fieldOfView)&&c.fieldOfView>=.017453293f&&c.fieldOfView<=3.1241394f
        &&std::isfinite(c.orthographicSize)&&c.orthographicSize>=.001f&&c.orthographicSize<=100000.f
        &&std::isfinite(c.nearClip)&&std::isfinite(c.farClip)
        &&c.nearClip>=.001f&&c.farClip<=1000000.f&&c.farClip>c.nearClip;
}
inline void ValidateCamera(const CameraComponent& c) {
    if(!ValidCamera(c))throw std::runtime_error("Camera: invalid projection, FOV, size or clip range (near must be less than far)");
}
}
