#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "EditorLayer.h"
#include "ScriptEditor.h"
#include "../core/Application.h"
#include "../core/AssetPaths.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include "../resources/ResourceManager.h"
#include "../resources/loaders/MaterialLoader.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <fstream>
#include <cwctype>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace editor {
namespace {
std::string Canonical(const std::filesystem::path& path){
    auto result=std::filesystem::weakly_canonical(path);
#ifdef _WIN32
    const auto native=result.native();
    const auto count=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,native.data(),int(native.size()),nullptr,0,nullptr,nullptr,0);
    if(count>0){std::wstring folded(count,L'\0');if(LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,native.data(),int(native.size()),folded.data(),count,nullptr,nullptr,0))result=folded;}
#endif
    return AssetPaths::ToUtf8String(result);
}
std::string ProjectIdentity(Application& app){return Canonical(app.GetProject()?app.GetProject()->descriptorPath:AssetPaths::ResolveAssetRoot());}
EditorWorkspace WorkspaceFor(DocumentType type){
    switch(type){case DocumentType::UI:return EditorWorkspace::UIDesigner;case DocumentType::Material:return EditorWorkspace::Material;case DocumentType::Lua:return EditorWorkspace::Script;default:return EditorWorkspace::LevelEditing;}
}
}
EditorDocument& EditorLayer::RegisterDocument(Application& app,DocumentType type,const std::string& key){
    const auto project=ProjectIdentity(app);
    const auto path=type==DocumentType::Scene?(key.empty()?std::filesystem::path{}:app.GetProject()?app.GetProject()->ResolveScene(key):AssetPaths::ResolveAssetPath(key)):AssetPaths::ResolveAssetPath(key);
    const auto canonical=key.empty()?std::string("untitled"):Canonical(path);
    for(auto& d:m_Documents)if(d.project==project&&d.type==type&&d.canonical==canonical)return d;
    static std::uint64_t serial=0;
    EditorDocument document;document.type=type;document.project=project;document.key=key;document.canonical=canonical;document.id="Document-"+std::to_string(++serial);
    m_DocumentProject=project;m_Documents.push_back(std::move(document));return m_Documents.back();
}
void EditorLayer::CaptureSceneDocument(Application& app){
    if(app.IsEditorPlayMode()||app.IsPrefabEditing())return;
    for(auto& d:m_Documents)if(d.type==DocumentType::Scene&&d.key==app.GetSceneKey()){
        auto p=app.GetCameraPosition();d.camera={p.x,p.y,p.z};d.yaw=app.GetCameraYaw();d.pitch=app.GetCameraPitch();d.cameraValid=true;d.gizmo=m_GizmoOperation;d.grid=m_ShowGrid;d.gameView=m_GameView;d.selection.clear();
        for(auto e:m_Selection.items)if(auto* id=app.GetWorld().GetComponent<ecs::SceneIdentityComponent>(e))d.selection.push_back(id->value);
        if(auto* id=app.GetWorld().GetComponent<ecs::SceneIdentityComponent>(m_Selection.active)){std::erase(d.selection,id->value);d.selection.push_back(id->value);}
    }
}
void EditorLayer::SyncSceneDocument(Application& app){
    if(app.IsPrefabEditing())return;
    if(m_ObservedScene!=app.GetSceneKey()||m_Documents.empty()&&m_ObservedScene=="<initial>"){
        m_ObservedScene=app.GetSceneKey();auto& d=RegisterDocument(app,DocumentType::Scene,m_ObservedScene);m_ActiveDocument=d.id;
        if(m_Workspace==EditorWorkspace::UIDesigner||m_Workspace==EditorWorkspace::Material||m_Workspace==EditorWorkspace::Script)SwitchWorkspace(EditorWorkspace::LevelEditing);
    }
}
bool EditorLayer::DocumentDirty(const EditorDocument& d)const{
    if(d.type==DocumentType::Scene)return d.key==m_ObservedScene&&(m_EditHistory?(m_EditHistory->external||m_EditHistory->revision!=m_EditHistory->saved):IsSceneDirty());
    if(d.type==DocumentType::Material)return d.key==m_EditingMaterial&&m_MaterialDirty;
    if(d.type==DocumentType::UI){for(const auto& [origin,s]:m_UIDocuments)if(s.resourceKey==d.key)return s.dirty||!m_DirtyUIStyles.empty();}
    return false;
}
bool EditorLayer::ResolveDocumentEdits(Application& app,const EditorDocument& d,OpenDecision decision){
    if(!DocumentDirty(d))return true;
    if(decision==OpenDecision::Cancel)return false;
    if(d.type==DocumentType::Scene){
        if(decision==OpenDecision::Save)return app.SaveAuthoringSceneForTransition(&m_EditorError);
        // Discard reloads the same authored world so closing a tab cannot leave a hidden dirty world.
        if(d.key.empty()){app.NewScene();return true;}
        return app.OpenScene(d.key,&m_EditorError);
    }
    if(app.IsEditorPlayMode()){m_EditorError="Stop Play before saving or discarding asset drafts.";return false;}
    if(d.type==DocumentType::UI){
        if(m_UIWidgetDrag){CancelUIDocumentEdit(app,m_UIWidgetDrag->key);m_UIWidgetDrag.reset();}
        if(!(decision==OpenDecision::Save?SaveUIDocument(app,d.key):ReloadUIDocument(app,d.key)))return false;
        // Shared styles have their own dirty source. Resolve them explicitly with this UI close.
        const auto styles=m_DirtyUIStyles;
        for(const auto& key:styles){
            if(decision==OpenDecision::Save){if(!SaveUIStyle(app,key))return false;}
            else {auto loaded=UILoader::Load(key,{});if(!loaded.success||!loaded.data.styles){m_EditorError=loaded.errorMessage;return false;}
                app.GetResourceManager()->Get<UIResource>(key)->ReplaceData(loaded.data,ResourceLoadState::Loaded,false);app.GetResourceManager()->SetUIDirty(key,false);m_UIStyles.erase(key);m_DirtyUIStyles.erase(key);}
        }
    }
    if(d.type==DocumentType::Material){
        auto* rm=app.GetResourceManager();auto resource=rm->Get<MaterialResource>(d.key);if(!resource)return false;
        if(decision==OpenDecision::Save&&!MaterialLoader::Save(d.key,resource->GetData(),&m_EditorError))return false;
        rm->SetMaterialDirty(d.key,false);
        if(decision==OpenDecision::Discard){auto loaded=rm->Reload<MaterialResource>(d.key);if(!loaded||!loaded->IsLoaded()){rm->SetMaterialDirty(d.key,true);m_EditorError="Material reload failed";return false;}}
        m_MaterialDirty=false;
    }
    return true;
}
bool EditorLayer::ActivateDocument(Application& app,const std::string& id,OpenDecision decision){
    auto it=std::find_if(m_Documents.begin(),m_Documents.end(),[&](auto& d){return d.id==id;});if(it==m_Documents.end()||it->project!=ProjectIdentity(app))return false;
    if(app.IsPrefabEditing()){m_EditorError="Return from Prefab Editor before switching documents.";return false;}
    const auto target=*it;
    if(target.type==DocumentType::Scene&&target.key!=app.GetSceneKey()){
        if(app.IsEditorPlayMode()){if(decision==OpenDecision::Cancel)return false;app.SetEditorPlayMode(false);}
        CaptureSceneDocument(app);
        for(const auto& d:m_Documents)if(d.type==DocumentType::Scene&&d.key==app.GetSceneKey()&&!ResolveDocumentEdits(app,d,decision))return false;
        if(target.key.empty())app.NewScene();else if(!app.OpenScene(target.key,&m_EditorError))return false;
        m_ObservedScene=app.GetSceneKey();
        if(target.cameraValid)app.SetEditorCameraPose({target.camera[0],target.camera[1],target.camera[2]},target.yaw,target.pitch);
        m_GizmoOperation=target.gizmo;m_ShowGrid=target.grid;m_GameView=target.gameView;m_Selection.Clear();
        for(const auto& selected:target.selection)for(auto e:app.GetWorld().Entities())if(auto* identity=app.GetWorld().GetComponent<ecs::SceneIdentityComponent>(e);identity&&identity->value==selected)m_Selection.Add(e);
    }
    if(target.type==DocumentType::Material&&m_EditingMaterial!=target.key){
        for(const auto& d:m_Documents)if(d.type==DocumentType::Material&&d.key==m_EditingMaterial&&!ResolveDocumentEdits(app,d,decision))return false;
        m_EditingMaterial=target.key;auto resource=app.GetResourceManager()->Load<MaterialResource>(target.key);if(!resource||!resource->IsLoaded()){m_EditorError="Material unavailable";return false;}
        app.GetResourceManager()->WatchForHotReload<MaterialResource>(target.key);
    }
    CaptureSceneDocument(app);CaptureCurrentWorkspace();SetWorkspaceFocused(false);
    if(target.type==DocumentType::UI){if(!OpenUIDesigner(app,target.key))return false;}
    m_ActiveDocument=id;
    const auto requested=m_WorkspacePending?m_RequestedWorkspace:m_Workspace;
    const auto workspace=target.type==DocumentType::Scene&&int(requested)<=int(EditorWorkspace::GameplayDebugging)?requested:WorkspaceFor(target.type);
    if(requested!=workspace)SwitchWorkspace(workspace);
    return true;
}
bool EditorLayer::OpenDocument(Application& app,const std::string& input){
    try{
        SyncSceneDocument(app);
        const auto encoded=std::filesystem::u8path(input).lexically_normal().generic_u8string();const std::string key(encoded.begin(),encoded.end());
        if(!ecs::ComponentRegistry::ValidateAssetKey(key,ecs::AssetKind::None)||key.empty())throw std::runtime_error("Use a portable project-relative resource key.");
        DocumentType type;const auto prefix=app.GetProject()?app.GetProject()->sceneDirectory+"/":"scenes/";
        if(key.ends_with(".ui.json"))type=DocumentType::UI;
        else if(key.ends_with(".lua"))type=DocumentType::Lua;
        else if(key.starts_with("materials/")&&(key.ends_with(".json")||key.ends_with(".material")))type=DocumentType::Material;
        else if(key.starts_with(prefix)&&key.ends_with(".json"))type=DocumentType::Scene;
        else throw std::runtime_error("Unsupported document type. Choose a scene, .ui.json, material or Lua script.");
        auto path=type==DocumentType::Scene&&app.GetProject()?app.GetProject()->ResolveScene(key):AssetPaths::ResolveAssetPath(key);
        if(path.empty()||!std::filesystem::is_regular_file(path))throw std::runtime_error("Document file is unavailable.");
        if(type==DocumentType::Lua)(void)ResolveEditableLuaScript(key);
        const auto id=RegisterDocument(app,type,key).id;
        if(ActivateDocument(app,id))return true;
        m_PendingDocument=id;m_DocumentClose=false;m_DocumentPrompt=true;return false;
    }catch(const std::exception& e){m_EditorError=e.what();return false;}
}
bool EditorLayer::CloseDocument(Application& app,const std::string& id,OpenDecision decision){
    auto it=std::find_if(m_Documents.begin(),m_Documents.end(),[&](auto& d){return d.id==id;});if(it==m_Documents.end())return false;
    if(it->type==DocumentType::Scene&&it->key==app.GetSceneKey()&&app.IsEditorPlayMode()){
        if(decision==OpenDecision::Cancel)return false;app.SetEditorPlayMode(false);
    }
    if(!ResolveDocumentEdits(app,*it,decision))return false;
    const auto index=std::size_t(it-m_Documents.begin());const auto type=it->type;const auto key=it->key;const bool active=m_ActiveDocument==id;
    if(active)SetWorkspaceFocused(false);
    if(type==DocumentType::UI&&m_DesignDocument==key){auto& view=UIDocumentSession(app,key);view.preview=m_UIPreviewSize;view.zoom=m_UIZoom;view.panX=m_UIPan.x;view.panY=m_UIPan.y;view.fit=m_UIFit;view.viewSaved=true;}
    m_Documents.erase(it);
    if(type==DocumentType::UI){std::erase(m_UIDesignerKeys,key);if(m_DesignDocument==key)m_DesignDocument.clear();}
    if(active){m_ActiveDocument.clear();
        if(!m_Documents.empty()){const auto next=m_Documents[std::min(index,m_Documents.size()-1)].id;if(!ActivateDocument(app,next)){m_PendingDocument=next;m_DocumentClose=false;m_DocumentPrompt=true;}}
    }
    StoreDocumentSession();return true;
}
bool EditorLayer::SaveActiveDocument(Application& app){
    if(app.IsPrefabEditing())return app.SaveCurrentScene(&m_EditorError);
    for(const auto& d:m_Documents)if(d.id==m_ActiveDocument){if(d.type==DocumentType::Lua)return true;if(d.type==DocumentType::Scene)return app.SaveCurrentScene(&m_EditorError);return ResolveDocumentEdits(app,d,OpenDecision::Save);}
    return app.SaveCurrentScene(&m_EditorError);
}
bool EditorLayer::CanUndoActiveDocument(bool redo)const{
    if(m_PrefabHistory)return redo?CanRedo():CanUndo()||HasPendingTransaction();
    if(ActiveDocumentIs(DocumentType::UI)){for(const auto& [origin,s]:m_UIDocuments)if(s.resourceKey==m_DesignDocument)return redo?!s.redo.empty():s.pending.has_value()||!s.undo.empty();return false;}
    return ActiveDocumentIs(DocumentType::Scene)&&(redo?CanRedo():CanUndo()||HasPendingTransaction());
}
bool EditorLayer::UndoActiveDocument(Application& app,bool redo){
    if(app.IsPrefabEditing()){if(redo)Redo(app);else Undo(app);return true;}
    if(ActiveDocumentIs(DocumentType::UI))return UndoUIDocument(app,m_DesignDocument,redo);
    if(!ActiveDocumentIs(DocumentType::Scene))return false;
    if(redo)Redo(app);else Undo(app);return true;
}
void EditorLayer::StoreDocumentSession(){
    if(m_DocumentProject.empty()||m_RestoreDocuments)return;
    auto rows=nlohmann::json::array();std::string active;
    for(const auto& d:m_Documents){
        nlohmann::json row={{"type",int(d.type)},{"key",d.key}};
        if(d.type==DocumentType::Scene)row["scene"]={{"camera",d.camera},{"yaw",d.yaw},{"pitch",d.pitch},{"valid",d.cameraValid},{"selection",d.selection},{"gizmo",d.gizmo},{"grid",d.grid},{"gameView",d.gameView}};
        if(d.type==DocumentType::UI)for(const auto& [origin,s]:m_UIDocuments)if(s.resourceKey==d.key){const bool current=m_DesignDocument==d.key;row["view"]={{"preview",current?m_UIPreviewSize:s.preview},{"zoom",current?m_UIZoom:s.zoom},{"panX",current?m_UIPan.x:s.panX},{"panY",current?m_UIPan.y:s.panY},{"selected",s.selected}};}
        rows.push_back(std::move(row));if(d.id==m_ActiveDocument)active=d.key;
    }
    m_DocumentSessions[m_DocumentProject]={{"documents",rows},{"active",active}};
}
void EditorLayer::RestoreDocumentSession(Application& app){
    m_RestoreDocuments=false;m_DocumentProject=ProjectIdentity(app);
    auto saved=m_DocumentSessions.find(m_DocumentProject);if(saved==m_DocumentSessions.end()||!saved->is_object())return;
    auto rows=saved->find("documents");if(rows==saved->end()||!rows->is_array())return;
    const auto active=saved->contains("active")&&(*saved)["active"].is_string()?(*saved)["active"].get<std::string>():std::string();std::string activate;
    // Reopening scenes registers navigation only; only the active scene may load a world.
    for(const auto& row:*rows){if(m_Documents.size()>=64)break;try{
        auto key=row.at("key").get<std::string>();int type=row.at("type").get<int>();if(type<0||type>3||key.empty()||!ecs::ComponentRegistry::ValidateAssetKey(key,ecs::AssetKind::None))continue;
        auto kind=DocumentType(type);auto path=kind==DocumentType::Scene&&app.GetProject()?app.GetProject()->ResolveScene(key):AssetPaths::ResolveAssetPath(key);if(!std::filesystem::is_regular_file(path))continue;
        if(kind==DocumentType::UI){auto& s=UIDocumentSession(app,key);if(row.contains("view")){try{const auto& v=row["view"];auto preview=v.at("preview").get<ui::Pair>();float zoom=v.at("zoom"),x=v.at("panX"),y=v.at("panY");if(std::isfinite(zoom)&&zoom>=.02f&&zoom<=4&&std::isfinite(x)&&std::isfinite(y)&&std::isfinite(preview[0])&&std::isfinite(preview[1])&&preview[0]>=64&&preview[1]>=64&&preview[0]<=8192&&preview[1]<=8192){s.preview=preview;s.zoom=zoom;s.panX=x;s.panY=y;s.viewSaved=true;s.fit=false;}auto selected=v.value("selected",s.draft.root);if(std::any_of(s.draft.widgets.begin(),s.draft.widgets.end(),[&](auto& w){return w.id==selected;}))s.selected=selected;}catch(...){}}}
        auto& document=RegisterDocument(app,kind,key);
        if(kind==DocumentType::Scene&&row.contains("scene"))try{auto& v=row["scene"];auto camera=v.at("camera").get<std::array<float,3>>();float yaw=v.at("yaw"),pitch=v.at("pitch");if(std::isfinite(yaw)&&std::isfinite(pitch)&&std::all_of(camera.begin(),camera.end(),[](float x){return std::isfinite(x);})){document.camera=camera;document.yaw=yaw;document.pitch=pitch;document.cameraValid=v.value("valid",false);}if(v.contains("selection")&&v["selection"].is_array()&&v["selection"].size()<4096)document.selection=v["selection"].get<std::vector<std::string>>();document.gizmo=std::clamp(v.value("gizmo",0),0,2);document.grid=v.value("grid",true);document.gameView=v.value("gameView",false);}catch(...){}
        auto id=document.id;if(key==active)activate=id;
    }catch(...){}}
    if(!IsSceneDirty()&&!app.IsEditorPlayMode()){
        for(const auto& d:m_Documents)if(d.type==DocumentType::Scene&&d.key==app.GetSceneKey()&&d.cameraValid){
            app.SetEditorCameraPose({d.camera[0],d.camera[1],d.camera[2]},d.yaw,d.pitch);m_GizmoOperation=d.gizmo;m_ShowGrid=d.grid;m_GameView=d.gameView;m_Selection.Clear();
            for(const auto& selected:d.selection)for(auto entity:app.GetWorld().Entities())if(auto* identity=app.GetWorld().GetComponent<ecs::SceneIdentityComponent>(entity);identity&&identity->value==selected)m_Selection.Add(entity);
        }
        if(!activate.empty())ActivateDocument(app,activate);
    }
}
void EditorLayer::DrawDocumentTabs(Application& app){
    SyncSceneDocument(app);if(m_RestoreDocuments)RestoreDocumentSession(app);
    auto* viewport=ImGui::GetMainViewport();const float row=ImGui::GetFrameHeightWithSpacing()+8;
    ImGui::SetNextWindowPos({viewport->WorkPos.x,viewport->WorkPos.y+row});ImGui::SetNextWindowSize({viewport->WorkSize.x,row});
    ImGui::Begin("Document Tabs",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
    std::string activate,close;
    if(ImGui::BeginTabBar("Documents",ImGuiTabBarFlags_FittingPolicyScroll|ImGuiTabBarFlags_NoCloseWithMiddleMouseButton)){
        for(const auto& d:m_Documents){bool open=true;auto label=(d.key.empty()?std::string("Untitled Scene"):AssetPaths::ToUtf8String(std::filesystem::u8path(d.key).filename()))+"###"+d.id;
            auto flags=(DocumentDirty(d)?ImGuiTabItemFlags_UnsavedDocument:0)|(m_ActiveDocument==d.id?ImGuiTabItemFlags_SetSelected:0)|ImGuiTabItemFlags_NoPushId;
            if(ImGui::BeginTabItem(label.c_str(),&open,flags))ImGui::EndTabItem();
            if(ImGui::IsItemClicked()&&m_ActiveDocument!=d.id)activate=d.id;
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%s",d.key.c_str(),d.canonical.c_str());
            if(!open)close=d.id;
        }ImGui::EndTabBar();
    }
    ImGui::End();
    const auto& io=ImGui::GetIO();if(!io.WantTextInput&&!app.IsViewportMouseCaptured()&&!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)&&io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_Tab,false)&&!m_Documents.empty()){
        auto it=std::find_if(m_Documents.begin(),m_Documents.end(),[&](auto& d){return d.id==m_ActiveDocument;});auto index=it==m_Documents.end()?0:int(it-m_Documents.begin());index=(index+(io.KeyShift?int(m_Documents.size())-1:1))%int(m_Documents.size());activate=m_Documents[index].id;
    }
    if(!activate.empty()&&!ActivateDocument(app,activate)){m_PendingDocument=activate;m_DocumentClose=false;m_DocumentPrompt=true;}
    if(!close.empty()&&!CloseDocument(app,close)){m_PendingDocument=close;m_DocumentClose=true;m_DocumentPrompt=true;}
    if(m_DocumentPrompt){ImGui::OpenPopup("Document transition");m_DocumentPrompt=false;}
    if(ImGui::BeginPopupModal("Document transition",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextWrapped("Resolve unsaved changes before %s. Shared UI style drafts are included.",m_DocumentClose?"closing this document":"changing the loaded scene or material");
        if(app.IsEditorPlayMode())ImGui::TextWrapped("This action stops Play and discards the Runtime World.");
        for(auto choice:{OpenDecision::Save,OpenDecision::Discard,OpenDecision::Cancel}){if(choice!=OpenDecision::Save)ImGui::SameLine();
            if(ImGui::Button(choice==OpenDecision::Save?"Save / Stop":choice==OpenDecision::Discard?"Discard / Stop":"Cancel")){
                if(choice==OpenDecision::Cancel){m_PendingDocument.clear();ImGui::CloseCurrentPopup();}
                else if(m_DocumentClose?CloseDocument(app,m_PendingDocument,choice):ActivateDocument(app,m_PendingDocument,choice)){m_PendingDocument.clear();ImGui::CloseCurrentPopup();}
            }
        }if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::EndPopup();
    }
    if(m_OpenDocumentPrompt){ImGui::OpenPopup("Open Document");m_OpenDocumentPrompt=false;}
    if(ImGui::BeginPopupModal("Open Document",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::InputText("Project-relative key",&m_OpenDocumentKey);
        if(ImGui::Button("Open")){if(OpenDocument(app,m_OpenDocumentKey)||m_DocumentPrompt)ImGui::CloseCurrentPopup();}
        ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::EndPopup();
    }
    if(m_ActiveDocument.empty()){
        ImGui::SetNextWindowDockID(ImHashStr("WhispEngine.Editor.Dockspace"),ImGuiCond_FirstUseEver);
        if(ImGui::Begin("Start Workspace")){ImGui::TextWrapped("Open a Scene, UI, Material or Lua document from File > Open Document or the Asset Browser.");if(ImGui::Button("Open Document..."))m_OpenDocumentPrompt=true;}ImGui::End();
    }
}
void EditorLayer::DrawScriptDocument(Application& app){
    auto it=std::find_if(m_Documents.begin(),m_Documents.end(),[&](auto& d){return d.id==m_ActiveDocument&&d.type==DocumentType::Lua;});if(it==m_Documents.end())return;
    PrepareWorkspacePrimaryWindow();
    if(ImGui::Begin("Script Document",nullptr,m_WorkspaceFocus?ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings:0)){
        ImGui::TextUnformatted(it->key.c_str());if(ImGui::Button("Open in Visual Studio"))OpenLuaScriptInVisualStudio(it->key,&m_EditorError);ImGui::SameLine();if(ImGui::Button("Reload file"))m_ScriptKey.clear();
        if(m_ScriptKey!=it->canonical){try{auto path=ResolveEditableLuaScript(it->key);if(std::filesystem::file_size(path)>2*1024*1024)throw std::runtime_error("Script exceeds 2 MiB preview limit; open it externally.");std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Script cannot be read");m_ScriptText.assign(std::istreambuf_iterator<char>(file),{});m_ScriptKey=it->canonical;}catch(const std::exception& e){m_ScriptText=e.what();}}
        ImGui::TextDisabled("Read-only preview. External edits keep the existing Lua hot reload workflow.");
        const auto diagnostic=app.GetLuaRuntime().Error(it->key);if(!diagnostic.empty())ImGui::TextWrapped("Lua: %s",diagnostic.c_str());
        if(ImGui::BeginChild("Source",{0,0},ImGuiChildFlags_Borders,ImGuiWindowFlags_HorizontalScrollbar))ImGui::TextUnformatted(m_ScriptText.c_str(),m_ScriptText.c_str()+m_ScriptText.size());ImGui::EndChild();
    }ImGui::End();
}
void EditorLayer::PrepareWorkspacePrimaryWindow(){
    if(!m_WorkspaceFocus)return;
    auto* viewport=ImGui::GetMainViewport();const float shell=(ImGui::GetFrameHeightWithSpacing()+8)*2;
    ImGui::SetNextWindowPos({viewport->WorkPos.x,viewport->WorkPos.y+shell});ImGui::SetNextWindowSize({viewport->WorkSize.x,std::max(1.f,viewport->WorkSize.y-shell)});
}
}
