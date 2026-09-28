#pragma once
#include "../MathTypes.h"
namespace ecs {
enum class ColliderType { Box, Sphere };
struct ColliderComponent {
    ColliderType type = ColliderType::Box;
    // Already scaled dimensions and world-space offset (also used by debug rendering).
    Vec3 halfExtents{0.5f, 0.5f, 0.5f};
    Vec3 offset{};
    // Zero is a valid material value; negative values select the system default.
    float restitution = 0.05f;
    float friction = 0.85f;
    bool autoFitFromMesh = true;
};
}
