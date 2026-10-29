#pragma once
#include "AmbientOcclusion.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
struct EnvironmentSettings
{
    AmbientOcclusionSettings ambientOcclusion;
    // enabled controls IBL only. Exposure (EV) and toneMapping are global scene
    // output settings, including when IBL is disabled.
    bool enabled = false, diffuse = true, specular = true, toneMapping = true;
    std::string assetPath = "environments/studio.hdr";
    float intensity = 1, rotationDegrees = 0, exposure = 0;
    bool operator==(const EnvironmentSettings &) const = default;
};
struct EnvironmentDiagnostics
{
    std::string status = "Disabled", error;
    unsigned cubeResolution = 0, irradianceResolution = 0, mipCount = 0, lutResolution = 0;
    std::uint64_t gpuBytes = 0, generation = 0;
    double preprocessMs = 0, uploadMs = 0;
};
inline EnvironmentSettings SanitizeEnvironment(EnvironmentSettings s)
{
    s.ambientOcclusion=SanitizeAo(s.ambientOcclusion);
    s.intensity = std::isfinite(s.intensity) ? std::clamp(s.intensity, 0.f, 10000.f) : 1;
    s.rotationDegrees =
        std::isfinite(s.rotationDegrees) ? std::remainder(s.rotationDegrees, 360.f) : 0;
    s.exposure = std::isfinite(s.exposure) ? std::clamp(s.exposure, -16.f, 16.f) : 0;
    return s;
}
