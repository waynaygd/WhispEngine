#pragma once

#include <cstdint>
#include <vector>

struct TextureData
{
    int width = 1;
    int height = 1;
    int sourceChannelCount = 4;
    int channelCount = 4;
    std::vector<std::uint8_t> pixels;
};
