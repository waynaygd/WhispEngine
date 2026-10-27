#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
// Full-resolution, deterministic hemisphere SSAO. Radius/bias are view-space
// world units. Debug is authored so reference scenes are reproducible.
struct AmbientOcclusionSettings {
    bool enabled=false;
    int quality=1, debug=0; // Low/Medium/High; Final/Raw/Filtered
    float radius=.75f, intensity=1.f, bias=.025f;
    bool operator==(const AmbientOcclusionSettings&) const=default;
};
inline AmbientOcclusionSettings SanitizeAo(AmbientOcclusionSettings s) {
    s.quality=std::clamp(s.quality,0,2);s.debug=std::clamp(s.debug,0,2);
    s.radius=std::isfinite(s.radius)?std::clamp(s.radius,.01f,10.f):.75f;
    s.intensity=std::isfinite(s.intensity)?std::clamp(s.intensity,0.f,4.f):1.f;
    s.bias=std::isfinite(s.bias)?std::clamp(s.bias,0.f,.5f):.025f;
    return s;
}
struct AmbientOcclusionDiagnostics {
    bool supported=false,active=false;
    double depthMs=0,rawMs=0,filterMs=0,cpuMs=0;
    std::uint64_t gpuBytes=0;
    unsigned depthDraws=0;
    std::string error;
};
