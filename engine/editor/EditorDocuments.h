#pragma once
#include <string>
#include <array>
#include <vector>
namespace editor {
enum class DocumentType {Scene, UI, Material, Lua, Prefab, ProjectSettings};
struct EditorDocument {
    DocumentType type=DocumentType::Scene;
    std::string project,key,canonical,id;
    std::string returnDocument;
    // Only stable authored identities survive a scene reload; never ECS/resource handles.
    std::vector<std::string> selection;
    std::array<float,3> camera{};
    float yaw=0,pitch=0;
    int gizmo=0;
    bool cameraValid=false;
    bool grid=true,gameView=false;
};
}
