#pragma once

#include "../Component.h"
#include "../MathTypes.h"

namespace ecs
{
struct TransformComponent : Component
{
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{ 1.0f, 1.0f, 1.0f };
};
}
