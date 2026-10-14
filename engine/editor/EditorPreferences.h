#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include <vector>
#include "../render/EditorVisuals.h"
namespace editor {
struct EditorPreferences {
    bool hierarchy=true,inspector=true,viewport=true,assets=true,material=false,settings=true,console=true,diagnostics=false;
    EditorVisualSettings visuals;
    std::vector<std::string> recentScenes;
    bool assetGrid=false;
    bool visualPanel=false,backgroundOverride=false,projectileTestInput=false;
    bool grid=true,lightGizmos=true,xray=false,snapping=false;
    int operation=0,space=0;
    float translationSnap=1,rotationSnap=15,scaleSnap=.1f,cameraSpeed=1.8f,cameraSensitivity=.0025f;
    bool operator==(const EditorPreferences&)const=default;
};
nlohmann::json PreferencesToJson(const EditorPreferences&);
EditorPreferences PreferencesFromJson(const nlohmann::json&);
bool LoadPreferences(const std::filesystem::path&,EditorPreferences&,std::string* error=nullptr);
bool SavePreferences(const std::filesystem::path&,const EditorPreferences&,std::string* error=nullptr);
}
