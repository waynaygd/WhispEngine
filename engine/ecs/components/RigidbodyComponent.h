#pragma once
#include "../MathTypes.h"

namespace ecs {
    struct RigidbodyComponent
    {
        Vec3 velocity{};
        Vec3 acceleration{};

        Vec3 angularVelocity{}; // World-space radians/second, not Euler angle rates.
        Vec3 torque{}; // World-space torque, integrated throughout Update(dt).

        float mass = 1.0f;

        bool useGravity = true;
        bool isStatic = false;
        bool simulatePhysics = true;

        float linearDampingMultiplier = 1.0f;
        float angularDampingMultiplier = 1.0f;

        // Solver-owned state: an entire supported contact island sleeps together.
        // Nonzero velocity, acceleration or torque wakes a sleeping body/island.
        bool sleeping = false;
        float sleepTimer = 0.0f;

        bool useAdvancedSphereStabilization = false;
    };
}
