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
    // CPU resource generation used by the backend PSO key; not serialized.
    std::uint64_t pipelineVersion = 0;
    std::uint64_t gpuAttemptVersion = 0;
    bool gpuColorPipeline = false;
    std::string gpuError;
    bool placeholder = true;
};
