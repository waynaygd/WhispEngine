#include "EditorLayer.h"
#include "SunSynchronization.h"
#include "../core/Application.h"
#include <imgui.h>
#include <ImGuizmo.h>
namespace editor {
bool EditorLayer::BeginGroupTransform(Application& app,bool local){
    if(app.IsEditorPlayMode())return false;
    PruneSelection(app);EndGroupTransform(app);FinishTransaction(app);if(m_Selection.items.size()<2)return false;
    GroupSession group;group.entities=app.GetWorld().HierarchyRoots(m_Selection.items);group.before=CaptureSelection(app,true);
    double x=0,y=0,z=0;
    for(auto e:group.entities){auto* t=app.GetWorld().GetComponent<ecs::TransformComponent>(e);if(!t||!ValidGroupSource(*t)){m_EditorError="Group Transform requires valid Transform on every selected entity";return false;}auto absolute=app.GetWorld().WorldTransform(e);group.originals.push_back(absolute);x+=absolute.position.x;y+=absolute.position.y;z+=absolute.position.z;}
    const double n=double(group.entities.size());group.pivot.position={float(x/n),float(y/n),float(z/n)};
    if(local)group.pivot.rotation=app.GetWorld().WorldTransform(m_Selection.active).rotation;
    group.current=TransformMatrix(group.pivot);m_GroupTransform=std::move(group);return true;
}
bool EditorLayer::UpdateGroupTransform(Application& app,const ImportMatrix& target){
    PruneSelection(app);if(!m_GroupTransform||app.IsEditorPlayMode())return false;
    auto& group=*m_GroupTransform;
    auto result=TransformGroup(group.originals,group.pivot,target);
    if(!result){m_EditorError="Group transform rejected: non-finite/singular scale or shear. Use uniform scale or compatible object orientations.";return false;}
    const auto sun=PrimarySun(app.GetWorld());bool changed=false;
    for(std::size_t i=0;i<group.entities.size();++i){
        const auto& value=(*result)[i];const auto& original=group.originals[i];
        auto a=ecs::RenderRotation(value.rotation),b=ecs::RenderRotation(original.rotation);float dot=std::abs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w);
        if(group.entities[i]==sun&&app.GetSceneBackground().sunSync==SunSyncMode::SkyDrivesLight&&dot<.999999f){m_EditorError="Group rotation contains a sun driven by the sky";return false;}
        auto current=TransformMatrix(app.GetWorld().WorldTransform(group.entities[i]));auto proposed=TransformMatrix(value);for(int k=0;k<16;++k)changed|=std::abs(current[k]-proposed[k])>1e-6;
    }
    if(changed){
        if(!m_Transaction)BeginTransaction("Group Transform",group.before);
        // All targets were validated before the first component mutation.
        std::vector<std::pair<ecs::Entity,ecs::TransformComponent>> edits;for(std::size_t i=0;i<group.entities.size();++i)edits.emplace_back(group.entities[i],(*result)[i]);
        if(!app.GetWorld().SetWorldTransforms(edits,&m_EditorError))return false;
        for(auto e:group.entities)app.NotifyEditorMutation(e);
    }
    group.current=target;return true;
}
void EditorLayer::EndGroupTransform(Application& app,bool cancel){
    if(!m_GroupTransform)return;
    if(m_GroupTransform->before.world!=app.GetWorld().GetIdentity()||std::any_of(m_GroupTransform->entities.begin(),m_GroupTransform->entities.end(),[&](auto e){return !app.GetWorld().IsAlive(e)||!app.GetWorld().HasComponent<ecs::TransformComponent>(e);})){PruneSelection(app);return;}
    if(cancel)CancelTransaction(app);else FinishTransaction(app);m_GroupTransform.reset();m_GizmoUndoCaptured=false;
}
void EditorLayer::DrawGroupGizmo(Application& app,const float* view,const float* projection){
    // Idle reference follows selection. Once pressed, both the reference and all
    // object transforms are immutable baselines until release/cancel.
    ecs::TransformComponent reference;
    if(m_GroupTransform)reference=m_GroupTransform->pivot;
    else{
        double x=0,y=0,z=0;for(auto e:app.GetWorld().HierarchyRoots(m_Selection.items)){auto value=app.GetWorld().WorldTransform(e);auto* t=&value;if(!t||!ValidGroupSource(*t))return;x+=t->position.x;y+=t->position.y;z+=t->position.z;}
        double count=double(app.GetWorld().HierarchyRoots(m_Selection.items).size());reference.position={float(x/count),float(y/count),float(z/count)};
        if(m_GizmoMode==0)reference.rotation=app.GetWorld().WorldTransform(m_Selection.active).rotation;
    }
    auto current=m_GroupTransform?m_GroupTransform->current:TransformMatrix(reference);float matrix[16];for(int i=0;i<16;++i)matrix[i]=float(current[i]);
    const auto operation=m_GizmoOperation==0?ImGuizmo::TRANSLATE:m_GizmoOperation==1?ImGuizmo::ROTATE:ImGuizmo::SCALE;
    float value=m_GizmoOperation==0?m_SnapTranslation:m_GizmoOperation==1?m_SnapRotation:m_SnapScale;float snap[3]={value,value,value};
    const bool changed=ImGuizmo::Manipulate(view,projection,operation,m_GizmoMode==0?ImGuizmo::LOCAL:ImGuizmo::WORLD,matrix,nullptr,(m_GizmoOperation==1?m_RotationSnapping:m_Snapping)?snap:nullptr);
    if(ImGuizmo::IsUsing()){
        if(!m_GroupTransform&&!BeginGroupTransform(app,m_GizmoMode==0))return;
        if(changed&&ImGui::IsMouseDragging(0,.5f*ImGui::GetStyle().FontScaleDpi)){
            ImportMatrix target;std::copy(matrix,matrix+16,target.begin());
            bool matrixChanged=false;for(int i=0;i<16;++i)matrixChanged|=std::abs(target[i]-m_GroupTransform->current[i])>1e-6;
            if(!matrixChanged)return;
            if(!m_GizmoUndoCaptured){
                if(m_GizmoOperation==0&&ImGui::GetIO().KeyAlt){
                    if(!BeginDuplicateDrag(app).IsValid())return;
                    // Duplicate retargets the gesture while keeping its original
                    // pivot and immutable source transforms.
                }
                m_GizmoUndoCaptured=true;
            }
            UpdateGroupTransform(app,target);
        }
    }else if(m_GroupTransform)EndGroupTransform(app);
}
}
