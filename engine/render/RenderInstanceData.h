#pragma once
#include <array>
#include "Lighting.h"
#include <cstddef>
#include <cstdint>

// CPU model and clip matrices originate in the common RenderSystem. Retaining
// the prepared MVP preserves the ordinary shader's exact transform convention.
struct RenderInstanceData {
    std::array<float,16> model{};
    std::array<float,16> mvp{};
    std::array<float,4> tint{1,1,1,1};
    std::array<float,12> normal{1,0,0,0,0,1,0,0,0,0,1,0};
    std::array<float,4> surface{0,.5f,0,0}; // metallic, roughness, shading model, reserved
    std::array<float,4> baseColor{1,1,1,1}; // linear material factor
    std::array<float,4> textureParameters{1,1,0,1}; // normalScale, AO strength, enabled map bits, model orientation
};
static_assert(sizeof(RenderInstanceData)==240);
static_assert(offsetof(RenderInstanceData,textureParameters)==224);
static_assert(offsetof(RenderInstanceData,surface)==192 && offsetof(RenderInstanceData,baseColor)==208);
static_assert(offsetof(RenderInstanceData,normal)==144);
static_assert(offsetof(RenderInstanceData,mvp)==64 && offsetof(RenderInstanceData,tint)==128);

struct RenderSubmissionStatistics {
    std::uint64_t drawCalls=0,instancedDrawCalls=0,renderedInstances=0,outputDrawCalls=0;
    std::uint64_t shadowDrawCalls=0,shadowInstancedDrawCalls=0,shadowCasters=0;
    std::uint32_t shadowResolution=0;
    float shadowCoverage=0,shadowDepthRange=0;
    std::uint64_t frameConstantUploadBytes=0,textureMemoryBytes=0,materialDescriptors=0;
    std::uint64_t uploadBytes=0,instanceBufferCapacity=0,instanceBufferGrowths=0;
};
