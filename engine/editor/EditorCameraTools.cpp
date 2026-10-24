#include "EditorLayer.h"
#include "CameraEditorMath.h"
#include "LightGizmoMath.h"
#include "../core/Application.h"
#include <imgui.h>
#include <ImGuizmo.h>
namespace editor {
void EditorLayer::DrawCameraPreview(Application& app,ecs::Entity entity){
    if(app.IsEditorPlayMode()||(!ActiveDocumentIs(DocumentType::Scene)&&!ActiveDocumentIs(DocumentType::Prefab)))return;
    auto* settings=app.GetWorld().GetComponent<ecs::CameraComponent>(entity);
    if(!settings)return;
    ImGui::SeparatorText("Camera Preview");
    ImGui::Checkbox("Camera Gizmos",&m_ShowCameraGizmos);ImGui::SameLine();ImGui::Checkbox("Frustum",&m_ShowCameraFrustum);
    ImGui::Checkbox("Live Preview",&m_ShowCameraPreview);
    ImGui::TextWrapped("Frustum: preview aspect when visible, Scene View aspect otherwise. Display far is capped at max(near, 200); camera settings stay unchanged.");
    if(!m_ShowCameraPreview)return;
    auto* renderer=app.GetRenderAdapter();
    if(!renderer||!renderer->SupportsCameraPreview()){ImGui::TextWrapped("Camera Preview is available on DX12.");return;}
    if(!settings->enabled||!ecs::ValidCamera(*settings)||!app.GetWorld().HasComponent<ecs::TransformComponent>(entity)){
        ImGui::TextWrapped("Enable a valid Camera with Transform to preview.");return;
    }
    try{(void)ecs::BuildCameraState(ecs::ResolveCameraPose(app.GetWorld(),entity),*settings,1.f);}
    catch(const std::exception& failure){ImGui::TextWrapped("Cannot preview Camera: %s",failure.what());return;}
    float width=std::clamp(ImGui::GetContentRegionAvail().x,64.f,1024.f),height=width*9.f/16.f;
    if(!ImGui::IsRectVisible({width,height})){ImGui::Dummy({width,height});return;}
    if(m_CameraPreviewSource!=entity){renderer->ReleaseCameraPreview();m_CameraPreviewSource=entity;}
    m_CameraPreviewRequest={entity,int(width),int(height)};
    if(auto texture=renderer->GetCameraPreviewTextureId())ImGui::Image((ImTextureID)texture,{width,height});
    else ImGui::Dummy({width,height});
    ImGui::TextWrapped("Live: one scene pass every 4 frames. Scene View holds its last image during that pass.");
}
void EditorLayer::DrawCameraGizmos(Application& app,const ImVec2& origin,const ImVec2& size){
    if(app.IsEditorPlayMode()||m_GameView||!m_ShowCameraGizmos||size.x<=1||size.y<=1)return;
    auto view=GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),size.x/size.y,app.GetCameraNearPlane(),app.GetCameraFarPlane());
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
    const auto mouse=ImGui::GetMousePos();ecs::Entity hit{};float nearest=INFINITY;
    app.GetWorld().ForEach<ecs::CameraComponent,ecs::TransformComponent>([&](auto entity,const auto& settings,const auto&){
        if(!EditorEntityVisible(app,entity)||!ecs::ValidCamera(settings))return;
        try{
            const bool selected=IsSelected(entity);float aspect=size.x/size.y;
            if(m_CameraPreviewRequest.entity==entity)aspect=float(m_CameraPreviewRequest.width)/m_CameraPreviewRequest.height;
            auto state=ecs::BuildCameraState(ecs::ResolveCameraPose(app.GetWorld(),entity),settings,aspect,entity);
            auto ink=selected?IM_COL32(255,230,85,255):settings.enabled?IM_COL32(110,200,255,255):IM_COL32(130,140,150,180);
            auto line=[&](ecs::Vec3 a,ecs::Vec3 b){if(auto p=ProjectEditorLine(a,b,view))draw->AddLine({origin.x+(*p)[0]*size.x,origin.y+(*p)[1]*size.y},{origin.x+(*p)[2]*size.x,origin.y+(*p)[3]*size.y},ink,selected?2.f:1.4f);};
            auto point=ProjectGizmo(state.pose.position,view);
            float dx=state.pose.position.x-app.GetCameraPosition().x,dy=state.pose.position.y-app.GetCameraPosition().y,dz=state.pose.position.z-app.GetCameraPosition().z;
            float scale=std::clamp(std::sqrt(dx*dx+dy*dy+dz*dz)*std::tan(app.GetCameraVerticalFovRadians()*.5f)*32.f/size.y,.06f,20.f);
            auto local=[&](float x,float y,float z){return CameraPoint(state,x*scale,y*scale,z*scale);};
            // Procedural camera body, lens pyramid, top/up marker and optical axis.
            for(int z=0;z<2;++z){auto a=local(-1,-.65f,float(z)-1),b=local(1,-.65f,float(z)-1),c=local(1,.65f,float(z)-1),d=local(-1,.65f,float(z)-1);line(a,b);line(b,c);line(c,d);line(d,a);}
            for(float x:{-1.f,1.f})for(float y:{-.65f,.65f}){line(local(x,y,-1),local(x,y,0));line(local(x*.5f,y*.5f,0),local(x,y,1));}
            line(local(-1,-.65f,1),local(1,-.65f,1));line(local(1,-.65f,1),local(1,.65f,1));line(local(1,.65f,1),local(-1,.65f,1));line(local(-1,.65f,1),local(-1,-.65f,1));
            line(local(0,0,1),local(0,0,2.5f));line(local(-.5f,.65f,-.5f),local(0,1.3f,-.5f));line(local(0,1.3f,-.5f),local(.5f,.65f,-.5f));
            if(selected&&m_ShowCameraFrustum){auto frustum=BuildCameraFrustum(state);for(int j=0;j<4;++j){line(frustum.corners[j],frustum.corners[(j+1)%4]);line(frustum.corners[j+4],frustum.corners[4+(j+1)%4]);line(frustum.corners[j],frustum.corners[j+4]);}}
            if(point){ImVec2 screen{origin.x+(*point)[0]*size.x,origin.y+(*point)[1]*size.y};draw->AddCircle(screen,selected?11.f:8.f,ink,16,1.5f);
                if(CanViewportSelect(app,entity)&&HitLightIcon(mouse.x-origin.x,mouse.y-origin.y,(*point)[0],(*point)[1],size.x,size.y)&&(*point)[2]<nearest){nearest=(*point)[2];hit=entity;}}
        }catch(const std::exception&){/* Invalid native data must not break the overlay. */}
    });
    m_SpecialIconHovered|=hit.IsValid();
    if(hit.IsValid()&&!m_LightIconClicked&&m_ViewportHovered&&!m_ModalActive&&!m_InputTextActive&&!m_LightPointerCaptured&&!app.IsCameraControlActive()&&!ImGui::IsMouseDown(1)&&ImGui::IsMouseClicked(0)&&!ImGuizmo::IsUsing()&&!ImGui::IsAnyItemActive()&&(!ImGuizmo::IsOver()||!m_Selection.active.IsValid()||ImGui::GetIO().KeyCtrl||ImGui::GetIO().KeyShift)){
        FinishTransaction(app);SelectViewportEntity(app,hit,ImGui::GetIO().KeyCtrl,ImGui::GetIO().KeyShift);m_LightIconClicked=true;
    }
    draw->PopClipRect();
}
}
