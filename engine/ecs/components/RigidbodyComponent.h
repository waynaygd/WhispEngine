#pragma once
#include "../MathTypes.h"
namespace ecs { struct RigidbodyComponent { Vec3 velocity{}; Vec3 acceleration{}; float mass = 1.0f; bool useGravity = true; bool isStatic = false; bool simulatePhysics = true; float linearDampingMultiplier = 1.0f; bool useAdvancedSphereStabilization = false; }; }
