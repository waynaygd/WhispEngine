#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <optional>
#include "../render/EditorVisuals.h"
#include "EditorWorkspaces.h"
namespace editor {
struct RecentProject {std::string name,descriptor;bool operator==(const RecentProject&)const=default;};
std::filesystem::path PreferencesPath();
struct CameraBookmark {
    std::string name;
    std::array<float,3> position{};
    float yaw=0,pitch=0;
    bool operator==(const CameraBookmark&)const=default;
};
using SceneBookmarks=std::array<std::optional<CameraBookmark>,9>;
struct SelectionFilters {
    bool meshes=true,lights=true,playerStarts=true,fog=true,physics=true,other=true;
    bool operator==(const SelectionFilters&)const=default;
};
struct EditorPreferences {
    EditorWorkspace activeWorkspace=EditorWorkspace::LevelEditing;
    std::map<std::string,WorkspaceLayout> workspaces;
    bool workspacesInitialized=false;
    std::map<std::string,SceneBookmarks> bookmarks;
    SelectionFilters selectionFilters;
    bool hierarchy=true,inspector=true,viewport=true,assets=true,material=false,settings=true,console=true,diagnostics=false;
    EditorVisualSettings visuals;
    std::vector<std::string> recentScenes;
    std::vector<RecentProject> recentProjects;
    bool assetGrid=true,projectAssets=true;
    float assetCardSize=180;
    bool visualPanel=false,backgroundOverride=false,projectileTestInput=false;
    bool grid=true,lightGizmos=true,xray=false,snapping=false,rotationSnapping=false,physicsStress=false;
    bool surfaceSnap=true,alignToNormal=false;
    float surfaceOffset=0,surfaceRotationOffset=0;
    int transformPrecision=1,lightXrayMode=1;
    int operation=0,space=0;
    float translationSnap=1,rotationSnap=15,scaleSnap=.1f,cameraSpeed=1.8f,cameraSensitivity=.0025f;
    bool operator==(const EditorPreferences&)const=default;
};
nlohmann::json PreferencesToJson(const EditorPreferences&);
EditorPreferences PreferencesFromJson(const nlohmann::json&);
bool LoadPreferences(const std::filesystem::path&,EditorPreferences&,std::string* error=nullptr);
bool SavePreferences(const std::filesystem::path&,const EditorPreferences&,std::string* error=nullptr);
}
