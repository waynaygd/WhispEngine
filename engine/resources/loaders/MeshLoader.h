#pragma once

#include "../MeshResource.h"
#include "../ResourceLoader.h"

#include <filesystem>
#include <string>

class MeshLoader
{
public:
    static ResourceLoadResult<MeshResource> Load(const std::string& normalizedKey, const std::filesystem::path& resolvedPath);
    static MeshResource CreateDefault();
};
