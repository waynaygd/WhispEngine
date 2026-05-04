#pragma once

#include "../render/RenderResourceHandles.h"

#include <cstdint>
#include <string>

struct ShaderResource
{
    std::string name = "UnnamedShader";
    std::string sourcePath;
    std::string vertexPath;
    std::string fragmentPath;
    std::string vertexSource;
    std::string fragmentSource;
    std::string language;
    std::string entryPoint = "main";
    bool hotReloadable = false;
    RenderShaderHandle gpuHandle{};
    std::uint64_t gpuHandleVersion = 0;
    bool placeholder = true;
};
