#include "EditorLayer.h"
#include "LightGizmoMath.h"
#include "../core/Application.h"
#include "../ecs/components/FogComponent.h"
#include <imgui.h>
#include <ImGuizmo.h>
namespace editor {
void EditorLayer::DrawFogGizmos(Application& app,const ImVec2& origin,const ImVec2& size){
    if(app.IsEditorPlayMode()||m_GameView||!m_VisualSettings.enabled||size.x<=1||size.y<=1)return;
    auto camera=GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),size.x/size.y,app.GetCameraNearPlane(),app.GetCameraFarPlane());
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
    ecs::Entity hit{};float nearest=INFINITY;const auto mouse=ImGui::GetMousePos();const float dpi=ImGui::GetStyle().FontScaleDpi;
    app.GetWorld().ForEach<ecs::FogComponent,ecs::TransformComponent>([&](auto e,const auto& fog,const auto& transform){
        if(!EditorEntityVisible(app,e))return;
        auto projected=ProjectGizmo(app.GetWorld().WorldTransform(e).position,camera);if(!projected)return;
        ImVec2 p(origin.x+(*projected)[0]*size.x,origin.y+(*projected)[1]*size.y);
        if(p.x<origin.x-20*dpi||p.y<origin.y-20*dpi||p.x>origin.x+size.x+20*dpi||p.y>origin.y+size.y+20*dpi)return;
        const ImU32 ink=fog.enabled?IM_COL32(210,238,250,255):IM_COL32(145,150,155,255),outline=IM_COL32(10,16,24,255);
        // A cloud silhouette with three lobes and horizontal mist strokes. Dark
        // backing and pale foreground remain legible on both sky and interiors.
        for(int pass=0;pass<2;++pass){float r=(pass?0.f:2.f)*dpi;auto color=pass?ink:outline;
            for(auto circle:std::array<std::array<float,3>,3>{{{-9,0,6},{0,-5,9},{10,-1,7}}})draw->AddCircleFilled(ImVec2(p.x+circle[0]*dpi,p.y+circle[1]*dpi),(circle[2]*dpi)+r,color,20);
            draw->AddRectFilled(ImVec2(p.x-12*dpi-r,p.y-r),ImVec2(p.x+13*dpi+r,p.y+6*dpi+r),color,2*dpi);
        }
        for(int row=0;row<2;++row){float y=p.y+(11+row*5)*dpi,w=(row?9:14)*dpi;draw->AddLine(ImVec2(p.x-w,p.y+(11+row*5)*dpi),ImVec2(p.x+w,y),outline,4*dpi);draw->AddLine(ImVec2(p.x-w,y),ImVec2(p.x+w,y),ink,1.5f*dpi);}
        if(IsSelected(e)){draw->AddRect(ImVec2(p.x-20*dpi,p.y-18*dpi),ImVec2(p.x+21*dpi,p.y+20*dpi),IM_COL32(255,225,80,255),5*dpi,0,1.5f*dpi);draw->AddText(ImVec2(p.x+25*dpi,p.y-7*dpi),ink,"Fog (global)");}
        if(CanViewportSelect(app,e)&&std::abs(mouse.x-p.x)<=20*dpi&&mouse.y>=p.y-18*dpi&&mouse.y<=p.y+20*dpi&&(*projected)[2]<nearest){hit=e;nearest=(*projected)[2];}
    });
    m_SpecialIconHovered |= hit.IsValid();
    if(hit.IsValid()&&!m_LightIconClicked&&!app.IsCameraControlActive()&&!ImGui::IsMouseDown(1)&&m_ViewportHovered&&!m_InputTextActive&&!m_ModalActive&&!m_LightPointerCaptured&&!m_PlaneScaleCaptured&&ImGui::IsMouseClicked(0)&&(!m_Selection.active.IsValid()||!ImGuizmo::IsOver()||ImGui::GetIO().KeyCtrl||ImGui::GetIO().KeyShift)&&!ImGuizmo::IsUsing()&&!ImGui::IsAnyItemActive()){
        SelectViewportEntity(app,hit,ImGui::GetIO().KeyCtrl,ImGui::GetIO().KeyShift);m_LightIconClicked=true;
    }
    draw->PopClipRect();
}
}
