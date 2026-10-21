#pragma once

#include "../Component.h"
#include "../MathTypes.h"

namespace ecs
{
struct TransformComponent : Component
{
    // Local TRS relative to World::Parent(entity); root TRS is in World space.
    // Use World::WorldTransform/SetWorldTransform at spatial system boundaries.
    // M = T(position) T(pivot) R S T(-pivot); pivot is in entity geometry coordinates.
    Vec3 pivot{};
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{ 1.0f, 1.0f, 1.0f };
};
}
