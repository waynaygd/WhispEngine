#include "TextureLoader.h"

#include "../../core/AssetPaths.h"
#include "../../core/Logger.h"

#include <stb_image.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

namespace
{
constexpr char kTextureCacheMagic[4] = { 'W', 'T', 'E', 'X' };
constexpr std::uint32_t kTextureCacheVersion = 3;
constexpr std::uint64_t kFnv64OffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kFnv64Prime = 1099511628211ull;

std::filesystem::path BuildBinaryCachePath(const std::filesystem::path& sourcePath)
{
    std::filesystem::path cachePath = sourcePath;
    cachePath += ".wtex";
    return cachePath;
}

bool LoadTextureBinaryCache(
    const std::filesystem::path& cachePath,
    const std::string& normalizedKey,
    std::uint64_t expectedSourceHash,
    TextureResource& outTexture)
{
    std::ifstream stream(cachePath, std::ios::binary);
    if (!stream.is_open())
        return false;

    char magic[4]{};
    std::uint32_t version = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t sourceChannels = 0;
    std::uint32_t storedChannels = 0;
    std::uint32_t pixelCount = 0;
    std::uint64_t sourceHash = 0;
    stream.read(magic, sizeof(magic));
    stream.read(reinterpret_cast<char*>(&version), sizeof(version));
    stream.read(reinterpret_cast<char*>(&width), sizeof(width));
    stream.read(reinterpret_cast<char*>(&height), sizeof(height));
    stream.read(reinterpret_cast<char*>(&sourceChannels), sizeof(sourceChannels));
    stream.read(reinterpret_cast<char*>(&storedChannels), sizeof(storedChannels));
    stream.read(reinterpret_cast<char*>(&pixelCount), sizeof(pixelCount));
    stream.read(reinterpret_cast<char*>(&sourceHash), sizeof(sourceHash));

    if (!stream.good() ||
        std::memcmp(magic, kTextureCacheMagic, sizeof(kTextureCacheMagic)) != 0 ||
        version != kTextureCacheVersion ||
        storedChannels != 4 ||
        sourceHash != expectedSourceHash)
    {
        return false;
    }

    outTexture.name = cachePath.stem().stem().string();
    outTexture.sourcePath = normalizedKey;
    outTexture.textureData.width = static_cast<int>(width);
    outTexture.textureData.height = static_cast<int>(height);
    outTexture.textureData.sourceChannelCount = static_cast<int>(sourceChannels);
    outTexture.textureData.channelCount = static_cast<int>(storedChannels);
    outTexture.textureData.pixels.resize(pixelCount);
    if (pixelCount > 0)
    {
        stream.read(
            reinterpret_cast<char*>(outTexture.textureData.pixels.data()),
            static_cast<std::streamsize>(pixelCount));
    }
    outTexture.placeholder = false;
    return stream.good();
}

void SaveTextureBinaryCache(
    const std::filesystem::path& cachePath,
    const TextureResource& texture,
    std::uint64_t sourceHash)
{
    std::ofstream stream(cachePath, std::ios::binary);
    if (!stream.is_open())
        return;

    const auto width = static_cast<std::uint32_t>(texture.textureData.width);
    const auto height = static_cast<std::uint32_t>(texture.textureData.height);
    const auto sourceChannels = static_cast<std::uint32_t>(texture.textureData.sourceChannelCount);
    const auto storedChannels = static_cast<std::uint32_t>(texture.textureData.channelCount);
    const auto pixelCount = static_cast<std::uint32_t>(texture.textureData.pixels.size());
    stream.write(kTextureCacheMagic, sizeof(kTextureCacheMagic));
    stream.write(reinterpret_cast<const char*>(&kTextureCacheVersion), sizeof(kTextureCacheVersion));
    stream.write(reinterpret_cast<const char*>(&width), sizeof(width));
    stream.write(reinterpret_cast<const char*>(&height), sizeof(height));
    stream.write(reinterpret_cast<const char*>(&sourceChannels), sizeof(sourceChannels));
    stream.write(reinterpret_cast<const char*>(&storedChannels), sizeof(storedChannels));
    stream.write(reinterpret_cast<const char*>(&pixelCount), sizeof(pixelCount));
    stream.write(reinterpret_cast<const char*>(&sourceHash), sizeof(sourceHash));
    if (pixelCount > 0)
    {
        stream.write(
            reinterpret_cast<const char*>(texture.textureData.pixels.data()),
            static_cast<std::streamsize>(pixelCount));
    }
}

std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    if (offset + 4 > bytes.size())
        return 0;

    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::uint32_t ReadU32(const std::uint8_t* bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) |
        (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::uint16_t ReadU16(const std::uint8_t* bytes)
{
    return static_cast<std::uint16_t>(bytes[0]) |
        static_cast<std::uint16_t>(bytes[1] << 8);
}

std::array<std::uint8_t, 4> DecodeRgb565(std::uint16_t value)
{
    const std::uint8_t r5 = static_cast<std::uint8_t>((value >> 11) & 0x1F);
    const std::uint8_t g6 = static_cast<std::uint8_t>((value >> 5) & 0x3F);
    const std::uint8_t b5 = static_cast<std::uint8_t>(value & 0x1F);
    return
    {
        static_cast<std::uint8_t>((r5 << 3) | (r5 >> 2)),
        static_cast<std::uint8_t>((g6 << 2) | (g6 >> 4)),
        static_cast<std::uint8_t>((b5 << 3) | (b5 >> 2)),
        255
    };
}

bool DecodeDxt5ToRgba8(
    const std::uint8_t* compressed,
    int width,
    int height,
    std::vector<std::uint8_t>& outPixels)
{
    if (compressed == nullptr || width <= 0 || height <= 0)
        return false;

    outPixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);
    const int blockCountX = (width + 3) / 4;
    const int blockCountY = (height + 3) / 4;

    for (int blockY = 0; blockY < blockCountY; ++blockY)
    {
        for (int blockX = 0; blockX < blockCountX; ++blockX)
        {
            const std::uint8_t* block = compressed + ((blockY * blockCountX + blockX) * 16);
            std::uint8_t alphaTable[8]{};
            alphaTable[0] = block[0];
            alphaTable[1] = block[1];
            if (alphaTable[0] > alphaTable[1])
            {
                alphaTable[2] = static_cast<std::uint8_t>((6 * alphaTable[0] + 1 * alphaTable[1]) / 7);
                alphaTable[3] = static_cast<std::uint8_t>((5 * alphaTable[0] + 2 * alphaTable[1]) / 7);
                alphaTable[4] = static_cast<std::uint8_t>((4 * alphaTable[0] + 3 * alphaTable[1]) / 7);
                alphaTable[5] = static_cast<std::uint8_t>((3 * alphaTable[0] + 4 * alphaTable[1]) / 7);
                alphaTable[6] = static_cast<std::uint8_t>((2 * alphaTable[0] + 5 * alphaTable[1]) / 7);
                alphaTable[7] = static_cast<std::uint8_t>((1 * alphaTable[0] + 6 * alphaTable[1]) / 7);
            }
            else
            {
                alphaTable[2] = static_cast<std::uint8_t>((4 * alphaTable[0] + 1 * alphaTable[1]) / 5);
                alphaTable[3] = static_cast<std::uint8_t>((3 * alphaTable[0] + 2 * alphaTable[1]) / 5);
                alphaTable[4] = static_cast<std::uint8_t>((2 * alphaTable[0] + 3 * alphaTable[1]) / 5);
                alphaTable[5] = static_cast<std::uint8_t>((1 * alphaTable[0] + 4 * alphaTable[1]) / 5);
                alphaTable[6] = 0;
                alphaTable[7] = 255;
            }

            std::uint64_t alphaBits = 0;
            for (int i = 0; i < 6; ++i)
                alphaBits |= static_cast<std::uint64_t>(block[2 + i]) << (8 * i);

            const std::uint8_t* colorBlock = block + 8;
            const auto c0 = DecodeRgb565(ReadU16(colorBlock));
            const auto c1 = DecodeRgb565(ReadU16(colorBlock + 2));
            std::array<std::array<std::uint8_t, 4>, 4> colors{};
            colors[0] = c0;
            colors[1] = c1;
            for (int component = 0; component < 3; ++component)
            {
                colors[2][component] = static_cast<std::uint8_t>((2 * colors[0][component] + colors[1][component]) / 3);
                colors[3][component] = static_cast<std::uint8_t>((colors[0][component] + 2 * colors[1][component]) / 3);
            }
            colors[2][3] = 255;
            colors[3][3] = 255;

            const std::uint32_t colorBits = ReadU32(colorBlock + 4);

            for (int py = 0; py < 4; ++py)
            {
                for (int px = 0; px < 4; ++px)
                {
                    const int pixelIndex = py * 4 + px;
                    const int dstX = blockX * 4 + px;
                    const int dstY = blockY * 4 + py;
                    if (dstX >= width || dstY >= height)
                        continue;

                    const std::uint32_t colorIndex = (colorBits >> (2 * pixelIndex)) & 0x3;
                    const std::uint32_t alphaIndex = static_cast<std::uint32_t>((alphaBits >> (3 * pixelIndex)) & 0x7);
                    const std::size_t dstOffset =
                        (static_cast<std::size_t>(dstY) * static_cast<std::size_t>(width) +
                        static_cast<std::size_t>(dstX)) * 4u;

                    outPixels[dstOffset + 0] = colors[colorIndex][0];
                    outPixels[dstOffset + 1] = colors[colorIndex][1];
                    outPixels[dstOffset + 2] = colors[colorIndex][2];
                    outPixels[dstOffset + 3] = alphaTable[alphaIndex];
                }
            }
        }
    }

    return true;
}

bool DecodeDxt1ToRgba8(
    const std::uint8_t* compressed,
    int width,
    int height,
    std::vector<std::uint8_t>& outPixels)
{
    if (compressed == nullptr || width <= 0 || height <= 0)
        return false;

    outPixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);
    const int blockCountX = (width + 3) / 4;
    const int blockCountY = (height + 3) / 4;

