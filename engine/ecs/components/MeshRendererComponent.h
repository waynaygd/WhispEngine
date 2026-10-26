#pragma once

#include "../Component.h"
#include <string>

namespace ecs
{
enum class MeshRenderMode { World=0,FirstPerson=1 };
struct MeshRendererComponent : Component
{
    std::string meshPath;
    std::string texturePath;
    std::string shaderPath;
    bool visible = true;
    MeshRenderMode renderMode = MeshRenderMode::World;
};
}
