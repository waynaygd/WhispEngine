#pragma once

#include <string>

struct MaterialResource
{
    std::string name = "UnnamedMaterial";
    std::string sourcePath;
    std::string shaderPath;
    std::string texturePath;
    float baseColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    bool placeholder = true;
};
