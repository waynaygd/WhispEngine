#pragma once

#include "../MaterialResource.h"
#include "../ResourceLoader.h"

#include <filesystem>
#include <string>

class MaterialLoader
{
public:
    static ResourceLoadResult<MaterialResource> Load(const std::string& normalizedKey, const std::filesystem::path& resolvedPath);
    static MaterialResource CreateDefault();
};
