#pragma once

#include "../Component.h"
#include "../MathTypes.h"

#include <string>

namespace ecs
{
enum class PrimitiveType
{
    Line,
    Triangle,
    Quad,
    Cube
};

struct MeshRendererComponent : Component
{
    PrimitiveType primitive = PrimitiveType::Triangle;
    Vec4 color{};
    std::string material = "default";
    std::string texture;
    bool visible = true;
};
}
