#include "EditorLayer.h"
#include "../core/Application.h"
#include "../core/Logger.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include "../scene/SceneSerializer.h"
namespace editor {
namespace {
bool Same(const nlohmann::json& a,const nlohmann::json& b){return a==b;}
}
EditorLayer::EntitySnapshot EditorLayer::CaptureSelectedEntity(Application& app,ecs::Entity entity)const{
    EntitySnapshot s;s.entity=entity.IsValid()?entity:m_SelectedEntity;s.world=app.GetWorld().GetIdentity();s.alive=app.GetWorld().IsAlive(s.entity);
    if(s.alive)s.components=ecs::ComponentRegistry::Builtins().Capture(app.GetWorld(),s.entity);return s;
}
EditorLayer::EntitySnapshot EditorLayer::CaptureLike(Application& app,const EntitySnapshot& sample)const{
    auto result=sample.settings?EntitySnapshot{}:CaptureSelectedEntity(app,sample.entity);result.world=app.GetWorld().GetIdentity();
    if(sample.settings)result.settings=app.GetSceneSettings();for(const auto& related:sample.related)result.related.push_back(CaptureLike(app,related));return result;
}
void EditorLayer::RemapHistory(ecs::Entity oldEntity,ecs::Entity replacement){
    auto remap=[&](auto&& self,EntitySnapshot& s)->void{if(s.entity==oldEntity)s.entity=replacement;for(auto& item:s.related)self(self,item);};
    for(auto* stack:{&m_UndoStack,&m_RedoStack})for(auto& record:*stack)remap(remap,record.snapshot);
    if(m_Transaction)remap(remap,m_Transaction->before);
}
ecs::Entity EditorLayer::RestoreSnapshot(Application& app,const EntitySnapshot& snapshot){
    auto& world=app.GetWorld();auto entity=snapshot.entity;
    const auto owner=snapshot.world?snapshot.world:entity.world;
    if(owner!=world.GetIdentity())throw std::runtime_error("Undo transaction belongs to a different World");
    if(snapshot.settings){std::string error;if(!app.SetSceneSettings(*snapshot.settings,&error))throw std::runtime_error(error);}
    if(entity.IsValid()){
        if(!snapshot.alive){if(world.IsAlive(entity))app.DestroySceneEntity(entity);if(m_SelectedEntity==entity)m_SelectedEntity={};}
        else {
            if(snapshot.components.contains("SceneIdentity")){
                const auto& id=snapshot.components.at("SceneIdentity").at("value");
                for(auto other:world.Entities())if(other!=entity)if(auto* c=world.GetComponent<ecs::SceneIdentityComponent>(other))if(c->value==id.get<std::string>())throw std::runtime_error("Undo scene identity conflicts with another entity");
            }
            if(!world.IsAlive(entity)){auto old=entity;entity=app.CreateEmptyEntity();RemapHistory(old,entity);}
            ecs::ComponentRegistry::Builtins().Restore(world,entity,snapshot.components);app.NotifyEditorMutation(entity);m_SelectedEntity=entity;
        }
    }
    for(const auto& item:snapshot.related)RestoreSnapshot(app,item);return entity;
}
void EditorLayer::BeginTransaction(const std::string& label,EntitySnapshot before){
    if(m_Transaction)return;m_Transaction=PendingTransaction{label,std::move(before)};
}
void EditorLayer::PushUndoBeforeChange(Application& app,const std::string& label){
    if(m_Transaction)return;auto before=CaptureSelectedEntity(app);if(before.alive)BeginTransaction(label,std::move(before));
}
void EditorLayer::BeginSceneTransaction(Application& app,const std::string& label){
    if(m_Transaction)return;EntitySnapshot before;before.world=app.GetWorld().GetIdentity();before.settings=app.GetSceneSettings();
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
    if(!m_Transaction)return;auto pending=std::move(*m_Transaction);m_Transaction.reset();CommitUndo(app,pending.label,std::move(pending.before));
}
void EditorLayer::CancelTransaction(Application& app){
    if(!m_Transaction)return;auto pending=std::move(*m_Transaction);m_Transaction.reset();
    try{RestoreSnapshot(app,pending.before);}catch(const std::exception& e){m_EditorError=e.what();Logger::Get().Error(m_EditorError);}m_GizmoUndoCaptured=false;
}
void EditorLayer::Undo(Application& app){
    FinishTransaction(app);if(m_UndoStack.empty())return;auto record=std::move(m_UndoStack.back());m_UndoStack.pop_back();
    try{auto inverse=CaptureLike(app,record.snapshot);inverse.entity=RestoreSnapshot(app,record.snapshot);m_Revision=record.before;m_RedoStack.push_back({record.label,std::move(inverse),record.before,record.after});}
    catch(const std::exception& e){m_EditorError=e.what();Logger::Get().Error(m_EditorError);m_UndoStack.push_back(std::move(record));}
}
void EditorLayer::Redo(Application& app){
    FinishTransaction(app);if(m_RedoStack.empty())return;auto record=std::move(m_RedoStack.back());m_RedoStack.pop_back();
    try{auto inverse=CaptureLike(app,record.snapshot);inverse.entity=RestoreSnapshot(app,record.snapshot);m_Revision=record.after;m_UndoStack.push_back({record.label,std::move(inverse),record.before,record.after});}
    catch(const std::exception& e){m_EditorError=e.what();Logger::Get().Error(m_EditorError);m_RedoStack.push_back(std::move(record));}
}
ecs::Entity EditorLayer::CreateEntity(Application& app,const std::string& name){
    FinishTransaction(app);auto e=app.CreateEmptyEntity(name);m_SelectedEntity=e;EntitySnapshot before;before.entity=e;before.world=e.world;CommitUndo(app,"Create entity",before);return e;
}
bool EditorLayer::DeleteEntity(Application& app,ecs::Entity e){
    FinishTransaction(app);if(!app.GetWorld().IsAlive(e))return false;auto before=CaptureSelectedEntity(app,e);app.DestroySceneEntity(e);CommitUndo(app,"Delete entity",std::move(before));return true;
}
ecs::Entity EditorLayer::DuplicateEntity(Application& app,ecs::Entity source){
    FinishTransaction(app);if(!app.GetWorld().IsAlive(source))return {};auto values=ecs::ComponentRegistry::Builtins().Capture(app.GetWorld(),source);
    auto e=app.CreateEmptyEntity();ecs::ComponentRegistry::Builtins().Restore(app.GetWorld(),e,values);
    if(auto* id=app.GetWorld().GetComponent<ecs::SceneIdentityComponent>(e))id->value=ecs::NewSceneIdentity();else app.GetWorld().AddComponent<ecs::SceneIdentityComponent>(e);
    if(auto* tag=app.GetWorld().GetComponent<ecs::TagComponent>(e))tag->name+=" Copy";
    m_SelectedEntity=e;EntitySnapshot before;before.entity=e;before.world=e.world;CommitUndo(app,"Duplicate entity",before);return e;
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
    m_EditHistory=HistoryContext{m_SelectedEntity,std::move(m_UndoStack),std::move(m_RedoStack),m_Revision,m_SavedRevision,m_NextRevision,m_ExternalDirty};
    auto found=mapping.find(m_SelectedEntity);m_SelectedEntity=found==mapping.end()?ecs::Entity{}:found->second;
    m_Transaction.reset();m_PropertyDrafts.clear();m_Revision=m_SavedRevision=m_NextRevision=0;m_ExternalDirty=false;m_GizmoUndoCaptured=false;
}
void EditorLayer::LeaveRuntime(){
    m_UndoStack.clear();m_RedoStack.clear();m_SelectedEntity={};m_Transaction.reset();m_PropertyDrafts.clear();m_GizmoUndoCaptured=false;
    if(m_EditHistory){m_SelectedEntity=m_EditHistory->selected;m_UndoStack=std::move(m_EditHistory->undo);m_RedoStack=std::move(m_EditHistory->redo);m_Revision=m_EditHistory->revision;m_SavedRevision=m_EditHistory->saved;m_NextRevision=m_EditHistory->next;m_ExternalDirty=m_EditHistory->external;m_EditHistory.reset();}
}
}
