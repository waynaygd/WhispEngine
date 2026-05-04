#pragma once

#include "TextureData.h"
#include "../render/RenderResourceHandles.h"

#include <array>
#include <cstdint>
#include <string>

struct TextureResource
{
    std::string name = "UnnamedTexture";
    std::string sourcePath;
    TextureData textureData;
    std::array<std::uint8_t, 4> fallbackPixel = { 255, 0, 255, 255 };
    RenderTextureHandle gpuHandle{};
    std::uint64_t gpuHandleVersion = 0;
    bool placeholder = true;
};
