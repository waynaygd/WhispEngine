#pragma once

#include "../Component.h"
#include "../MathTypes.h"

namespace ecs
{
struct VelocityComponent : Component
{
    Vec3 linear{};
    Vec3 angular{};
};
}
