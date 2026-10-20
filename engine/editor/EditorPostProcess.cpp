#include "EditorLayer.h"
#include "LightGizmoMath.h"
#include "../core/Application.h"
#include "../ecs/PostProcessEvaluation.h"
#include <imgui.h>
namespace editor {
ecs::Entity EditorLayer::CreatePostProcessVolume(Application& app,bool global){
    if(app.IsEditorPlayMode())return {};FinishTransaction(app);auto previous=m_Selection.active;
    auto entity=app.CreateEmptyEntity(global?"Global Post Process Volume":"Local Post Process Volume");
    auto& volume=app.GetWorld().AddComponent<ecs::PostProcessVolumeComponent>(entity);volume.global=global;
    // Minimal working starting point: existing Distance Fog, not a second fog implementation.
    volume.fogEnabled=true;volume.fogDensity=.65f;volume.fogStart=2.f;volume.fogEnd=30.f;
    SelectEntityFromUser(app,entity);EntitySnapshot before;before.entity=entity;before.world=entity.world;before.previousSelection=previous;CommitUndo(app,"Create Post Process Volume",before);return entity;
}
void EditorLayer::DrawPostProcessGizmos(Application& app,const ImVec2& origin,const ImVec2& size){
    if(app.IsEditorPlayMode()||m_GameView||!m_VisualSettings.enabled||size.x<=1||size.y<=1)return;
    auto& world=app.GetWorld();auto camera=GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),size.x/size.y,app.GetCameraNearPlane(),app.GetCameraFarPlane());
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
    for(auto entity:m_Selection.items){const auto* volume=world.GetComponent<ecs::PostProcessVolumeComponent>(entity);if(!volume||volume->global||!EditorEntityVisible(app,entity))continue;auto box=ecs::GetPostProcessBox(world,entity,*volume);if(!box)continue;
        std::array<ecs::Vec3,8> points;for(int corner=0;corner<8;++corner){points[corner]=box->center;for(int axis=0;axis<3;++axis){float sign=corner&(1<<axis)?1.f:-1.f;const auto& v=box->axes[axis];points[corner].x+=v.x*box->half[axis]*sign;points[corner].y+=v.y*box->half[axis]*sign;points[corner].z+=v.z*box->half[axis]*sign;}}
        for(int corner=0;corner<8;++corner)for(int axis=0;axis<3;++axis)if(!(corner&(1<<axis))){auto a=ProjectGizmo(points[corner],camera),b=ProjectGizmo(points[corner|(1<<axis)],camera);if(!a||!b)continue;
            draw->AddLine({origin.x+(*a)[0]*size.x,origin.y+(*a)[1]*size.y},{origin.x+(*b)[0]*size.x,origin.y+(*b)[1]*size.y},volume->enabled?IM_COL32(240,190,70,225):IM_COL32(130,130,130,180),1.5f);
        }
    }
    draw->PopClipRect();
}
}
