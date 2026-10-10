#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// CPU model and clip matrices originate in the common RenderSystem. Retaining
// the prepared MVP preserves the ordinary shader's exact transform convention.
struct RenderInstanceData {
    std::array<float,16> model{};
    std::array<float,16> mvp{};
    std::array<float,4> tint{1,1,1,1};
};
static_assert(sizeof(RenderInstanceData)==144);
static_assert(offsetof(RenderInstanceData,mvp)==64 && offsetof(RenderInstanceData,tint)==128);

struct RenderSubmissionStatistics {
    std::uint64_t drawCalls=0,instancedDrawCalls=0,renderedInstances=0;
    std::uint64_t uploadBytes=0,instanceBufferCapacity=0,instanceBufferGrowths=0;
};
