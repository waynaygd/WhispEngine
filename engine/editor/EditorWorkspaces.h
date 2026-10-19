#pragma once
#include <array>
#include <map>
#include <string>
namespace editor {
enum class EditorWorkspace {LevelEditing,Scripting,AssetManagement,GameplayDebugging};
constexpr std::array<const char*,4> WorkspaceKeys={"level","scripting","assets","gameplay"};
constexpr std::array<const char*,4> WorkspaceNames={"Level Editing","Scripting","Asset Management","Gameplay Debugging"};
constexpr std::array<const char*,14> WorkspacePanels={"Scene Hierarchy","Inspector","Viewport","Asset Browser","Console / Diagnostics","Material Editor","Scene Settings","Statistics","Gameplay Events","Scene Validation","Lua Runtime Inspector","Editor Visuals","Physics Stress Test","Project Settings"};
struct WorkspaceLayout {
    std::string ini;
    std::map<std::string,bool> visible;
    bool operator==(const WorkspaceLayout&)const=default;
};
// Validate native settings before handing a damaged disk record to ImGui.
// The docking tree itself is always serialized/deserialized by ImGui.
bool ValidWorkspaceIni(const std::string&);
}
