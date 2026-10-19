#pragma once

#include "../Component.h"
#include "../MathTypes.h"

namespace ecs
{
struct TransformComponent : Component
{
    // Local TRS relative to World::Parent(entity); root TRS is in World space.
    // Use World::WorldTransform/SetWorldTransform at spatial system boundaries.
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{ 1.0f, 1.0f, 1.0f };
};
}
