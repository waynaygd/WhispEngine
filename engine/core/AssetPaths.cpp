#include "AssetPaths.h"

#include <algorithm>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace
{
namespace fs = std::filesystem;

void AppendRootChain(std::vector<fs::path>& roots, const fs::path& start)
{
    if (start.empty())
        return;

    fs::path current = start;
    while (!current.empty())
    {
        if (std::find(roots.begin(), roots.end(), current) == roots.end())
            roots.push_back(current);

        if (!current.has_parent_path() || current == current.parent_path())
            break;

        current = current.parent_path();
    }
}

std::vector<fs::path> BuildSearchRoots()
{
    std::vector<fs::path> roots;
    AppendRootChain(roots, fs::current_path());
    AppendRootChain(roots, AssetPaths::GetExecutableDirectory());
    return roots;
}

fs::path NormalizeIfExists(const fs::path& path)
{
    if (!fs::exists(path))
        return {};

    try
    {
        return fs::weakly_canonical(path);
    }
    catch (...)
    {
        return fs::absolute(path);
    }
}

fs::path ResolveCandidatePaths(const std::vector<fs::path>& candidates)
{
    const std::vector<fs::path> roots = BuildSearchRoots();
    for (const fs::path& root : roots)
    {
        for (const fs::path& candidate : candidates)
        {
            const fs::path resolved = NormalizeIfExists(root / candidate);
            if (!resolved.empty())
                return resolved;
        }
    }

    return {};
}

std::string NormalizeRelativeKey(const fs::path& path)
{
    if (path.empty())
        return {};

    const fs::path normalized = path.lexically_normal();
    if (normalized.empty() || normalized == ".")
        return {};

    if (normalized.is_absolute())
        return {};

    const std::string key = normalized.generic_string();
    if (key.empty() || key == "..")
        return {};

    if (key.rfind("../", 0) == 0)
        return {};

    return key;
}

std::string NormalizeKeyAgainstRoot(const fs::path& path, const fs::path& root)
{
    if (path.empty())
        return {};

    if (!path.is_absolute())
        return NormalizeRelativeKey(path);

    if (root.empty())
        return {};

    try
    {
        const fs::path relative = fs::relative(path.lexically_normal(), root);
        return NormalizeRelativeKey(relative);
    }
    catch (...)
    {
        return {};
    }
}
}

namespace AssetPaths
{
std::filesystem::path GetExecutableDirectory()
{
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        return {};

    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

std::filesystem::path ResolveAssetRoot()
{
    return ResolveCandidatePaths(
        {
            std::filesystem::path("assets"),
            std::filesystem::path("engine") / "assets"
        });
}

std::filesystem::path ResolveShaderRoot()
{
    return ResolveCandidatePaths(
        {
            std::filesystem::path("shaders"),
            std::filesystem::path("engine") / "shaders"
        });
}

std::string NormalizeAssetKey(const std::filesystem::path& path)
{
    return NormalizeKeyAgainstRoot(path, ResolveAssetRoot());
}

std::string NormalizeShaderKey(const std::filesystem::path& path)
{
    return NormalizeKeyAgainstRoot(path, ResolveShaderRoot());
}

std::filesystem::path ResolveAssetPath(const std::filesystem::path& relativePath)
{
    const std::string key = NormalizeAssetKey(relativePath);
    if (key.empty())
        return {};

    const std::filesystem::path assetRoot = ResolveAssetRoot();
    if (assetRoot.empty())
        return {};

    return NormalizeIfExists(assetRoot / std::filesystem::path(key));
}

std::filesystem::path ResolveAssetOutputPath(const std::filesystem::path& relativePath)
{
    const std::string key = NormalizeAssetKey(relativePath);
    if (key.empty())
        return {};

    const std::filesystem::path assetRoot = ResolveAssetRoot();
    if (assetRoot.empty())
        return {};

    return (assetRoot / std::filesystem::path(key)).lexically_normal();
}

std::filesystem::path ResolveShaderPath(const std::filesystem::path& relativePath)
{
    const std::string key = NormalizeShaderKey(relativePath);
    if (key.empty())
        return {};

    const std::filesystem::path shaderRoot = ResolveShaderRoot();
    if (shaderRoot.empty())
        return {};

    return NormalizeIfExists(shaderRoot / std::filesystem::path(key));
}
}
