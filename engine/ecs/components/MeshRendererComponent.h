#pragma once

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

struct MeshRendererComponent
{
    PrimitiveType primitive = PrimitiveType::Triangle;
    float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    std::string material = "default";
    std::string texture;
    bool visible = true;
};
}
