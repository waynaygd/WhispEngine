#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

// Immutable after publication. The editor owns transitions; resource workers only read.
struct ProjectSettings {
    std::string name,startupScene;
    bool gpuInstancing=true;
    bool operator==(const ProjectSettings&)const=default;
};
struct ProjectContext {
    static constexpr int CurrentVersion = 1;
    int formatVersion = CurrentVersion;
    std::string name, assetDirectory = "Assets", sceneDirectory = "Scenes", startupScene = "Scenes/Main.json";
    std::filesystem::path descriptorPath, root;
    nlohmann::json descriptor;
    bool gpuInstancing=true;
    ProjectSettings Settings()const{return {name,startupScene,gpuInstancing};}
    static void ValidateName(const std::string&);
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
    static std::shared_ptr<const ProjectContext> WithSettings(const ProjectContext&,const ProjectSettings&);
    static std::shared_ptr<const ProjectContext> Active();
    // Application calls this only after draining resource jobs and stopping Lua.
    static void Activate(std::shared_ptr<const ProjectContext> context);
};
