#pragma once

#include "../ResourceLoader.h"
#include "../ShaderResource.h"

#include <filesystem>
#include <string>

class ShaderLoader
{
public:
    static ResourceLoadResult<ShaderResource> Load(const std::string& normalizedKey, const std::filesystem::path& resolvedPath);
    static ShaderResource CreateDefault();
};
