#include "EditorLayer.h"
#include "../core/Application.h"
#include "../core/AssetPaths.h"
#include "../ecs/components/PlayerStartComponent.h"
#include "../ecs/components/FogComponent.h"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cctype>
namespace editor {
std::string EditorLayer::BookmarkSceneKey(const Application& app)const{
    if(app.GetSceneKey().empty())return {};
    try{
        const auto path=app.GetProject()?app.GetProject()->ResolveScene(app.GetSceneKey()):AssetPaths::ResolveAssetOutputPath(app.GetSceneKey());
        return AssetPaths::ToUtf8String(std::filesystem::absolute(path).lexically_normal());
    }catch(...){return {};}
}
const SceneBookmarks* EditorLayer::GetSceneBookmarks(const Application& app)const{
    auto it=m_Bookmarks.find(BookmarkSceneKey(app));return it==m_Bookmarks.end()?nullptr:&it->second;
}
bool EditorLayer::SaveBookmark(Application& app,int slot){
    if(app.IsEditorPlayMode()||slot<0||slot>=9||HasPendingTransaction()||app.IsCameraControlActive())return false;
    auto key=BookmarkSceneKey(app);if(key.empty()){m_EditorError="Save the scene before adding camera bookmarks.";return false;}
    auto& b=m_Bookmarks[key][slot];auto p=app.GetCameraPosition();
    b=CameraBookmark{b?b->name:"View "+std::to_string(slot+1),{p.x,p.y,p.z},app.GetCameraYaw(),app.GetCameraPitch()};
    PersistPreferences(app);return true;
}
bool EditorLayer::GoToBookmark(Application& app,int slot){
    if(app.IsEditorPlayMode()||slot<0||slot>=9||HasPendingTransaction()||app.IsCameraControlActive())return false;
    const auto* slots=GetSceneBookmarks(app);if(!slots||!(*slots)[slot])return false;const auto& b=*(*slots)[slot];
    return app.SetEditorCameraPose({b.position[0],b.position[1],b.position[2]},b.yaw,b.pitch);
}
bool EditorLayer::RenameBookmark(Application& app,int slot,const std::string& name){
    if(app.IsEditorPlayMode()||slot<0||slot>=9||name.empty()||name.size()>128||std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isspace(c);})||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;}))return false;
    auto it=m_Bookmarks.find(BookmarkSceneKey(app));if(it==m_Bookmarks.end()||!it->second[slot])return false;
    it->second[slot]->name=name;PersistPreferences(app);return true;
}
bool EditorLayer::DeleteBookmark(Application& app,int slot){
    if(app.IsEditorPlayMode()||slot<0||slot>=9)return false;
    auto it=m_Bookmarks.find(BookmarkSceneKey(app));if(it==m_Bookmarks.end()||!it->second[slot])return false;
    it->second[slot].reset();if(std::none_of(it->second.begin(),it->second.end(),[](const auto& b){return b.has_value();}))m_Bookmarks.erase(it);
    PersistPreferences(app);return true;
}
bool EditorLayer::IsolateSelected(Application& app){
    if(app.IsEditorPlayMode()||HasPendingTransaction())return false;PruneSelection(app);
    if(m_Selection.items.empty())return false;m_Isolation=m_Selection.items;return true;
}
bool EditorLayer::EditorEntityVisible(const Application& app,ecs::Entity entity)const{
    return app.IsEditorPlayMode()||m_Isolation.empty()||std::find(m_Isolation.begin(),m_Isolation.end(),entity)!=m_Isolation.end();
}
bool EditorLayer::CanViewportSelect(const Application& app,ecs::Entity entity)const{
    const auto& world=app.GetWorld();if(!world.IsAlive(entity)||!EditorEntityVisible(app,entity))return false;
    const auto& f=m_SelectionFilters;bool known=false;
    auto category=[&](bool present,bool enabled){known|=present;return !present||enabled;};
    if(!category(world.HasComponent<ecs::MeshRendererComponent>(entity),f.meshes))return false;
    if(!category(world.HasComponent<ecs::DirectionalLightComponent>(entity)||world.HasComponent<ecs::PointLightComponent>(entity)||world.HasComponent<ecs::SpotLightComponent>(entity),f.lights))return false;
    if(!category(world.HasComponent<ecs::PlayerStartComponent>(entity),f.playerStarts))return false;
    if(!category(world.HasComponent<ecs::FogComponent>(entity),f.fog))return false;
    if(!category(world.HasComponent<ecs::ColliderComponent>(entity)||world.HasComponent<ecs::RigidbodyComponent>(entity),f.physics))return false;
    return known||f.other;
}
bool EditorLayer::DrawViewportTools(Application& app){
    const bool isolation=IsIsolationActive();
    if(isolation)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.48f,.29f,.07f,1));
    if(ImGui::Button(isolation?"View [ISOLATED]":"View"))ImGui::OpenPopup("ViewportTools");
    if(isolation)ImGui::PopStyleColor();
    bool owns=ImGui::IsItemHovered()||ImGui::IsPopupOpen("ViewportTools");
    if(isolation){ImGui::SameLine();if(ImGui::SmallButton("Exit Isolation"))ExitIsolation();owns|=ImGui::IsItemHovered();}
    if(ImGui::BeginPopup("ViewportTools")){
        ImGui::BeginDisabled(app.IsEditorPlayMode()||HasPendingTransaction());
        if(ImGui::BeginMenu("Bookmarks")){
            const bool saved=!BookmarkSceneKey(app).empty();
            if(!saved)ImGui::TextDisabled("Save the scene to enable bookmarks.");
            ImGui::TextDisabled("Ctrl+1..9: go; Ctrl+Shift+1..9: save");
            ImGui::BeginDisabled(!saved);
            for(int i=0;i<9;++i){const auto* slots=GetSceneBookmarks(app);const auto slot=slots?(*slots)[i]:std::nullopt;
                const auto label=std::to_string(i+1)+". "+(slot?slot->name:"(empty)")+"###Bookmark"+std::to_string(i);
                if(ImGui::BeginMenu(label.c_str())){
                    ImGui::BeginDisabled(!slot);if(ImGui::MenuItem("Go to Bookmark"))GoToBookmark(app,i);ImGui::EndDisabled();
                    if(ImGui::MenuItem(slot?"Replace with Current View":"Save Current View"))SaveBookmark(app,i);
                    if(slot){ImGui::PushID(i);auto name=slot->name;ImGui::SetNextItemWidth(220);
                        if(ImGui::InputText("Name",&name,ImGuiInputTextFlags_EnterReturnsTrue)&&!RenameBookmark(app,i,name))m_EditorError="Bookmark name must contain 1-128 UTF-8 bytes without control characters.";
                        if(ImGui::IsItemHovered())ImGui::SetTooltip("Press Enter to save the name.");
                        if(ImGui::MenuItem("Delete Bookmark"))DeleteBookmark(app,i);ImGui::PopID();}
                    ImGui::EndMenu();
                }
            }ImGui::EndDisabled();ImGui::EndMenu();
        }
        ImGui::BeginDisabled(m_Selection.items.empty());if(ImGui::MenuItem("Isolate Selected"))IsolateSelected(app);ImGui::EndDisabled();
        ImGui::EndDisabled();
        if(ImGui::MenuItem("Exit Isolation",nullptr,false,isolation))ExitIsolation();
        if(isolation){ImGui::TextDisabled("Selection changes keep the isolated set.");ImGui::TextDisabled("New objects created here are included; lighting stays active.");}
        if(ImGui::BeginMenu("Selection Filters")){
            bool changed=false;auto& f=m_SelectionFilters;
            if(ImGui::MenuItem("Enable All")){f={};changed=true;}
            ImGui::Separator();changed|=ImGui::Checkbox("Mesh Objects",&f.meshes);changed|=ImGui::Checkbox("Lights",&f.lights);
            changed|=ImGui::Checkbox("Player Starts",&f.playerStarts);changed|=ImGui::Checkbox("Fog",&f.fog);
            changed|=ImGui::Checkbox("Physics / Triggers",&f.physics);changed|=ImGui::Checkbox("Empty / Other Entities",&f.other);
            ImGui::TextDisabled("Viewport only. Hierarchy and Gizmo stay available.");
            ImGui::TextDisabled("All categories present on an entity must be enabled.");
            if(changed)PersistPreferences(app);ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }return owns;
}
void EditorLayer::HandleViewportToolShortcuts(Application& app){
    const auto& io=ImGui::GetIO();
    if(app.IsEditorPlayMode()||!OwnsViewportKeyboard()||m_InputTextActive||m_ToolActive||app.IsCameraControlActive()||HasPendingTransaction()||io.KeyAlt)return;
    if(io.KeyCtrl)for(int i=0;i<9;++i)if(ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_1+i),false)){
        if(io.KeyShift)SaveBookmark(app,i);else GoToBookmark(app,i);
    }
}
}
