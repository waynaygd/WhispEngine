#pragma once

#include "../Component.h"

namespace ecs
{
struct BoundsBounceComponent : Component
{
    float minX = -0.9f;
    float maxX = 0.9f;
    float minY = -0.9f;
    float maxY = 0.9f;
};
}
