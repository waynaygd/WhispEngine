#pragma once

#include <filesystem>
#include <string>

namespace AssetPaths
{
std::filesystem::path GetExecutableDirectory();
std::filesystem::path ResolveAssetRoot();
std::filesystem::path ResolveShaderRoot();
std::string NormalizeAssetKey(const std::filesystem::path& path);
std::string NormalizeShaderKey(const std::filesystem::path& path);
std::filesystem::path ResolveAssetPath(const std::filesystem::path& relativePath);
std::filesystem::path ResolveAssetOutputPath(const std::filesystem::path& relativePath);
std::filesystem::path ResolveShaderPath(const std::filesystem::path& relativePath);
}
