#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct MeshVertex
{
    float position[3] = { 0.0f, 0.0f, 0.0f };
    float normal[3] = { 0.0f, 0.0f, 1.0f };
    float uv[2] = { 0.0f, 0.0f };
};

struct MeshSubmesh
{
    std::string name;
    std::uint32_t baseVertex = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::string materialName;
    std::string diffuseTexturePath;
};

struct MeshData
{
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<MeshSubmesh> submeshes;
};
