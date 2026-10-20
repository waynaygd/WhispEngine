#pragma once
#include "../../render/PostProcess.h"
#include "../MathTypes.h"
#include <optional>
namespace ecs {
struct PostProcessVolumeComponent {
    bool enabled=true,global=true;
    int priority=0,shape=0; // Box only in PPV.1.
    float weight=1,blendRadius=2;
    Vec3 extents{2,2,2}; // Local-space half extents, independent of mesh bounds.
#define PP_FIELD(name,target,lo,hi,section,label) std::optional<float> name;
    WHISP_PP_FLOATS(PP_FIELD)
#undef PP_FIELD
    std::optional<bool> fogEnabled,sceneLighting,toneMapping;
    std::optional<FogMode> fogMode;
    std::optional<FogQuality> fogQuality;
    std::optional<std::array<float,4>> fogColor,scatteringColor;
};
std::string ValidatePostProcessVolume(const PostProcessVolumeComponent&);
}