    for (int blockY = 0; blockY < blockCountY; ++blockY)
    {
        for (int blockX = 0; blockX < blockCountX; ++blockX)
        {
            const std::uint8_t* block = compressed + ((blockY * blockCountX + blockX) * 8);
            const std::uint16_t color0 = ReadU16(block);
            const std::uint16_t color1 = ReadU16(block + 2);
            const auto c0 = DecodeRgb565(color0);
            const auto c1 = DecodeRgb565(color1);
            std::array<std::array<std::uint8_t, 4>, 4> colors{};
            colors[0] = c0;
            colors[1] = c1;

            if (color0 > color1)
            {
                for (int component = 0; component < 3; ++component)
                {
                    colors[2][component] = static_cast<std::uint8_t>((2 * colors[0][component] + colors[1][component]) / 3);
                    colors[3][component] = static_cast<std::uint8_t>((colors[0][component] + 2 * colors[1][component]) / 3);
                }
                colors[2][3] = 255;
                colors[3][3] = 255;
            }
            else
            {
                for (int component = 0; component < 3; ++component)
                    colors[2][component] = static_cast<std::uint8_t>((colors[0][component] + colors[1][component]) / 2);
                colors[2][3] = 255;
                colors[3] = { 0, 0, 0, 0 };
            }

            const std::uint32_t colorBits = ReadU32(block + 4);
            for (int py = 0; py < 4; ++py)
            {
                for (int px = 0; px < 4; ++px)
                {
                    const int pixelIndex = py * 4 + px;
                    const int dstX = blockX * 4 + px;
                    const int dstY = blockY * 4 + py;
                    if (dstX >= width || dstY >= height)
                        continue;

                    const std::uint32_t colorIndex = (colorBits >> (2 * pixelIndex)) & 0x3;
                    const std::size_t dstOffset =
                        (static_cast<std::size_t>(dstY) * static_cast<std::size_t>(width) +
                        static_cast<std::size_t>(dstX)) * 4u;
                    outPixels[dstOffset + 0] = colors[colorIndex][0];
                    outPixels[dstOffset + 1] = colors[colorIndex][1];
                    outPixels[dstOffset + 2] = colors[colorIndex][2];
                    outPixels[dstOffset + 3] = colors[colorIndex][3];
                }
            }
        }
    }

