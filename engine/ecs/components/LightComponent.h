#pragma once

#include "../Component.h"
#include "../MathTypes.h"

namespace ecs
{
enum class LightType
{
    Directional,
    Point,
    Spot
};

struct LightComponent : Component
{
    LightType type = LightType::Point;
    Vec3 color{ 1.0f, 1.0f, 1.0f };
    float intensity = 1.0f;
    float range = 10.0f;
    float innerConeAngle = 15.0f;
    float outerConeAngle = 25.0f;
    bool castsShadows = true;
    float shadowBias = 0.001f;
    float normalBias = 0.0f;
    int shadowMapResolution = 1024;
    bool enabled = true;
};
}
