#include "EditorLayer.h"
#include "../core/Application.h"
#include "../scripting/Prefab.h"
#include "../ecs/components/PrefabInstanceComponent.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include "../scene/SceneSerializer.h"
#include <fstream>
#include <unordered_set>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
namespace editor {
namespace {
using Json=nlohmann::json;
Json DiskPrefab(const std::string& key){std::ifstream f(scripting::SafeAsset(key,".prefab.json"));Json j;f>>j;return j;}
void AuthoringOnly(Application& app){if(app.IsEditorPlayMode()||app.IsPrefabEditing())throw std::runtime_error("Return to the scene and stop Play before managing instances");}
Json UpdatedNodes(ecs::World& world,const std::string& key,const Json& data){
    Json nodes=Json::array();for(auto e:world.Entities())if(auto* link=world.GetComponent<ecs::PrefabInstanceComponent>(e);link&&link->asset==key){auto part=scripting::InstanceSceneNodes(world,e,data);for(auto& n:part)nodes.push_back(std::move(n));}return nodes;
}
void ValidateNodes(ecs::World& world,const SceneSettings& settings,const Json& nodes){
    auto document=SceneSerializer::CaptureWorld(world,settings);
    std::unordered_set<std::string> removed;
    for(const auto& n:nodes){auto id=n.at("id").get<std::string>();auto& list=document["entities"];
        auto found=std::find_if(list.begin(),list.end(),[&](const auto& old){return old.at("id")==id;});
        if(n.value("removed",false)){if(found!=list.end())list.erase(found);removed.insert(id);}
        else if(found!=list.end())*found=n;else list.push_back(n);
    }
    auto references=[&](auto&& self,const Json& value)->void{
        if(value.is_object()&&value.size()==1&&value.contains("$entity")&&removed.contains(value.at("$entity").get<std::string>()))throw std::runtime_error("A scene entity references a prefab child removed by this edit; clear that reference first");
        if(value.is_structured())for(const auto& v:value)self(self,v);
    };references(references,document);
    ecs::World check;SceneSettings staged;std::string error;if(!SceneSerializer::RestoreWorld(document,check,staged,&error))throw std::runtime_error(error);
}
}
bool EditorLayer::CreatePrefab(Application& app,const std::string& key){
    try{
        AuthoringOnly(app);FinishTransaction(app);if(m_Selection.items.size()!=1)throw std::runtime_error("Select exactly one root and its subtree");
        if(!key.starts_with("prefabs/"))throw std::runtime_error("Choose a .prefab.json path inside Assets/prefabs");
        auto root=m_Selection.active;if(scripting::PrefabRoot(app.GetWorld(),root).IsValid())throw std::runtime_error("Selection already belongs to a prefab instance");
        auto data=scripting::CapturePrefab(app.GetWorld(),root);auto before=CaptureSubtree(app,root);
        before.files.push_back({key,std::nullopt,data});auto path=scripting::SafeAsset(key,".prefab.json");
        if(!SceneSerializer::WriteAtomic(path,data,&m_EditorError,false))throw std::runtime_error(m_EditorError);
        try{
            for(auto e:app.GetWorld().Subtree(root))if(!app.GetWorld().HasComponent<ecs::SceneIdentityComponent>(e)){auto id=SceneSerializer::EntitySceneId(app.GetWorld(),e);app.GetWorld().AddComponent<ecs::SceneIdentityComponent>(e).value=id;}
            scripting::LinkPrefab(app.GetWorld(),root,key,data);
        }catch(...){std::filesystem::remove(path);RestoreSnapshot(app,before);throw;}
        CommitUndo(app,"Create Prefab (file and link)",std::move(before));m_SelectedAsset=key;m_SelectedAssetKind="Prefab";m_AssetListsDirty=true;m_EditorError.clear();return true;
    }catch(const std::exception& e){m_EditorError=e.what();return false;}
}
ecs::Entity EditorLayer::InstantiatePrefab(Application& app,const std::string& key,ecs::Vec3 position,const ecs::TransformComponent* placement,const nlohmann::json* prepared){
    ecs::Entity created;
    try{
        AuthoringOnly(app);FinishTransaction(app);
        auto e=prepared?scripting::InstantiatePrefab(app.GetWorld(),key,*prepared,position):scripting::SpawnPrefab(app.GetWorld(),key,position);created=e;
        if(placement&&!app.GetWorld().SetLocalTransform(e,*placement,&m_EditorError))throw std::runtime_error(m_EditorError);
        auto before=CaptureSubtree(app,e);before.alive=false;before.components=Json::object();before.parent.reset();
        for(auto& child:before.related){child.alive=false;child.components=Json::object();child.parent.reset();}
        if(IsIsolationActive()){auto subtree=app.GetWorld().Subtree(e);m_Isolation.insert(m_Isolation.end(),subtree.begin(),subtree.end());}
        SelectEntityFromUser(app,e);app.NotifyEditorMutation(e);CommitUndo(app,"Instantiate Prefab",std::move(before));m_EditorError.clear();return e;
    }catch(const std::exception& e){if(app.GetWorld().IsAlive(created))app.DestroySceneEntity(created);m_EditorError=e.what();return {};}
}
EditorLayer::EntitySnapshot EditorLayer::PrefabNodesSnapshot(Application& app,const Json& nodes){
    EntitySnapshot result;result.world=app.GetWorld().GetIdentity();result.selection=m_Selection;result.prefabBatch=true;
    std::unordered_map<std::string,ecs::Entity> ids;for(auto e:app.GetWorld().Entities())ids.emplace(SceneSerializer::EntitySceneId(app.GetWorld(),e),e);
    for(const auto& node:nodes)if(!node.value("removed",false)){
        auto id=node.at("id").get<std::string>();if(!ids.contains(id))ids.emplace(id,app.GetWorld().CreateEntity());
    }
    for(const auto& node:nodes){
        auto id=node.at("id").get<std::string>();auto e=ids.at(id);auto s=CaptureSelectedEntity(app,e);s.selection.reset();
        if(node.value("removed",false)){s.alive=false;s.components=Json::object();s.parent.reset();}
        else {s.components=node.at("components");s.components["SceneIdentity"]={{"value",id}};s.parent=node.contains("parent")?ids.at(node.at("parent").get<std::string>()):ecs::Entity{};}
        result.related.push_back(std::move(s));
    }return result;
}
bool EditorLayer::WritePrefabAndUpdate(Application& app,const std::string& key,const Json& data,const Json& expected,bool update){
    EntitySnapshot desired;
    try{
        AuthoringOnly(app);scripting::ValidatePrefab(data);FinishTransaction(app);
        if(DiskPrefab(key)!=expected)throw std::runtime_error("Prefab changed on disk; reload before applying changes");
        auto nodes=update?UpdatedNodes(app.GetWorld(),key,data):Json::array();ValidateNodes(app.GetWorld(),app.GetSceneSettings(),nodes);
        desired=PrefabNodesSnapshot(app,nodes);auto before=CaptureLike(app,desired);
        for(auto& item:before.related)if(item.components.empty()){item.alive=false;item.parent.reset();}
        if(data!=expected&&!data.value("_legacySceneReferences",false)){desired.files.push_back({key,data,expected});before.files.push_back({key,expected,data});}
        RestoreSnapshot(app,desired);CommitUndo(app,update?"Save Prefab and Update Instances":"Save Prefab",std::move(before));m_AssetListsDirty=true;m_EditorError.clear();return true;
    }catch(const std::exception& e){for(const auto& item:desired.related)if(app.GetWorld().IsAlive(item.entity)&&!app.GetWorld().HasComponent<ecs::SceneIdentityComponent>(item.entity))app.GetWorld().DestroyEntity(item.entity);m_EditorError=e.what();return false;}
}
bool EditorLayer::ApplyPrefab(Application& app,ecs::Entity member){
    try{
        AuthoringOnly(app);FinishTransaction(app);auto root=scripting::PrefabRoot(app.GetWorld(),member);if(!root.IsValid())throw std::runtime_error("Select a prefab instance");
        auto key=app.GetWorld().GetComponent<ecs::PrefabInstanceComponent>(root)->asset;auto source=scripting::ReadPrefab(key);auto data=scripting::CapturePrefab(app.GetWorld(),root);
        std::unordered_set<std::string> sourceIds,currentIds;
        for(const auto& n:source.at("entities"))sourceIds.insert(n.at("id").get<std::string>());
        for(const auto& n:data.at("entities"))currentIds.insert(n.at("id").get<std::string>());
        if(sourceIds!=currentIds)throw std::runtime_error("Prefab topology differs from its source; Update Instances before Apply, or undo local child additions/deletions");
        // Root placement belongs to the scene; Apply never writes it to the template.
        for(auto& n:data["entities"])if(n.at("id")==data.at("root"))for(const auto& original:source.at("entities"))if(original.at("id")==source.at("root")){
            ecs::World staging;auto entity=staging.CreateEntity();ecs::ComponentRegistry::Builtins().Restore(staging,entity,{{"Transform",original.at("components").at("Transform")}});
            const auto pose=ecs::RebasePivot(staging.LocalTransform(entity),app.GetWorld().LocalTransform(root).pivot);staging.SetLocalTransform(entity,pose);n["components"]["Transform"]=ecs::ComponentRegistry::Builtins().Capture(staging,entity).at("Transform");
        }
        (void)scripting::InstanceSceneNodes(app.GetWorld(),root,source);return WritePrefabAndUpdate(app,key,data,DiskPrefab(key),true);
    }catch(const std::exception& e){m_EditorError=e.what();return false;}
}
bool EditorLayer::RevertPrefab(Application& app,ecs::Entity member){
    try{
        AuthoringOnly(app);FinishTransaction(app);auto root=scripting::PrefabRoot(app.GetWorld(),member);if(!root.IsValid())throw std::runtime_error("Select a prefab instance");
        auto source=scripting::ReadPrefab(app.GetWorld().GetComponent<ecs::PrefabInstanceComponent>(root)->asset);auto nodes=scripting::InstanceSceneNodes(app.GetWorld(),root,source);
        ValidateNodes(app.GetWorld(),app.GetSceneSettings(),nodes);auto desired=PrefabNodesSnapshot(app,nodes);auto before=CaptureLike(app,desired);
        for(auto& item:before.related)if(item.components.empty()){item.alive=false;item.parent.reset();}
        RestoreSnapshot(app,desired);CommitUndo(app,"Revert Prefab Instance",std::move(before));m_EditorError.clear();return true;
    }catch(const std::exception& e){m_EditorError=e.what();return false;}
}
bool EditorLayer::UpdatePrefabInstances(Application& app,const std::string& key){
    try{auto source=scripting::ReadPrefab(key);return WritePrefabAndUpdate(app,key,source,DiskPrefab(key),true);}catch(const std::exception& e){m_EditorError=e.what();return false;}
}
bool EditorLayer::StartPrefabEditor(Application& app,const std::string& key,const Json& data,const Json& base){
    try{
        AuthoringOnly(app);FinishTransaction(app);ecs::Vec3 origin{};
        for(const auto& node:data.at("entities"))if(node.at("id")==data.at("root")){auto p=node.at("components").at("Transform").value("position",Json::array({0,0,0}));origin={p.at(0).get<float>(),p.at(1).get<float>(),p.at(2).get<float>()};}
        auto world=std::make_unique<ecs::World>();auto root=scripting::InstantiatePrefab(*world,key,data,origin);
        m_PrefabSavedSceneSettings=app.GetSceneSettings();
        if(!app.BeginPrefabContext(std::move(world)))throw std::runtime_error("Cannot enter Prefab Editor");
        m_PrefabIsolation=m_Isolation;m_PrefabGameView=m_GameView;m_GameView=false;
        m_PrefabHistory=HistoryContext{m_Selection,std::move(m_UndoStack),std::move(m_RedoStack),m_Revision,m_SavedRevision,m_NextRevision,m_ExternalDirty};ResetHistory();
        m_EditingPrefab=key;m_PrefabDiskBase=base;m_PrefabRoot=root;SelectEntity(root);app.FocusEntity(root);m_EditorError.clear();return true;
    }catch(const std::exception& e){m_EditorError=e.what();return false;}
}
bool EditorLayer::OpenPrefabEditor(Application& app,const std::string& key){
    return OpenDocument(app,key);
}
void EditorLayer::RestorePrefabHistory(){
    ResetHistory();if(m_PrefabHistory){auto saved=std::move(*m_PrefabHistory);m_PrefabHistory.reset();m_Selection=std::move(saved.selection);m_UndoStack=std::move(saved.undo);m_RedoStack=std::move(saved.redo);m_Revision=saved.revision;m_SavedRevision=saved.saved;m_NextRevision=saved.next;m_ExternalDirty=saved.external;}
    m_Isolation=std::move(m_PrefabIsolation);m_GameView=m_PrefabGameView;
    m_EditingPrefab.clear();m_PrefabRoot={};m_PropertyDrafts.clear();
}
bool EditorLayer::ClosePrefabEditor(Application& app,bool discard){
    if(!app.IsPrefabEditing())return true;FinishTransaction(app);
    if(IsSceneDirty()&&!discard){m_EditorError="Prefab has unsaved changes; Save and Return or Discard explicitly";return false;}
    app.EndPrefabContext();RestorePrefabHistory();
    m_ActiveDocument=RegisterDocument(app,DocumentType::Scene,app.GetSceneKey()).id;m_FocusViewportWindow=true;
    m_EditorError.clear();return true;
}
bool EditorLayer::SavePrefabEditor(Application& app,bool stayOpen){
    if(!app.IsPrefabEditing())return false;
    try{
        FinishTransaction(app);if(app.GetWorld().Subtree(m_PrefabRoot).size()!=app.GetWorld().GetAliveCount())throw std::runtime_error("A prefab must contain exactly one root; parent all entities beneath its root");auto data=scripting::CapturePrefab(app.GetWorld(),m_PrefabRoot);auto key=m_EditingPrefab;auto base=m_PrefabDiskBase;
        if(DiskPrefab(key)!=base)throw std::runtime_error("Prefab changed on disk; return without saving and reopen it");
        auto nodes=UpdatedNodes(app.GetEditorWorld(),key,data);ValidateNodes(app.GetEditorWorld(),m_PrefabSavedSceneSettings,nodes);
        // Hold the existing prefab World and its history while the scene's existing
        // atomic file/instance transaction runs. A failed write must not recreate it.
        const auto root=m_PrefabRoot;const auto active=m_ActiveDocument;const auto position=app.GetCameraPosition();const auto yaw=app.GetCameraYaw(),pitch=app.GetCameraPitch();const auto settings=app.GetSceneSettings();
        auto isolation=m_Isolation;const auto gameView=m_GameView;
        HistoryContext history{m_Selection,std::move(m_UndoStack),std::move(m_RedoStack),m_Revision,m_SavedRevision,m_NextRevision,m_ExternalDirty};
        auto world=app.SuspendPrefabContext();RestorePrefabHistory();
        const bool ok=WritePrefabAndUpdate(app,key,data,base,true);const auto error=m_EditorError;
        if(ok&&!stayOpen){m_ActiveDocument=RegisterDocument(app,DocumentType::Scene,app.GetSceneKey()).id;m_FocusViewportWindow=true;return true;}
        app.BeginPrefabContext(std::move(world));
        m_PrefabIsolation=std::move(m_Isolation);m_PrefabGameView=m_GameView;
        m_PrefabHistory=HistoryContext{m_Selection,std::move(m_UndoStack),std::move(m_RedoStack),m_Revision,m_SavedRevision,m_NextRevision,m_ExternalDirty};ResetHistory();
        m_Selection=std::move(history.selection);m_UndoStack=std::move(history.undo);m_RedoStack=std::move(history.redo);m_Revision=history.revision;m_SavedRevision=history.saved;m_NextRevision=history.next;m_ExternalDirty=history.external;
        m_Isolation=std::move(isolation);m_GameView=gameView;m_EditingPrefab=key;m_PrefabRoot=root;m_PrefabDiskBase=ok&&!data.value("_legacySceneReferences",false)?data:base;m_ActiveDocument=active;
        app.SetSceneSettings(settings);app.SetEditorCameraPose(position,yaw,pitch);if(ok)MarkSaved();m_EditorError=error;return ok;
    }catch(const std::exception& e){m_EditorError=e.what();return false;}
}
void EditorLayer::DrawPrefabWorkflow(Application& app){
    if(!ImGui::BeginMainMenuBar())return;
    if(ImGui::BeginMenu("Prefab")){
        if(app.IsPrefabEditing()){
            ImGui::TextWrapped("Editing %s",m_EditingPrefab.c_str());
            if(ImGui::MenuItem("Save Prefab"))SavePrefabEditor(app,true);
            if(ImGui::MenuItem("Return to Scene"))BackToSceneDocument(app);
        }else{
            if(ImGui::MenuItem("Create Prefab from Selection...",nullptr,false,!app.IsEditorPlayMode()&&m_Selection.items.size()==1))m_PrefabCreatePopup=true;
            auto root=scripting::PrefabRoot(app.GetWorld(),m_Selection.active);auto* link=app.GetWorld().GetComponent<ecs::PrefabInstanceComponent>(root);
            if(link){
                ImGui::TextWrapped("%s",link->asset.c_str());
                if(ImGui::MenuItem("Open Source"))m_PrefabActionKey=link->asset;
                if(ImGui::MenuItem("Apply to Source and All Instances")){m_PrefabAction=3;m_PrefabActionEntity=root;m_PrefabConfirm=true;}
                if(ImGui::MenuItem("Revert Instance")){m_PrefabAction=4;m_PrefabActionEntity=root;m_PrefabConfirm=true;}
                if(ImGui::MenuItem("Update All Linked Instances")){m_PrefabAction=5;m_PrefabActionKey=link->asset;m_PrefabConfirm=true;}
            }
        }ImGui::EndMenu();
    }ImGui::EndMainMenuBar();
    if(!m_PrefabActionKey.empty()&&!m_PrefabConfirm&&!ImGui::IsPopupOpen("Prefab changes")){auto key=std::exchange(m_PrefabActionKey,{});OpenDocument(app,key);}
    if(m_PrefabCreatePopup){ImGui::OpenPopup("Create Prefab");m_PrefabCreatePopup=false;}
    if(ImGui::BeginPopupModal("Create Prefab",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextUnformatted("One selected root and its complete subtree. Path inside Assets:");ImGui::InputText("Asset key",&m_PrefabNewKey);
        if(ImGui::Button("Create")){if(CreatePrefab(app,m_PrefabNewKey))ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();
        if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::EndPopup();
    }
    if(m_PrefabConfirm){ImGui::OpenPopup("Prefab changes");m_PrefabConfirm=false;}
    if(ImGui::BeginPopupModal("Prefab changes",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextWrapped(m_PrefabAction==2?"Discard unsaved prefab edits and return to the scene?":"This operation replaces supported local component changes. Save/Apply also changes the source file and updates all linked instances. Root placement is preserved. The scene operation and file change can be undone together.");
        if(ImGui::Button(m_PrefabAction==2?"Discard":"Continue")){
            bool ok=false;switch(m_PrefabAction){case 1:ok=SavePrefabEditor(app);break;case 2:ok=ClosePrefabEditor(app,true);break;case 3:ok=ApplyPrefab(app,m_PrefabActionEntity);break;case 4:ok=RevertPrefab(app,m_PrefabActionEntity);break;case 5:ok=UpdatePrefabInstances(app,m_PrefabActionKey);break;}
            if(ok){m_PrefabActionKey.clear();ImGui::CloseCurrentPopup();}
        }ImGui::SameLine();if(ImGui::Button("Cancel")){m_PrefabActionKey.clear();ImGui::CloseCurrentPopup();}
        if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::EndPopup();
    }
}
}
