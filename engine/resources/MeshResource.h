#pragma once

#include "MeshData.h"
#include "../render/RenderResourceHandles.h"

#include <cstdint>
#include <string>

struct MeshResource
{
    std::string name = "UnnamedMesh";
    std::string sourcePath;
    MeshData meshData;
    RenderMeshHandle gpuHandle{};
    std::uint64_t gpuHandleVersion = 0;
    bool placeholder = true;
};