    return true;
}

std::optional<std::uint64_t> ComputeFileHash(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
        return std::nullopt;

    std::array<char, 16 * 1024> buffer{};
    std::uint64_t hash = kFnv64OffsetBasis;
    while (stream.good())
    {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = stream.gcount();
        if (count <= 0)
            break;

        for (std::streamsize i = 0; i < count; ++i)
        {
            hash ^= static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(i)]);
            hash *= kFnv64Prime;
        }
    }

    if (!stream.eof() && stream.fail())
        return std::nullopt;

    return hash;
}

bool LoadDds(
    const std::filesystem::path& path,
    TextureResource& outTexture,
    std::string& outError)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        outError = "DDS file could not be opened";
        return false;
    }

    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    if (bytes.size() < 128 || bytes[0] != 'D' || bytes[1] != 'D' || bytes[2] != 'S' || bytes[3] != ' ')
    {
        outError = "invalid DDS header";
        return false;
    }

    const int height = static_cast<int>(ReadU32(bytes, 12));
    const int width = static_cast<int>(ReadU32(bytes, 16));
    const std::uint32_t pixelFormatFlags = ReadU32(bytes, 80);
    const std::uint32_t fourCC = ReadU32(bytes, 84);
    constexpr std::uint32_t dxt1FourCC = 'D' | ('X' << 8) | ('T' << 16) | ('1' << 24);
    constexpr std::uint32_t dxt5FourCC = 'D' | ('X' << 8) | ('T' << 16) | ('5' << 24);
    if ((pixelFormatFlags & 0x4u) == 0 || (fourCC != dxt1FourCC && fourCC != dxt5FourCC))
    {
        outError = "only DDS DXT1/BC1 and DXT5/BC3 textures are supported by the lightweight loader";
        return false;
    }

    const std::uint8_t* compressedData = bytes.data() + 128;
    const std::size_t compressedSize = bytes.size() - 128;
    const std::size_t bytesPerBlock = fourCC == dxt1FourCC ? 8u : 16u;
    const std::size_t expectedSize =
        static_cast<std::size_t>((width + 3) / 4) *
        static_cast<std::size_t>((height + 3) / 4) * bytesPerBlock;
    if (compressedSize < expectedSize)
    {
        outError = "DDS compressed payload is smaller than expected";
        return false;
    }

    outTexture.textureData.width = width;
    outTexture.textureData.height = height;
    outTexture.textureData.sourceChannelCount = 4;
    outTexture.textureData.channelCount = 4;
    return fourCC == dxt1FourCC
        ? DecodeDxt1ToRgba8(compressedData, width, height, outTexture.textureData.pixels)
        : DecodeDxt5ToRgba8(compressedData, width, height, outTexture.textureData.pixels);
}
}

