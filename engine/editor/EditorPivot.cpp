#include "EditorLayer.h"
#include "../core/Application.h"
#include "../resources/ResourceManager.h"
#include "LightGizmoMath.h"
#include <imgui.h>
#include <ImGuizmo.h>
namespace editor {
bool EditorLayer::SetEntityPivot(Application& app,ecs::Entity e,ecs::Vec3 pivot,bool finish){
    if(app.IsEditorPlayMode()||!app.GetWorld().IsAlive(e)||!app.GetWorld().HasComponent<ecs::TransformComponent>(e))return false;
    for(float v:{pivot.x,pivot.y,pivot.z})if(!std::isfinite(v)||std::abs(v)>1e6f){m_EditorError="Pivot must be finite within 1000000 units";return false;}
    auto old=app.GetWorld().LocalTransform(e);EntitySnapshot before;before.entity=e;before.world=e.world;before.alive=true;before.transform=old;before.parent=app.GetWorld().Parent(e);before.selection=m_Selection;
    auto target=ecs::RebasePivot(old,pivot);if(old.pivot.x==pivot.x&&old.pivot.y==pivot.y&&old.pivot.z==pivot.z)return true;
    if(!app.GetWorld().SetLocalTransform(e,target,&m_EditorError))return false;
    if(!m_Transaction)BeginTransaction("Edit Pivot",std::move(before));app.NotifyEditorMutation(e);if(finish)FinishTransaction(app);return true;
}
bool EditorLayer::ResetEntityPivot(Application& app,ecs::Entity e){return SetEntityPivot(app,e,{});}
bool EditorLayer::CenterEntityPivot(Application& app,ecs::Entity e){
    auto* m=app.GetWorld().GetComponent<ecs::MeshRendererComponent>(e);auto* resources=app.GetResourceManager();auto mesh=m&&resources?resources->Get<MeshResource>(m->meshPath):ResourceHandle<MeshResource>{};
    if(!mesh||!mesh->IsLoaded()||mesh->GetData().placeholder||mesh->GetData().meshData.vertices.empty()){m_EditorError="Pivot center needs a loaded Mesh";return false;}
    const auto b=MeshPlacementBounds(mesh->GetData().meshData);return SetEntityPivot(app,e,{(b.min.x+b.max.x)*.5f,(b.min.y+b.max.y)*.5f,(b.min.z+b.max.z)*.5f});
}
void EditorLayer::DrawPivotGizmo(Application& app,const float* view,const float* projection,const ImVec2& origin,const ImVec2& size){
    if(m_Selection.items.size()!=1){ImGui::GetWindowDrawList()->AddText({origin.x+10,origin.y+size.y-24},IM_COL32(255,195,80,255),"Edit Pivot: select one entity.");return;}
    auto& world=app.GetWorld();const auto entity=m_Selection.active;auto absolute=world.WorldTransform(entity);auto point=ecs::PivotPosition(absolute);auto reference=absolute;reference.pivot={};reference.position=point;reference.scale={1,1,1};
    float matrix[16];auto model=TransformMatrix(reference);for(int i=0;i<16;++i)matrix[i]=float(model[i]);
    const bool changed=ImGuizmo::Manipulate(view,projection,ImGuizmo::TRANSLATE,m_GizmoMode==0?ImGuizmo::LOCAL:ImGuizmo::WORLD,matrix);
    if(changed&&ImGuizmo::IsUsing()&&ImGui::IsMouseDragging(0,.5f)){
        // Invert the current geometry's linear map: moving its local pivot must
        // not change that map or the geometry/children it positions.
        auto m=TransformMatrix(absolute);const double d=m[0]*(m[5]*m[10]-m[9]*m[6])-m[4]*(m[1]*m[10]-m[9]*m[2])+m[8]*(m[1]*m[6]-m[5]*m[2]);
        if(std::abs(d)>1e-12){const double x=matrix[12]-point.x,y=matrix[13]-point.y,z=matrix[14]-point.z;auto local=world.LocalTransform(entity).pivot;
            local.x+=float(((m[5]*m[10]-m[9]*m[6])*x+(m[8]*m[6]-m[4]*m[10])*y+(m[4]*m[9]-m[8]*m[5])*z)/d);
            local.y+=float(((m[9]*m[2]-m[1]*m[10])*x+(m[0]*m[10]-m[8]*m[2])*y+(m[8]*m[1]-m[0]*m[9])*z)/d);
            local.z+=float(((m[1]*m[6]-m[5]*m[2])*x+(m[4]*m[2]-m[0]*m[6])*y+(m[0]*m[5]-m[4]*m[1])*z)/d);
            if(SetEntityPivot(app,entity,local,false))m_GizmoUndoCaptured=true;
        }
    }else if(!ImGuizmo::IsUsing()&&m_GizmoUndoCaptured){FinishTransaction(app);m_GizmoUndoCaptured=false;}
    const auto camera=GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),size.x/size.y,app.GetCameraNearPlane(),app.GetCameraFarPlane());
    if(auto p=ProjectGizmo(ecs::PivotPosition(world.WorldTransform(entity)),camera))ImGui::GetWindowDrawList()->AddCircle({origin.x+(*p)[0]*size.x,origin.y+(*p)[1]*size.y},8,IM_COL32(255,175,65,255),20,2);
    ImGui::GetWindowDrawList()->AddText({origin.x+10,origin.y+size.y-24},IM_COL32(255,195,80,255),"EDIT PIVOT: geometry stays fixed. Escape cancels; exit mode to Rotate/Scale.");
}
}
