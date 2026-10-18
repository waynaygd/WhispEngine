#include "EditorLayer.h"
#include "SunSynchronization.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include "../core/Application.h"
#include "../core/Logger.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include "../scene/SceneSerializer.h"
#include "../ecs/components/PlayerStartComponent.h"
#include "../ecs/components/FogComponent.h"
#include "../scripting/EntityReference.h"
namespace editor {
namespace {
bool Same(const nlohmann::json& a,const nlohmann::json& b){return a==b;}
}
EditorLayer::EntitySnapshot EditorLayer::CaptureSelectedEntity(Application& app,ecs::Entity entity)const{
    EntitySnapshot s;s.entity=entity.IsValid()?entity:m_Selection.active;s.world=app.GetWorld().GetIdentity();s.alive=app.GetWorld().IsAlive(s.entity);
    if(s.alive)s.components=ecs::ComponentRegistry::Builtins().Capture(app.GetWorld(),s.entity);s.selection=m_Selection;return s;
}
EditorLayer::EntitySnapshot EditorLayer::CaptureLike(Application& app,const EntitySnapshot& sample)const{
    auto result=(sample.settings||(!sample.entity.IsValid()&&!sample.related.empty()))?EntitySnapshot{}:CaptureSelectedEntity(app,sample.entity);result.previousSelection=sample.previousSelection;result.world=app.GetWorld().GetIdentity();
    result.selection=sample.selection?std::optional<SelectionSet>(m_Selection):std::nullopt;
    if(sample.settings)result.settings=app.GetSceneSettings();for(const auto& related:sample.related)result.related.push_back(CaptureLike(app,related));return result;
}
void EditorLayer::RemapHistory(ecs::Entity oldEntity,ecs::Entity replacement){
    auto remap=[&](auto&& self,EntitySnapshot& s)->void{if(s.entity==oldEntity)s.entity=replacement;if(s.previousSelection==oldEntity)s.previousSelection=replacement;if(s.selection)s.selection->Remap(oldEntity,replacement);for(auto& item:s.related)self(self,item);};
    for(auto* stack:{&m_UndoStack,&m_RedoStack})for(auto& record:*stack)remap(remap,record.snapshot);
    if(m_Transaction)remap(remap,m_Transaction->before);
    m_Selection.Remap(oldEntity,replacement);
}
ecs::Entity EditorLayer::RestoreSnapshot(Application& app,const EntitySnapshot& snapshot,EntitySnapshot* inverse,ecs::World::CloneMap* remapped){
    ecs::World::CloneMap replacements;const bool root=remapped==nullptr;if(root)remapped=&replacements;
    auto& world=app.GetWorld();auto entity=snapshot.entity;
    const auto owner=snapshot.world?snapshot.world:entity.world;
    if(owner!=world.GetIdentity())throw std::runtime_error("Undo transaction belongs to a different World");
    if(snapshot.settings){std::string error;if(!app.SetSceneSettings(*snapshot.settings,&error))throw std::runtime_error(error);}
    if(entity.IsValid()){
        if(!snapshot.alive){if(world.IsAlive(entity))app.DestroySceneEntity(entity);if(m_Selection.active==entity)m_Selection.active=world.IsAlive(snapshot.previousSelection)?snapshot.previousSelection:ecs::Entity{};}
        else {
            if(snapshot.components.contains("SceneIdentity")){
                const auto& id=snapshot.components.at("SceneIdentity").at("value");
                for(auto other:world.Entities())if(other!=entity)if(auto* c=world.GetComponent<ecs::SceneIdentityComponent>(other))if(c->value==id.get<std::string>())throw std::runtime_error("Undo scene identity conflicts with another entity");
            }
            if(!world.IsAlive(entity)){auto old=entity;entity=app.CreateEmptyEntity();(*remapped)[old]=entity;RemapHistory(old,entity);if(inverse)inverse->entity=entity;}
            ecs::ComponentRegistry::Builtins().Restore(world,entity,snapshot.components);app.NotifyEditorMutation(entity);m_Selection.active=entity;
        }
    }
    for(std::size_t i=0;i<snapshot.related.size();++i)RestoreSnapshot(app,snapshot.related[i],inverse?&inverse->related[i]:nullptr,remapped);
    if(!entity.IsValid()&&!snapshot.related.empty()&&std::all_of(snapshot.related.begin(),snapshot.related.end(),[](const auto& item){return !item.alive;}))m_Selection.active=world.IsAlive(snapshot.previousSelection)?snapshot.previousSelection:ecs::Entity{};
    if(root){
        if(snapshot.selection){m_Selection=*snapshot.selection;for(auto [old,replacement]:*remapped)m_Selection.Remap(old,replacement);m_Selection.Prune(world);}
        else SelectEntity(m_Selection.active);
        if(inverse&&inverse->selection)for(auto [old,replacement]:*remapped)inverse->selection->Remap(old,replacement);
        m_PropertyDrafts.clear();
    }
    return entity;
}
void EditorLayer::BeginTransaction(const std::string& label,EntitySnapshot before){
    if(m_Transaction)return;m_Transaction=PendingTransaction{label,std::move(before)};
}
void EditorLayer::PushUndoBeforeChange(Application& app,const std::string& label){
    if(m_Transaction)return;auto before=CaptureSelectedEntity(app);if(before.alive)BeginTransaction(label,std::move(before));
}
void EditorLayer::BeginSceneTransaction(Application& app,const std::string& label){
    if(m_Transaction)return;EntitySnapshot before;before.world=app.GetWorld().GetIdentity();before.settings=app.GetSceneSettings();before.selection=m_Selection;
    for(auto e:app.GetWorld().Entities())if(app.GetWorld().HasComponent<ecs::DirectionalLightComponent>(e))before.related.push_back(CaptureSelectedEntity(app,e));
    BeginTransaction(label,std::move(before));
}
void EditorLayer::CommitUndo(Application& app,const std::string& label,EntitySnapshot before){
    FinishTransaction(app);if(!before.world)before.world=before.entity.world;
    auto after=CaptureLike(app,before);
    auto equal=[&](auto&& self,const EntitySnapshot& a,const EntitySnapshot& b)->bool{
        if(a.alive!=b.alive||!Same(a.components,b.components)||a.related.size()!=b.related.size()||a.settings.has_value()!=b.settings.has_value())return false;
        if(a.settings&&SceneSettingsToJson(*a.settings)!=SceneSettingsToJson(*b.settings))return false;
        for(std::size_t i=0;i<a.related.size();++i)if(!self(self,a.related[i],b.related[i]))return false;return true;
    };
    if(equal(equal,before,after))return;
    m_UndoStack.push_back({label,std::move(before),m_Revision,++m_NextRevision});m_Revision=m_NextRevision;
    if(m_UndoStack.size()>MaxUndoRecords)m_UndoStack.erase(m_UndoStack.begin());m_RedoStack.clear();
}
void EditorLayer::FinishTransaction(Application& app){
    if(!m_Transaction)return;auto pending=std::move(*m_Transaction);m_Transaction.reset();CommitUndo(app,pending.label,std::move(pending.before));if(pending.label=="Duplicate and move")m_DuplicateDrag=false;if(pending.label=="Group Transform")m_GroupTransform.reset();
}
void EditorLayer::CancelTransaction(Application& app){
    if(!m_Transaction)return;auto pending=std::move(*m_Transaction);m_Transaction.reset();
    try{RestoreSnapshot(app,pending.before);}catch(const std::exception& e){m_EditorError=e.what();Logger::Get().Error(m_EditorError);}m_GizmoUndoCaptured=false;m_DuplicateDrag=false;m_LightPointerCaptured=false;m_PlaneScaleCaptured=false;m_GroupTransform.reset();
}
void EditorLayer::Undo(Application& app){
    if(app.GetWorld().IsHierarchyData()){RestoreHierarchyHistory(app,false);return;}
    FinishTransaction(app);if(m_UndoStack.empty())return;auto record=std::move(m_UndoStack.back());m_UndoStack.pop_back();
    try{auto inverse=CaptureLike(app,record.snapshot);inverse.entity=RestoreSnapshot(app,record.snapshot,&inverse);m_Revision=record.before;m_RedoStack.push_back({record.label,std::move(inverse),record.before,record.after});}
    catch(const std::exception& e){m_EditorError=e.what();Logger::Get().Error(m_EditorError);m_UndoStack.push_back(std::move(record));}
}
void EditorLayer::Redo(Application& app){
    if(app.GetWorld().IsHierarchyData()){RestoreHierarchyHistory(app,true);return;}
    FinishTransaction(app);if(m_RedoStack.empty())return;auto record=std::move(m_RedoStack.back());m_RedoStack.pop_back();
    try{auto inverse=CaptureLike(app,record.snapshot);inverse.entity=RestoreSnapshot(app,record.snapshot,&inverse);m_Revision=record.after;m_UndoStack.push_back({record.label,std::move(inverse),record.before,record.after});}
    catch(const std::exception& e){m_EditorError=e.what();Logger::Get().Error(m_EditorError);m_RedoStack.push_back(std::move(record));}
}
void EditorLayer::SelectEntityFromUser(Application& app,ecs::Entity e){
    SelectEntity(e);
    if(!app.IsEditorPlayMode()&&app.GetWorld().IsAlive(e))m_GameView=false;
}
ecs::Entity EditorLayer::CreateEntity(Application& app,const std::string& name){
    if(app.IsEditorPlayMode())return {};
    FinishTransaction(app);auto e=app.CreateEmptyEntity(name);SelectEntityFromUser(app,e);EntitySnapshot before;before.entity=e;before.world=e.world;CommitUndo(app,"Create entity",before);return e;
}
ecs::Entity EditorLayer::CreatePlayerStart(Application& app){
    if(app.IsEditorPlayMode())return {};
    FinishTransaction(app);auto previous=m_Selection.active;
    auto e=app.CreateEmptyEntity("PlayerStart");app.GetWorld().AddComponent<ecs::PlayerStartComponent>(e);
    SelectEntityFromUser(app,e);EntitySnapshot before;before.entity=e;before.world=e.world;before.previousSelection=previous;
    CommitUndo(app,"Create Player Start",before);return e;
}
ecs::Entity EditorLayer::CreateFog(Application& app){
    if(app.IsEditorPlayMode())return {};FinishTransaction(app);auto previous=m_Selection.active;
    auto e=app.CreateEmptyEntity("Fog");app.GetWorld().AddComponent<ecs::FogComponent>(e);SelectEntityFromUser(app,e);
    EntitySnapshot before;before.entity=e;before.world=e.world;before.previousSelection=previous;CommitUndo(app,"Create Fog",before);return e;
}
bool EditorLayer::DeleteEntity(Application& app,ecs::Entity e){
    FinishTransaction(app);if(!app.GetWorld().IsAlive(e))return false;auto before=CaptureSelectedEntity(app,e);app.DestroySceneEntity(e);CommitUndo(app,"Delete entity",std::move(before));return true;
}
ecs::Entity EditorLayer::DuplicateEntity(Application& app,ecs::Entity source){
    if(app.IsEditorPlayMode())return {};
    FinishTransaction(app);if(!app.GetWorld().IsAlive(source))return {};auto values=ecs::ComponentRegistry::Builtins().Capture(app.GetWorld(),source);
    auto e=app.CreateEmptyEntity();ecs::ComponentRegistry::Builtins().Restore(app.GetWorld(),e,values);
    if(auto* id=app.GetWorld().GetComponent<ecs::SceneIdentityComponent>(e))id->value=ecs::NewSceneIdentity();else app.GetWorld().AddComponent<ecs::SceneIdentityComponent>(e);
    if(auto* tag=app.GetWorld().GetComponent<ecs::TagComponent>(e))tag->name+=" Copy";
    SelectEntityFromUser(app,e);EntitySnapshot before;before.entity=e;before.world=e.world;CommitUndo(app,"Duplicate entity",before);return e;
}
bool EditorLayer::CopyEntity(Application& app,ecs::Entity source){
    if(app.IsEditorPlayMode()||!app.GetWorld().IsAlive(source))return false;
    m_Clipboard=ecs::ComponentRegistry::Builtins().Capture(app.GetWorld(),source);m_Clipboard.erase("SceneIdentity");return true;
}
ecs::Entity EditorLayer::PasteEntity(Application& app){
    if(app.IsEditorPlayMode()||!m_Clipboard.is_object())return {};
    FinishTransaction(app);auto e=app.CreateEmptyEntity();
    try{ecs::ComponentRegistry::Builtins().Restore(app.GetWorld(),e,m_Clipboard);if(!app.GetWorld().HasComponent<ecs::SceneIdentityComponent>(e))app.GetWorld().AddComponent<ecs::SceneIdentityComponent>(e);app.GetWorld().GetComponent<ecs::SceneIdentityComponent>(e)->value=ecs::NewSceneIdentity();}
    catch(const std::exception& ex){app.DestroySceneEntity(e);m_EditorError=ex.what();return {};}
    SelectEntityFromUser(app,e);app.NotifyEditorMutation(e);EntitySnapshot before;before.entity=e;before.world=e.world;CommitUndo(app,"Paste entity",before);return e;
}
ecs::Entity EditorLayer::BeginDuplicateDrag(Application& app){
    if(app.IsEditorPlayMode()||m_DuplicateDrag||!app.GetWorld().IsAlive(m_Selection.active))return {};
    FinishTransaction(app);
    auto& world=app.GetWorld();const auto source=m_Selection;
    std::vector<nlohmann::json> values;std::vector<ecs::Entity> copies;
    std::unordered_map<std::string,std::string> identities;
    auto selected=source;
    EntitySnapshot before;before.world=world.GetIdentity();before.selection=source;
    try{
        // Capture the entire source set before creating or restoring any entity.
        for(auto e:source.items)values.push_back(ecs::ComponentRegistry::Builtins().Capture(world,e));
        for(std::size_t i=0;i<source.items.size();++i){
            const auto id=ecs::NewSceneIdentity();auto& data=values[i];
            if(data.contains("SceneIdentity"))identities.emplace(data["SceneIdentity"]["value"].get<std::string>(),id);
            data["SceneIdentity"]={{"value",id}};
            auto e=app.CreateEmptyEntity();copies.push_back(e);selected.Remap(source.items[i],e);
            EntitySnapshot absent;absent.entity=e;absent.world=world.GetIdentity();before.related.push_back(std::move(absent));
        }
        for(std::size_t i=0;i<copies.size();++i){
            auto& data=values[i];
            // Lua Inspector entity fields are the only registered persistent
            // entity references today. Strings/assets are deliberately untouched.
            if(data.contains("LuaScript")&&data["LuaScript"].contains("properties"))
                for(auto& value:data["LuaScript"]["properties"])
                    if(!value.is_null()&&scripting::ValidEntityReference(value)){
                        auto found=identities.find(value["$entity"].get<std::string>());
                        if(found!=identities.end())value["$entity"]=found->second;
                    }
            ecs::ComponentRegistry::Builtins().Restore(world,copies[i],data);app.NotifyEditorMutation(copies[i]);
        }
    }catch(const std::exception& error){for(auto e:copies)app.DestroySceneEntity(e);m_EditorError=error.what();return {};}
    m_Selection=std::move(selected);m_PropertyDrafts.clear();m_GameView=false;
    // Keep pivot/current and immutable source transforms for this same gesture.
    if(m_GroupTransform)m_GroupTransform->entities=copies;
    m_DragOriginal=source.active;m_DuplicateDrag=true;BeginTransaction("Duplicate and move",std::move(before));return m_Selection.active;
}
ecs::Entity EditorLayer::CreatePrimitive(Application& app,const std::string& type){
    if(app.IsEditorPlayMode()||(type!="cube"&&type!="sphere"&&type!="cylinder"&&type!="cone"&&type!="plane"))return {};
    const std::string prefix=app.GetProject()?"@engine/":"";
    auto e=CreateModelEntity(app,prefix+"models/primitives/"+type+".obj",type);if(e.IsValid())app.GetWorld().GetComponent<ecs::MaterialComponent>(e)->materialPath=prefix+"materials/default_primitive.material.json";return e;
}
ecs::Entity EditorLayer::CreateModelEntity(Application& app,const std::string& key,const std::string& name){
    if(app.IsEditorPlayMode()||key.empty()||!ecs::ComponentRegistry::ValidateAssetKey(key,ecs::AssetKind::Mesh,&m_EditorError))return {};
    FinishTransaction(app);auto e=app.CreateEmptyEntity(name);
    auto& mesh=app.GetWorld().AddComponent<ecs::MeshRendererComponent>(e);mesh.meshPath=key;
    app.GetWorld().AddComponent<ecs::MaterialComponent>(e);SelectEntityFromUser(app,e);
    EntitySnapshot before;before.entity=e;before.world=e.world;CommitUndo(app,"Create model",before);app.NotifyEditorMutation(e);return e;
}

bool EditorLayer::RenameEntity(Application& app,ecs::Entity e,const std::string& name){
    FinishTransaction(app);if(!app.GetWorld().IsAlive(e))return false;auto before=CaptureSelectedEntity(app,e);
    if(!app.GetWorld().HasComponent<ecs::TagComponent>(e))app.GetWorld().AddComponent<ecs::TagComponent>(e);
    if(!ecs::ComponentRegistry::Builtins().Set(app.GetWorld(),e,ecs::ComponentId("Tag"),"name",name,&m_EditorError))return false;CommitUndo(app,"Rename entity",std::move(before));return true;
}
bool EditorLayer::AddComponent(Application& app,ecs::Entity e,ecs::ComponentTypeId id){
    FinishTransaction(app);if(!app.GetWorld().IsAlive(e))return false;auto before=CaptureSelectedEntity(app,e);
    if(!ecs::ComponentRegistry::Builtins().Add(app.GetWorld(),e,id,&m_EditorError))return false;app.NotifyEditorMutation(e);CommitUndo(app,"Add component",std::move(before));return true;
}
bool EditorLayer::RemoveComponent(Application& app,ecs::Entity e,ecs::ComponentTypeId id){
    FinishTransaction(app);if(!app.GetWorld().IsAlive(e))return false;auto before=CaptureSelectedEntity(app,e);
    if(!ecs::ComponentRegistry::Builtins().Remove(app.GetWorld(),e,id,&m_EditorError))return false;app.NotifyEditorMutation(e);CommitUndo(app,"Remove component",std::move(before));return true;
}
bool EditorLayer::ResetComponent(Application& app,ecs::Entity e,ecs::ComponentTypeId id){
    FinishTransaction(app);if(!app.GetWorld().IsAlive(e))return false;auto before=CaptureSelectedEntity(app,e);
    if(!ecs::ComponentRegistry::Builtins().Reset(app.GetWorld(),e,id,&m_EditorError))return false;app.NotifyEditorMutation(e);CommitUndo(app,"Reset component",std::move(before));return true;
}
bool EditorLayer::SetProperty(Application& app,ecs::Entity e,ecs::ComponentTypeId id,const std::string& key,const ecs::PropertyValue& value,bool transactional){
    if(transactional)FinishTransaction(app);if(!app.GetWorld().IsAlive(e))return false;auto before=transactional?CaptureSelectedEntity(app,e):EntitySnapshot{};
    const auto previousError=m_EditorError;
    if(!ecs::ComponentRegistry::Builtins().Set(app.GetWorld(),e,id,key,value,&m_EditorError)){if(previousError!=m_EditorError)Logger::Get().Warn(m_EditorError);return false;}app.NotifyEditorMutation(e);
    if(transactional)CommitUndo(app,"Edit property",std::move(before));return true;
}
void EditorLayer::EnterRuntime(const ecs::World::CloneMap& mapping){
    m_HierarchyTransformDraft.reset();m_HierarchyTransformInvalidateInput=true;
    m_HierarchyDrag.reset();m_HierarchyClick={};m_HierarchyDragCancelled=false;
    m_EditHierarchyTree=std::move(m_HierarchyTree);m_HierarchyTree={};m_HierarchyTreeRows.clear();m_HierarchyVisible.clear();
    ImGuizmo::Enable(false);m_ToolActive=false;m_CancelGizmoUntilRelease=true;
    m_GroupTransform.reset();m_EditHistory=HistoryContext{m_Selection,std::move(m_UndoStack),std::move(m_RedoStack),m_Revision,m_SavedRevision,m_NextRevision,m_ExternalDirty};
    SelectionSet runtime;for(auto e:m_Selection.items)if(auto found=mapping.find(e);found!=mapping.end())runtime.Add(found->second);
    if(auto found=mapping.find(m_Selection.active);found!=mapping.end()&&runtime.Contains(found->second))runtime.active=found->second;
    if(auto found=mapping.find(m_Selection.anchor);found!=mapping.end())runtime.anchor=found->second;m_Selection=std::move(runtime);
    m_Transaction.reset();m_PropertyDrafts.clear();m_Revision=m_SavedRevision=m_NextRevision=0;m_ExternalDirty=false;m_GizmoUndoCaptured=false;m_DuplicateDrag=false;m_LightPointerCaptured=false;m_PlaneScaleCaptured=false;
}
void EditorLayer::LeaveRuntime(){
    m_HierarchyTransformDraft.reset();m_HierarchyTransformInvalidateInput=true;
    m_HierarchyDrag.reset();m_HierarchyClick={};m_HierarchyDragCancelled=false;
    m_HierarchyTree=m_EditHierarchyTree?std::move(*m_EditHierarchyTree):HierarchyTreeState{};m_EditHierarchyTree.reset();m_HierarchyTreeRows.clear();m_HierarchyVisible.clear();
    ImGuizmo::Enable(false);m_ToolActive=m_DuplicateDrag=m_LightPointerCaptured=false;m_CancelGizmoUntilRelease=true;
    m_GroupTransform.reset();m_UndoStack.clear();m_RedoStack.clear();m_Selection.Clear();m_Transaction.reset();m_PropertyDrafts.clear();m_GizmoUndoCaptured=false;
    if(m_EditHistory){m_Selection=m_EditHistory->selection;m_UndoStack=std::move(m_EditHistory->undo);m_RedoStack=std::move(m_EditHistory->redo);m_Revision=m_EditHistory->revision;m_SavedRevision=m_EditHistory->saved;m_NextRevision=m_EditHistory->next;m_ExternalDirty=m_EditHistory->external;m_EditHistory.reset();}
}
}

namespace editor {
bool EditorLayer::RotateSelectedQuarter(Application& app,int axis,int turns,bool world){
    if(app.IsEditorPlayMode())return false;auto t=ecs::ReadWorldTransform(app.GetWorld(),m_Selection.active);if(!t)return false;
    if(app.GetSceneBackground().sunSync==SunSyncMode::SkyDrivesLight&&PrimarySun(app.GetWorld())==m_Selection.active)return false;
    auto rotation=RotateQuarterTurn(*t,axis,turns,world);if(!rotation)return false;
    return SetProperty(app,m_Selection.active,ecs::ComponentId("Transform"),"rotation",*rotation);
}
}
