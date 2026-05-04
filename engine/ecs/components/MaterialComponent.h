#pragma once

#include "../Component.h"

#include <string>

namespace ecs
{
struct MaterialComponent : Component
{
    std::string materialPath;
    std::string shaderPath;
    std::string texturePath;
    float tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
};
}
