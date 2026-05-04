#pragma once

#include "../ResourceLoader.h"
#include "../TextureResource.h"

#include <filesystem>
#include <string>

class TextureLoader
{
public:
    static ResourceLoadResult<TextureResource> Load(const std::string& normalizedKey, const std::filesystem::path& resolvedPath);
    static TextureResource CreateDefault();
};
