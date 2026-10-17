#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>
#include "../render/AntiAliasing.h"

struct ProjectSettings {
    AntiAliasingMode antiAliasing=AntiAliasingMode::Off;
    bool operator==(const ProjectSettings&)const=default;
};

// Immutable after publication. The editor owns transitions; resource workers only read.
struct ProjectContext {
    static constexpr int CurrentVersion = 1;
    int formatVersion = CurrentVersion;
    std::string name, assetDirectory = "Assets", sceneDirectory = "Scenes", startupScene = "Scenes/Main.json";
    std::filesystem::path descriptorPath, root;
    nlohmann::json descriptor;
    ProjectSettings settings;
    std::filesystem::path Resolve(const std::string& relative) const;
    std::filesystem::path AssetRoot() const;
    std::filesystem::path SceneRoot() const;
    std::filesystem::path ScriptRoot() const { return ResolveAsset("scripts"); }
    std::filesystem::path ResolveAsset(const std::string& key) const;
    std::filesystem::path ResolveScene(const std::string& key) const;
    static void ValidateRelative(const std::string& key);
    static void CheckNoLinks(const std::filesystem::path& path);
};

class ProjectManager {
public:
    // Preparation never changes the active project or process working directory.
    static std::shared_ptr<const ProjectContext> Prepare(const std::filesystem::path& descriptor);
    static std::shared_ptr<const ProjectContext> Create(const std::filesystem::path& parent, const std::string& name);
    static std::shared_ptr<const ProjectContext> WithStartupScene(const ProjectContext&, const std::string& key);
    static std::shared_ptr<const ProjectContext> WithSettings(const ProjectContext&, const std::string& startupScene,const ProjectSettings&);
    static std::shared_ptr<const ProjectContext> Active();
    // Workspace transitions drain resources/Lua first. Settings-only replacement
    // preserves every root; old immutable snapshots remain valid for readers.
    static void Activate(std::shared_ptr<const ProjectContext> context);
};
