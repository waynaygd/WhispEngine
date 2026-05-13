#pragma once
#include "../MathTypes.h"
namespace ecs { enum class ColliderType { Box, Sphere }; struct ColliderComponent { ColliderType type = ColliderType::Box; Vec3 halfExtents{0.5f,0.5f,0.5f}; Vec3 offset{}; }; }
