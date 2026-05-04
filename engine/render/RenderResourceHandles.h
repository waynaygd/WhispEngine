#pragma once

#include <cstdint>

struct RenderMeshHandle
{
    std::uint64_t value = 0;

    [[nodiscard]] bool IsValid() const { return value != 0; }
    static constexpr RenderMeshHandle Invalid() { return {}; }

    friend bool operator==(const RenderMeshHandle& lhs, const RenderMeshHandle& rhs)
    {
        return lhs.value == rhs.value;
    }
};

struct RenderTextureHandle
{
    std::uint64_t value = 0;

    [[nodiscard]] bool IsValid() const { return value != 0; }
    static constexpr RenderTextureHandle Invalid() { return {}; }

    friend bool operator==(const RenderTextureHandle& lhs, const RenderTextureHandle& rhs)
    {
        return lhs.value == rhs.value;
    }
};

struct RenderShaderHandle
{
    std::uint64_t value = 0;

    [[nodiscard]] bool IsValid() const { return value != 0; }
    static constexpr RenderShaderHandle Invalid() { return {}; }

    friend bool operator==(const RenderShaderHandle& lhs, const RenderShaderHandle& rhs)
    {
        return lhs.value == rhs.value;
    }
};
