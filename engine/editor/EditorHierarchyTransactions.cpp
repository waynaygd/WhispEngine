#include "EditorLayer.h"
#include "../core/Application.h"
#include "../scene/SceneSerializer.h"
#include "SunSynchronization.h"
#include "../ecs/components/SceneIdentityComponent.h"
namespace editor {
namespace {
void ValidateGraphCandidate(ecs::World& world,const SceneSettings& settings){
    (void)SceneSettingsFromJson(SceneSettingsToJson(settings));
    (void)SceneSerializer::CaptureWorld(world,settings);
    for(auto e:world.Entities())if(world.HasComponent<ecs::MeshRendererComponent>(e))
        if(auto* c=world.GetComponent<ecs::ColliderComponent>(e);c&&c->autoFitFromMesh)
            throw std::runtime_error("Hierarchy Collider Entity "+world.GetComponent<ecs::SceneIdentityComponent>(e)->value+": requires explicit local geometry");
    const auto& background=settings.background;
    if(background.sunSync==SunSyncMode::SkyDrivesLight||(background.background==EditorBackground::DayNight&&background.driveLightProperties)){
        // Exercise the exact A3 guard on a disposable copy: validation must not
        // author a sun correction or change light properties during graph edits.
        auto probe=world.CloneData();
        SynchronizeHierarchySun(*probe,background,background.useIblEnvironment?settings.environment.rotationDegrees:background.skyRotation);
    }
}
void ValidateLocalCandidate(ecs::World& world,const SceneSettings& settings){
    ValidateGraphCandidate(world,settings);
    auto graph=ecs::HierarchyGraph::Build(world);
    for(const auto& node:graph->nodes)if(node.matrix)for(double value:*node.matrix)
        if(!std::isfinite(value)||std::abs(value)>1e8)throw std::runtime_error(node.id+": world matrix exceeds renderer numeric range");
    if(settings.background.sunSync==SunSyncMode::SkyDrivesLight){
        auto sun=PrimarySun(world);if(!sun.IsValid())return;
        auto probe=world.CloneData();auto target=probe->LocalEntity(sun.index);
        const auto before=*probe->GetComponent<ecs::TransformComponent>(target);
        const auto& b=settings.background;
        SynchronizeHierarchySun(*probe,b,b.useIblEnvironment?settings.environment.rotationDegrees:b.skyRotation);
        if(!SameLocalTransform(before,*probe->GetComponent<ecs::TransformComponent>(target)))
            throw std::runtime_error(graph->nodes[graph->entities.at(sun)].id+": Sky Drives Light owns the world orientation; Local edit would be overwritten");
    }
}
}
bool EditorLayer::EditHierarchyLocal(Application& app,ecs::Entity entity,const ecs::TransformComponent& value){
    try{
        if(app.IsEditorPlayMode()||&app.GetWorld()!=&app.GetEditorWorld()||m_Transaction||m_GroupTransform)
            throw std::runtime_error("Local Transform editing requires an idle Edit session");
        if(!app.GetWorld().IsHierarchyData()||m_Selection.items.size()!=1||m_Selection.active!=entity)
            throw std::runtime_error("Local Transform editing requires one selected Hierarchy Entity");
        ValidateLocalTransform(value);auto& world=app.GetWorld();auto next=ecs::HierarchyEdit::Capture(world,{entity});
        if(SameLocalTransform(next.front().local,value)){m_EditorError.clear();return true;}
        EntitySnapshot before;before.world=world.GetIdentity();before.selection=m_Selection;before.hierarchy=next;before.hierarchyLocal=true;
        next.front().local=value;UndoRecord record{"Edit Local Transform",std::move(before),m_Revision,m_NextRevision+1};
        m_UndoStack.reserve(std::max(m_UndoStack.size()+1,MaxUndoRecords+1));const auto settings=app.GetSceneSettings();
        ecs::HierarchyEdit::Apply(world,next,[&](auto& candidate){ValidateLocalCandidate(candidate,settings);});
        static_assert(std::is_nothrow_move_constructible_v<UndoRecord>);
        m_UndoStack.push_back(std::move(record));if(m_UndoStack.size()>MaxUndoRecords)m_UndoStack.erase(m_UndoStack.begin());
        m_Revision=++m_NextRevision;m_RedoStack.clear();m_PropertyDrafts.clear();m_EditorError.clear();return true;
    }catch(const std::exception& error){m_EditorError=error.what();return false;}
}
bool EditorLayer::ReparentHierarchy(Application& app,const std::vector<ecs::Entity>& entities,ecs::Entity target){
    try{
        if(app.IsEditorPlayMode()||&app.GetWorld()!=&app.GetEditorWorld()||m_Transaction||m_GroupTransform)
            throw std::runtime_error("Reparent requires an idle Edit session; Play/Pause are read-only");
        auto& world=app.GetWorld();auto next=ecs::HierarchyEdit::PrepareReparent(world,entities,target);
        if(next.empty()){m_EditorError.clear();return true;}
        std::vector<ecs::Entity> touched;for(const auto& state:next)touched.push_back(state.entity);
        EntitySnapshot before;before.world=world.GetIdentity();before.selection=m_Selection;
        before.hierarchy=ecs::HierarchyEdit::Capture(world,touched);
        UndoRecord record{target.IsValid()?"Reparent":"Unparent",std::move(before),m_Revision,m_NextRevision+1};
        m_UndoStack.reserve(std::max(m_UndoStack.size()+1,MaxUndoRecords+1));
        const auto settings=app.GetSceneSettings();
        ecs::HierarchyEdit::Apply(world,next,[&](auto& candidate){ValidateGraphCandidate(candidate,settings);});
        static_assert(std::is_nothrow_move_constructible_v<UndoRecord>);
        m_UndoStack.push_back(std::move(record));if(m_UndoStack.size()>MaxUndoRecords)m_UndoStack.erase(m_UndoStack.begin());
        m_Revision=++m_NextRevision;m_RedoStack.clear();m_PropertyDrafts.clear();m_EditorError.clear();return true;
    }catch(const std::exception& error){m_EditorError=error.what();return false;}
}
void EditorLayer::RestoreHierarchyHistory(Application& app,bool redo){
    if(app.IsEditorPlayMode())return;
    auto& source=redo?m_RedoStack:m_UndoStack;auto& destination=redo?m_UndoStack:m_RedoStack;
    if(source.empty())return;
    try{
        const auto& record=source.back();const auto& snapshot=record.snapshot;auto& world=app.GetWorld();
        if(snapshot.world!=world.GetIdentity()||!snapshot.hierarchy)throw std::runtime_error("Graph history belongs to a different World or unsupported transaction");
        EntitySnapshot inverse;inverse.world=world.GetIdentity();inverse.selection=m_Selection;inverse.hierarchyLocal=snapshot.hierarchyLocal;
        std::vector<ecs::Entity> entities;for(const auto& state:*snapshot.hierarchy)entities.push_back(state.entity);
        inverse.hierarchy=ecs::HierarchyEdit::Capture(world,entities);
        UndoRecord prepared{record.label,std::move(inverse),record.before,record.after};
        auto selection=snapshot.selection.value_or(m_Selection);selection.Prune(world);
        const auto revision=redo?record.after:record.before;
        destination.reserve(destination.size()+1);const auto settings=app.GetSceneSettings();
        if(snapshot.hierarchyLocal)for(const auto& state:*snapshot.hierarchy)ValidateLocalTransform(state.local);
        ecs::HierarchyEdit::Apply(world,*snapshot.hierarchy,[&](auto& candidate){if(snapshot.hierarchyLocal)ValidateLocalCandidate(candidate,settings);else ValidateGraphCandidate(candidate,settings);});
        m_HierarchyTransformDraft.reset();
        m_HierarchyTransformInvalidateInput=true;
        static_assert(std::is_nothrow_swappable_v<SelectionSet>);
        std::swap(m_Selection,selection);source.pop_back();destination.push_back(std::move(prepared));
        m_Revision=revision;m_PropertyDrafts.clear();m_EditorError.clear();
    }catch(const std::exception& error){m_EditorError=error.what();}
}
}
