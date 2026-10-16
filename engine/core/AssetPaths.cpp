#include "AssetPaths.h"
#include "ProjectContext.h"

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

    const auto utf8 = normalized.generic_u8string();
    const std::string key(utf8.begin(),utf8.end());
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
    if(auto project=ProjectManager::Active())return project->AssetRoot();
    return ResolveCandidatePaths({std::filesystem::path("assets"),std::filesystem::path("engine")/"assets"});
}

std::filesystem::path ResolveEngineAssetRoot()
{
    // Built-ins belong to the editor installation even when the process was
    // launched with a game's Assets directory in its working-directory chain.
    std::vector<fs::path> roots;AppendRootChain(roots,GetExecutableDirectory());
    for(const auto& root:roots)for(const auto& suffix:{fs::path("assets"),fs::path("engine")/"assets"}){
        auto resolved=NormalizeIfExists(root/suffix);if(!resolved.empty())return resolved;
    }
    return ResolveCandidatePaths(
        {
            std::filesystem::path("assets"),
            std::filesystem::path("engine") / "assets"
        });
}

std::filesystem::path ResolveShaderRoot()
{
    if(ProjectManager::Active()){
        std::vector<fs::path> roots;AppendRootChain(roots,GetExecutableDirectory());
        for(const auto& root:roots)for(const auto& suffix:{fs::path("shaders"),fs::path("engine")/"shaders"}){
            auto resolved=NormalizeIfExists(root/suffix);if(!resolved.empty())return resolved;
        }
    }
    return ResolveCandidatePaths(
        {
            std::filesystem::path("shaders"),
            std::filesystem::path("engine") / "shaders"
        });
}

std::string NormalizeAssetKey(const std::filesystem::path& path)
{
    if (path.empty())
        return {};

    const auto raw=ToUtf8String(path);
    if(raw.starts_with("@engine/")) {
        try {ProjectContext::ValidateRelative(raw.substr(8));return raw;}catch(...){return {};}
    }

    if(auto project=ProjectManager::Active()) {
        try {
            auto key=path.is_absolute()?NormalizeKeyAgainstRoot(path,project->AssetRoot()):ToUtf8String(path);
            // Embedded texture suffixes are runtime identifiers; validate their file part.
            project->ResolveAsset(key.substr(0,key.find("#embedded/")));
            return key;
        }catch(...){return {};}
    }

    if (!path.is_absolute())
        return NormalizeRelativeKey(path);

    auto key=NormalizeKeyAgainstRoot(path,ResolveAssetRoot());if(!key.empty())return key;
    // Authoring assets can live in engine/assets while the executable uses staged assets.
    for(const auto& root:BuildSearchRoots())for(const auto& candidate:{root/"assets",root/"engine"/"assets"}){
        if(!std::filesystem::is_directory(candidate))continue;key=NormalizeKeyAgainstRoot(path,NormalizeIfExists(candidate));if(!key.empty())return key;
    }
    return {};
}

std::string NormalizeShaderKey(const std::filesystem::path& path)
{
    if (path.empty())
        return {};

    if (!path.is_absolute())
        return NormalizeRelativeKey(path);

    return NormalizeKeyAgainstRoot(path, ResolveShaderRoot());
}

std::filesystem::path ResolveAssetPath(const std::filesystem::path& relativePath)
{
    const auto raw=ToUtf8String(relativePath);
    if(raw.starts_with("@engine/")) {
        try{const auto key=raw.substr(8);ProjectContext::ValidateRelative(key);
            const auto path=ResolveEngineAssetRoot()/std::filesystem::u8path(key);ProjectContext::CheckNoLinks(path);
            return std::filesystem::is_regular_file(path)?path:std::filesystem::path{};
        }catch(...){return {};}
    }
    if(auto project=ProjectManager::Active()) {
        try {auto key=NormalizeAssetKey(relativePath);if(key.empty())return {};auto path=project->ResolveAsset(key);return std::filesystem::is_regular_file(path)?path:std::filesystem::path{};}
        catch(...){return {};}
    }
    const std::string key = NormalizeAssetKey(relativePath);
    if (key.empty())
        return {};

    const std::filesystem::path assetRoot = ResolveAssetRoot();
    if (assetRoot.empty())
        return {};

    auto resolved=NormalizeIfExists(assetRoot/std::filesystem::u8path(key));if(!resolved.empty())return resolved;
    for(const auto& root:BuildSearchRoots())for(const auto& candidate:{root/"assets",root/"engine"/"assets"}){
        resolved=NormalizeIfExists(candidate/std::filesystem::u8path(key));if(!resolved.empty())return resolved;
    }
    return {};
}

std::filesystem::path ResolveAssetOutputPath(const std::filesystem::path& relativePath)
{
    if(ToUtf8String(relativePath).starts_with("@engine/"))return {};
    if(auto project=ProjectManager::Active()) {
        try {auto key=NormalizeAssetKey(relativePath);return key.empty()?std::filesystem::path{}:project->ResolveAsset(key);}
        catch(...){return {};}
    }
    const std::string key = NormalizeAssetKey(relativePath);
    if (key.empty())
        return {};

    const std::filesystem::path assetRoot = ResolveAssetRoot();
    if (assetRoot.empty())
        return {};

    return (assetRoot / std::filesystem::u8path(key)).lexically_normal();
}

std::filesystem::path ResolveShaderPath(const std::filesystem::path& relativePath)
{
    const std::string key = NormalizeShaderKey(relativePath);
    if (key.empty())
        return {};

    const std::filesystem::path shaderRoot = ResolveShaderRoot();
    if (shaderRoot.empty())
        return {};

    return NormalizeIfExists(shaderRoot / std::filesystem::u8path(key));
}

std::string ToUtf8String(const std::filesystem::path& path)
{
#if defined(_WIN32)
    const std::wstring wide = path.native();
    if (wide.empty())
        return {};

    const int requiredSize = WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.c_str(),
        static_cast<int>(wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (requiredSize <= 0)
        return {};

    std::string utf8(static_cast<std::size_t>(requiredSize), '\0');
    const int convertedSize = WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.c_str(),
        static_cast<int>(wide.size()),
        utf8.data(),
        requiredSize,
        nullptr,
        nullptr);
    if (convertedSize <= 0)
        return {};

    return utf8;
#else
    return path.string();
#endif
}
}
