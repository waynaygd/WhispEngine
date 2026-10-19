#pragma once
#include "components/TransformComponent.h"
#include <string>
namespace ecs {
// Transient render data only: no entity, scripts, physics or scene ownership.
struct PlacementRenderItem {std::string mesh;TransformComponent transform;};
}
