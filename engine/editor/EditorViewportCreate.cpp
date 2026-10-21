#include "EditorLayer.h"
#include "../core/Application.h"
#include <imgui.h>
namespace editor {
bool EditorLayer::DrawViewportCreateMenu(Application& app){
    const bool play=app.IsEditorPlayMode();ImGui::BeginDisabled(play);
    if(ImGui::Button("Create"))ImGui::OpenPopup("CreateEntity");
    const bool hovered=ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    if(play&&hovered)ImGui::SetTooltip("Stop Play before creating editor entities.");
    bool open=ImGui::IsPopupOpen("CreateEntity");
    if(ImGui::BeginPopup("CreateEntity")){
        if(play)ImGui::CloseCurrentPopup();
        if(ImGui::BeginMenu("3D Object")){for(const char* type:{"cube","sphere","cylinder","cone","plane"}){std::string label=type;label[0]=char(std::toupper(label[0]));if(ImGui::MenuItem(label.c_str()))CreatePrimitive(app,type);}ImGui::EndMenu();}
        if(ImGui::MenuItem("Empty Entity"))CreateEntity(app);
        if(ImGui::BeginMenu("Player")){if(ImGui::MenuItem("Player Start"))CreatePlayerStart(app);ImGui::EndMenu();}
        if(ImGui::BeginMenu("Light")){if(ImGui::MenuItem("Directional"))CreateLight(app,LightType::Directional);if(ImGui::MenuItem("Point"))CreateLight(app,LightType::Point);if(ImGui::MenuItem("Spot"))CreateLight(app,LightType::Spot);ImGui::EndMenu();}
        if(ImGui::BeginMenu("UI")){if(ImGui::MenuItem("Canvas"))CreateUICanvas(app);ImGui::EndMenu();}
        if(ImGui::BeginMenu("Environment")){if(ImGui::MenuItem("Fog"))CreateFog(app);if(ImGui::MenuItem("Global Post Process Volume"))CreatePostProcessVolume(app,true);if(ImGui::MenuItem("Local Post Process Volume"))CreatePostProcessVolume(app,false);ImGui::EndMenu();}
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();return hovered||open;
}
}
