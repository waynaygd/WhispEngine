#pragma once
#include "RenderResourceHandles.h"
#include "../resources/MaterialResource.h"
#include <array>
struct MaterialPreviewFrame {
    RenderMeshHandle mesh;RenderShaderHandle shader;
    std::array<RenderTextureHandle,4> textures{};
    MaterialResource material;
    unsigned width=640,height=480;
    float yaw=.65f,pitch=.3f,distance=3.7f,panX=0,panY=0;
    int lighting=0;bool dark=false,colorPipeline=true;
};
struct MaterialPreviewMetrics {
    double submissionMs=0,gpuMs=-1;
    std::uint64_t allocations=0,draws=0,targetBytes=0;
};
