#include "EditorLayer.h"
#include "../core/Application.h"
namespace editor {
void EditorLayer::FocusSelection(Application& app){PruneSelection(app);app.FocusEntities(m_Selection.items);}
void EditorLayer::PruneSelection(Application& app){
    auto& world=app.GetWorld();
    if(m_BoxSelection&&(app.IsEditorPlayMode()||m_BoxSelection->world!=world.GetIdentity()))m_BoxSelection.reset();
    if(app.IsEditorPlayMode())ExitIsolation();
    else std::erase_if(m_Isolation,[&](auto e){return !world.IsAlive(e);});
    if(m_GroupTransform){
        const auto& group=*m_GroupTransform;
        const bool alive=group.before.world==world.GetIdentity()&&std::all_of(group.entities.begin(),group.entities.end(),[&](auto e){return world.IsAlive(e)&&world.HasComponent<ecs::TransformComponent>(e);});
        if(!alive){
            // External destruction is not undone by cancelling a drag. Roll back
            // survivors only; do not resurrect an unrelated destroyed entity.
            for(std::size_t i=0;i<group.entities.size();++i)if(auto* t=world.GetComponent<ecs::TransformComponent>(group.entities[i])){world.SetWorldTransform(group.entities[i],group.originals[i]);app.NotifyEditorMutation(group.entities[i]);}
            m_GroupTransform.reset();
            if(m_DuplicateDrag)CancelTransaction(app);
            else if(m_Transaction&&m_Transaction->label=="Group Transform")m_Transaction.reset();
            m_GizmoUndoCaptured=false;m_CancelGizmoUntilRelease=true;
        }else if(group.entities!=world.HierarchyRoots(m_Selection.items))EndGroupTransform(app,false);
    }
    m_Selection.Prune(world);
}
void EditorLayer::SelectViewportEntity(Application& app,ecs::Entity e,bool ctrl,bool shift){
    PruneSelection(app);if(m_SelectionLocked||m_VertexSourcePicking)return;EndGroupTransform(app);FinishTransaction(app);
    if(!app.GetWorld().IsAlive(e)){if(!ctrl&&!shift)ClearSelection();return;}
    if(ctrl)m_Selection.Toggle(e);else if(shift)m_Selection.Add(e);else m_Selection.Replace(e);
    m_PropertyDrafts.clear();if(!app.IsEditorPlayMode())m_GameView=false;
}
void EditorLayer::SelectHierarchyEntity(Application& app,ecs::Entity e,bool ctrl,bool shift,const std::vector<ecs::Entity>& order){
    if(m_SelectionLocked||!app.GetWorld().IsAlive(e))return;
    if(!shift){SelectViewportEntity(app,e,ctrl,false);return;}
    PruneSelection(app);EndGroupTransform(app);FinishTransaction(app);m_Selection.Range(order,e,ctrl);m_Selection.Prune(app.GetWorld());m_PropertyDrafts.clear();if(!app.IsEditorPlayMode())m_GameView=false;
}
void EditorLayer::SelectAllEntities(Application& app,const std::vector<ecs::Entity>& order){
    if(m_SelectionLocked)return;
    PruneSelection(app);EndGroupTransform(app);FinishTransaction(app);m_Selection.Clear();for(auto e:order)if(app.GetWorld().IsAlive(e))m_Selection.Add(e);m_PropertyDrafts.clear();if(!app.IsEditorPlayMode())m_GameView=false;
}
EditorLayer::EntitySnapshot EditorLayer::CaptureSelection(Application& app,bool transformsOnly)const{
    EntitySnapshot snapshot;snapshot.world=app.GetWorld().GetIdentity();snapshot.selection=m_Selection;
    for(auto e:transformsOnly?app.GetWorld().HierarchyRoots(m_Selection.items):m_Selection.items){
        EntitySnapshot child;
        if(transformsOnly){child.entity=e;child.world=e.world;child.alive=app.GetWorld().IsAlive(e);if(auto* t=app.GetWorld().GetComponent<ecs::TransformComponent>(e))child.transform=*t;child.parent=app.GetWorld().Parent(e);}
        else child=CaptureSelectedEntity(app,e);
        child.selection.reset();snapshot.related.push_back(std::move(child));
    }return snapshot;
}
bool EditorLayer::DeleteSelection(Application& app){
    if(app.IsEditorPlayMode())return false;
    PruneSelection(app);EndGroupTransform(app);FinishTransaction(app);if(m_Selection.items.empty())return false;
    EntitySnapshot before;before.world=app.GetWorld().GetIdentity();before.selection=m_Selection;const auto entities=app.GetWorld().HierarchyRoots(m_Selection.items);
    for(auto e:entities)before.related.push_back(CaptureSubtree(app,e));
    for(auto e:entities)app.DestroySceneEntity(e);ClearSelection();CommitUndo(app,"Delete selection",std::move(before));return true;
}
}

namespace editor {
EditorLayer::EntitySnapshot EditorLayer::CaptureSubtree(Application& app,ecs::Entity root)const{
    auto before=CaptureSelectedEntity(app,root);for(auto e:app.GetWorld().Subtree(root))if(e!=root){auto child=CaptureSelectedEntity(app,e);child.selection.reset();before.related.push_back(std::move(child));}return before;
}
bool EditorLayer::ReparentEntity(Application& app,ecs::Entity child,ecs::Entity parent,bool keepWorld){
    if(app.IsEditorPlayMode())return false;FinishTransaction(app);auto before=CaptureSelectedEntity(app,child);
    if(!app.GetWorld().SetParent(child,parent,keepWorld,&m_EditorError))return false;app.NotifyEditorMutation(child);CommitUndo(app,parent.IsValid()?"Reparent":"Unparent",std::move(before));return true;
}
ecs::Entity EditorLayer::CreateChild(Application& app,ecs::Entity parent){
    if(app.IsEditorPlayMode()||!app.GetWorld().IsAlive(parent))return {};FinishTransaction(app);auto e=app.CreateEmptyEntity("Child");
    if(!app.GetWorld().SetParent(e,parent,false,&m_EditorError)){app.DestroySceneEntity(e);return {};}
    SelectEntityFromUser(app,e);EntitySnapshot before;before.entity=e;before.world=e.world;CommitUndo(app,"Create Child",before);return e;
}
}