ResourceLoadResult<TextureResource> TextureLoader::Load(const std::string& normalizedKey, const std::filesystem::path& resolvedPath)
{
    ResourceLoadResult<TextureResource> result;

    if (normalizedKey.empty())
    {
        result.errorMessage = "texture key is empty";
        return result;
    }

    if (resolvedPath.empty() || !std::filesystem::exists(resolvedPath))
    {
        result.errorMessage = "texture asset was not found at the resolved runtime path";
        return result;
    }

    const std::optional<std::uint64_t> sourceHash = ComputeFileHash(resolvedPath);
    const std::filesystem::path binaryCachePath = BuildBinaryCachePath(resolvedPath);
    if (sourceHash.has_value() && std::filesystem::exists(binaryCachePath))
    {
        TextureResource cachedTexture;
        if (LoadTextureBinaryCache(binaryCachePath, normalizedKey, *sourceHash, cachedTexture))
        {
            Logger::Get().Info(
                "TextureLoader: loaded binary cache key=" + normalizedKey +
                " size=" + std::to_string(cachedTexture.textureData.width) + "x" +
                std::to_string(cachedTexture.textureData.height) +
                " storedChannels=" + std::to_string(cachedTexture.textureData.channelCount));

            result.success = true;
            result.data = std::move(cachedTexture);
            return result;
        }
    }

    const std::string extension = resolvedPath.extension().generic_string();
    if (extension == ".dds" || extension == ".DDS")
    {
        TextureResource texture;
        texture.name = resolvedPath.stem().string();
        texture.sourcePath = normalizedKey;
        texture.placeholder = false;
        if (!LoadDds(resolvedPath, texture, result.errorMessage))
            return result;

        Logger::Get().Info(
            "TextureLoader: loaded DDS key=" + normalizedKey +
            " size=" + std::to_string(texture.textureData.width) + "x" +
            std::to_string(texture.textureData.height) +
            " storedChannels=4");

        if (sourceHash.has_value())
            SaveTextureBinaryCache(binaryCachePath, texture, *sourceHash);
        result.success = true;
        result.data = std::move(texture);
        return result;
    }

    int width = 0;
    int height = 0;
    int sourceChannelCount = 0;

    const std::string resolvedPathUtf8 = AssetPaths::ToUtf8String(resolvedPath);
    unsigned char* pixels = stbi_load(
        resolvedPathUtf8.c_str(),
        &width,
        &height,
        &sourceChannelCount,
        4);

    if (pixels == nullptr)
    {
        result.errorMessage = stbi_failure_reason() != nullptr
            ? stbi_failure_reason()
            : "stb_image failed to decode the texture";
        return result;
    }

    TextureResource texture;
    texture.name = resolvedPath.stem().string();
    texture.sourcePath = normalizedKey;
    texture.textureData.width = width;
    texture.textureData.height = height;
    texture.textureData.sourceChannelCount = sourceChannelCount;
    texture.textureData.channelCount = 4;
    texture.placeholder = false;

    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    texture.textureData.pixels.assign(pixels, pixels + pixelCount);
    stbi_image_free(pixels);

    Logger::Get().Info(
        "TextureLoader: loaded key=" + normalizedKey +
        " size=" + std::to_string(width) + "x" + std::to_string(height) +
        " sourceChannels=" + std::to_string(sourceChannelCount) +
        " storedChannels=4");

    if (sourceHash.has_value())
        SaveTextureBinaryCache(binaryCachePath, texture, *sourceHash);
    result.success = true;
    result.data = std::move(texture);
    return result;
}

TextureResource TextureLoader::CreateDefault()
{
    TextureResource texture;
    texture.name = "DefaultTexture";
    texture.sourcePath = "defaults/texture";
    texture.textureData.width = 1;
    texture.textureData.height = 1;
    texture.textureData.sourceChannelCount = 4;
    texture.textureData.channelCount = 4;
    texture.textureData.pixels = { 255, 0, 255, 255 };
    texture.fallbackPixel = { 255, 0, 255, 255 };
    texture.placeholder = true;
    return texture;
}
