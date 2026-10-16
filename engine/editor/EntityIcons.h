#pragma once
#include "../ecs/World.h"
#include "../ecs/components/TransformComponent.h"
#include "../ecs/components/TagComponent.h"
#include "../ecs/components/MeshRendererComponent.h"
#include "../ecs/components/LightComponent.h"
#include "../ecs/components/FogComponent.h"
#include "../ecs/components/PlayerStartComponent.h"
#include <array>
#include <tuple>

namespace editor {
inline bool HasEntityIcon(const ecs::World& world, ecs::Entity entity) {
    return world.HasComponent<ecs::TransformComponent>(entity)
        && !world.HasComponent<ecs::MeshRendererComponent>(entity)
        && !world.HasComponent<ecs::DirectionalLightComponent>(entity)
        && !world.HasComponent<ecs::PointLightComponent>(entity)
        && !world.HasComponent<ecs::SpotLightComponent>(entity)
        && !world.HasComponent<ecs::FogComponent>(entity)
        && !world.HasComponent<ecs::PlayerStartComponent>(entity);
}
inline std::string EntityIconLabel(const ecs::World& world, ecs::Entity entity) {
    if (!world.IsAlive(entity)) return {};
    if (const auto* tag = world.GetComponent<ecs::TagComponent>(entity); tag && !tag->name.empty()) return tag->name;
    return "Entity #" + std::to_string(entity.index);
}
struct EntityIconCandidate {
    ecs::Entity entity;
    std::array<float, 3> projection;
};
// The same order determines the topmost drawing, hover and click target.
inline bool NearerEntityIcon(const EntityIconCandidate& a, const EntityIconCandidate& b) {
    return std::tie(a.projection[2], a.entity.index, a.entity.generation)
         < std::tie(b.projection[2], b.entity.index, b.entity.generation);
}
}
