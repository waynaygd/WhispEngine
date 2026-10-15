#include "EditorLayer.h"
#include "../core/Application.h"
namespace editor {
void EditorLayer::PruneSelection(Application& app){
    auto& world=app.GetWorld();
    if(m_GroupTransform){
        const auto& group=*m_GroupTransform;
        const bool alive=group.before.world==world.GetIdentity()&&std::all_of(group.entities.begin(),group.entities.end(),[&](auto e){return world.IsAlive(e)&&world.HasComponent<ecs::TransformComponent>(e);});
        if(!alive){
            // External destruction is not undone by cancelling a drag. Roll back
            // survivors only; do not resurrect an unrelated destroyed entity.
            for(std::size_t i=0;i<group.entities.size();++i)if(auto* t=world.GetComponent<ecs::TransformComponent>(group.entities[i])){*t=group.originals[i];app.NotifyEditorMutation(group.entities[i]);}
            if(m_Transaction&&m_Transaction->label=="Group Transform")m_Transaction.reset();m_GroupTransform.reset();m_GizmoUndoCaptured=false;m_CancelGizmoUntilRelease=true;
        }else if(group.entities!=m_Selection.items)EndGroupTransform(app,false);
    }
    m_Selection.Prune(world);
}
void EditorLayer::SelectViewportEntity(Application& app,ecs::Entity e,bool ctrl,bool shift){
    PruneSelection(app);EndGroupTransform(app);FinishTransaction(app);
    if(!app.GetWorld().IsAlive(e)){if(!ctrl&&!shift)ClearSelection();return;}
    if(ctrl)m_Selection.Toggle(e);else if(shift)m_Selection.Add(e);else m_Selection.Replace(e);
    m_PropertyDrafts.clear();if(!app.IsEditorPlayMode())m_GameView=false;
}
void EditorLayer::SelectHierarchyEntity(Application& app,ecs::Entity e,bool ctrl,bool shift,const std::vector<ecs::Entity>& order){
    if(!app.GetWorld().IsAlive(e))return;
    if(!shift){SelectViewportEntity(app,e,ctrl,false);return;}
    PruneSelection(app);EndGroupTransform(app);FinishTransaction(app);m_Selection.Range(order,e,ctrl);m_Selection.Prune(app.GetWorld());m_PropertyDrafts.clear();if(!app.IsEditorPlayMode())m_GameView=false;
}
void EditorLayer::SelectAllEntities(Application& app,const std::vector<ecs::Entity>& order){
    PruneSelection(app);EndGroupTransform(app);FinishTransaction(app);m_Selection.Clear();for(auto e:order)if(app.GetWorld().IsAlive(e))m_Selection.Add(e);m_PropertyDrafts.clear();if(!app.IsEditorPlayMode())m_GameView=false;
}
EditorLayer::EntitySnapshot EditorLayer::CaptureSelection(Application& app)const{
    EntitySnapshot snapshot;snapshot.world=app.GetWorld().GetIdentity();snapshot.selection=m_Selection;
    for(auto e:m_Selection.items){auto child=CaptureSelectedEntity(app,e);child.selection.reset();snapshot.related.push_back(std::move(child));}return snapshot;
}
bool EditorLayer::DeleteSelection(Application& app){
    if(app.IsEditorPlayMode())return false;
    PruneSelection(app);EndGroupTransform(app);FinishTransaction(app);if(m_Selection.items.empty())return false;
    auto before=CaptureSelection(app);const auto entities=m_Selection.items;
    for(auto e:entities)app.DestroySceneEntity(e);ClearSelection();CommitUndo(app,"Delete selection",std::move(before));return true;
}
}
