#pragma once
#include <array>
#include <map>
#include <string>
namespace editor {
enum class EditorWorkspace {LevelEditing,Scripting,AssetManagement,GameplayDebugging,UIDesigner,Material,Script};
constexpr std::array<const char*,7> WorkspaceKeys={"level","scripting","assets","gameplay","ui","material","script"};
constexpr std::array<const char*,7> WorkspaceNames={"Level Editing","Scripting","Asset Management","Gameplay Debugging","UI Workspace","Material Workspace","Script Workspace"};
constexpr std::array<const char*,17> WorkspacePanels={"Scene Hierarchy","Inspector","Viewport","Asset Browser","Console / Diagnostics","Material Editor","Scene Settings","Statistics","Gameplay Events","Scene Validation","Lua Runtime Inspector","Editor Visuals","Physics Stress Test","Project Settings","Widget Hierarchy","Canvas Designer","Widget Inspector"};
struct WorkspaceLayout {
    struct ViewportPlacement {
        bool detached=false;
        float x=100,y=100,width=760,height=520;
        bool operator==(const ViewportPlacement&)const=default;
    };
    std::string ini;
    std::map<std::string,bool> visible;
    ViewportPlacement viewport;
    bool operator==(const WorkspaceLayout&)const=default;
};
// Validate native settings before handing a damaged disk record to ImGui.
// The docking tree itself is always serialized/deserialized by ImGui.
bool ValidWorkspaceIni(const std::string&);
}
