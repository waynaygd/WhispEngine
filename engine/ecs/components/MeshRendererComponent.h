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
    bool castShadows = true;
    bool receiveShadows = true;
    float albedoColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float shininess = 32.0f;
    bool useTexture = true;
};
}
