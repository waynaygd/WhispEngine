#pragma once
#include "World.h"
#include "MathTypes.h"
#include <functional>
#include <optional>
namespace ecs {
struct RaycastHit
{
    Entity entity{};
    Vec3 position{}, normal{};
    float distance = 0;
};
// Read simulation transforms, never interpolated render poses.
std::optional<RaycastHit> Raycast(const World&, Vec3 origin, Vec3 direction, float distance, Entity ignore = {},
                                  const std::function<bool(Entity)>& include = {});
std::vector<Entity> FindInRadius(const World&, Vec3 center, float radius);
}
