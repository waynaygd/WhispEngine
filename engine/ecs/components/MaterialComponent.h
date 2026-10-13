#pragma once

#include "../Component.h"

#include <string>
#include "../../resources/MaterialResource.h"

namespace ecs
{
struct MaterialComponent : Component
{
    std::string materialPath;
    std::vector<std::string> slotMaterials; // imported material slot index -> asset key
    MaterialOverrides overrides;
    std::string shaderPath;
    std::string texturePath;
    float tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
};
}
