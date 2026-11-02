#pragma once
#include "IRenderAdapter.h"
#include "../resources/ShaderResource.h"
#include "../core/Logger.h"

// One attempt per resource generation, shared by scene and Material Preview.
// Publish only a complete replacement; retire the previous handle afterwards.
inline bool PrepareShaderGpu(IRenderAdapter& renderer, ShaderResource& shader, std::uint64_t version, const std::string& resourceKey) {
    if(shader.gpuHandle.IsValid()&&shader.gpuHandleVersion==version)return true;
    if(shader.gpuAttemptVersion==version&&(!shader.gpuError.empty()||shader.gpuHandle.IsValid()))return shader.gpuHandle.IsValid();
    shader.gpuAttemptVersion=version;shader.pipelineVersion=version;
    const auto replacement=renderer.CreateShaderProgram(shader);
    if(!replacement.IsValid()){
        shader.gpuError="Shader '"+resourceKey+"': compilation / pipeline preparation failed; "+
            (shader.gpuHandle.IsValid()?"retaining last working GPU shader/PSO":"draw skipped (no working GPU shader)");
        Logger::Get().Error(shader.gpuError);return shader.gpuHandle.IsValid();
    }
    const auto previous=shader.gpuHandle;shader.gpuHandle=replacement;shader.gpuHandleVersion=version;
    shader.gpuColorPipeline=shader.fragmentSource.find("PBR_COLOR_PIPELINE_V1")!=std::string::npos;
    shader.gpuError.clear();if(previous.IsValid())renderer.DestroyShader(previous);return true;
}
