#pragma once

#include "../Component.h"
#include <string>

namespace ecs
{
struct MeshRendererComponent : Component
{
    std::string meshPath;
    std::string texturePath;
    std::string shaderPath;
    bool visible = true;
};
}
