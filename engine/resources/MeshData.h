#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include "MaterialResource.h"

struct MeshVertex
{
    float position[3] = { 0.0f, 0.0f, 0.0f };
    float normal[3] = { 0.0f, 0.0f, 0.0f };
    float uv[2] = { 0.0f, 0.0f };
    float tangent[4] = {0,0,0,0}; // xyz direction, w handedness; zero means unavailable.
};

static_assert(sizeof(MeshVertex)==48 && offsetof(MeshVertex,tangent)==32);
static_assert(offsetof(MeshVertex,normal)==12 && offsetof(MeshVertex,uv)==24);

struct MeshSubmesh
{
    std::string name;
    std::uint32_t baseVertex = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::string materialName;
    std::string diffuseTexturePath;
    std::uint32_t materialSlot = 0;
    bool hasUV0=true;
};

struct MeshData
{
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<MeshSubmesh> submeshes;
    std::vector<MaterialResource> materials;
};
