#pragma once
#include <cstdint>
struct RenderMeshRange {
    std::uint32_t firstIndex=0,indexCount=0; // zero count means whole mesh
    bool operator==(const RenderMeshRange&) const = default;
};
struct MaterialRenderState {
    bool doubleSided=false,receiveShadows=true;
    bool reverseWinding=false; // derived from model determinant, never an asset override
    bool operator==(const MaterialRenderState&) const = default;
};
