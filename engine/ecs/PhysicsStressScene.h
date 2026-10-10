#pragma once
#include "World.h"
#include "components/TransformComponent.h"
#include "components/ColliderComponent.h"
#include "components/RigidbodyComponent.h"
#include "components/MeshRendererComponent.h"
#include "components/MaterialComponent.h"
#include "components/TagComponent.h"
#include <algorithm>

namespace ecs {
// Shared by the application and headless lifecycle/physics tests. No loading or
// GPU work here: identical resource keys reuse ResourceManager/RenderSystem caches.
inline void ClearPhysicsStressScene(World& world, std::vector<Entity>& entities)
{
    for (auto entity : entities) world.DestroyEntity(entity);
    entities.clear();
}

inline void CreatePhysicsStressScene(World& world, std::vector<Entity>& entities, int count = 500)
{
    ClearPhysicsStressScene(world, entities);
    count = std::clamp(count, 1, 1000);
    entities.reserve(count + 1);
    auto add = [&](Vec3 position, Vec3 scale, bool fixed, int index) {
        const auto entity = world.CreateEntity();
        entities.push_back(entity);
        auto& t = world.AddComponent<TransformComponent>(entity);
        t.position = position; t.scale = scale;
        auto& c = world.AddComponent<ColliderComponent>(entity);
        c.type = ColliderType::Box;
        c.halfExtents = {scale.x * 0.5f, scale.y * 0.5f, scale.z * 0.5f};
        c.autoFitFromMesh = false;
        auto& rb = world.AddComponent<RigidbodyComponent>(entity);
        rb.isStatic = fixed; rb.simulatePhysics = true; rb.useGravity = !fixed; rb.mass = 1.0f;
        world.AddComponent<MeshRendererComponent>(entity).meshPath = "models/validation_cube.obj";
        auto& material = world.AddComponent<MaterialComponent>(entity);
        material.shaderPath = "dx12/textured.hlsl";
        material.texturePath = "defaults/texture";
        material.tint[0] = fixed ? 0.35f : 0.25f + 0.07f * (index % 5);
        material.tint[1] = fixed ? 0.4f : 0.55f + 0.04f * (index % 5);
        material.tint[2] = fixed ? 0.45f : 0.95f;
        world.AddComponent<TagComponent>(entity).name = fixed ? "Stress Floor" : "Stress Cube " + std::to_string(index);
    };
    // Separated from the normal demo. Floor covers the entire spawn footprint.
    add({0, -0.5f, 30}, {24, 1, 24}, true, 0);
    for (int i = 0; i < count; ++i)
        add({(i % 10 - 4.5f) * 1.4f, 3.0f + (i / 100) * 1.4f,
             30.0f + ((i / 10) % 10 - 4.5f) * 1.4f}, {0.8f, 0.8f, 0.8f}, false, i);
}
}
