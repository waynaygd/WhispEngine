#include "EditorLayer.h"
#include "TransformMath.h"
#include "ScalePlaneGeometry.h"
#include "../core/Application.h"
#include <imgui.h>
#include <imgui_internal.h>
namespace editor {
bool EditorLayer::DrawScalePlanes(Application& app,const ImVec2& origin,const ImVec2& size,const float* model){
 auto* t=app.GetWorld().GetComponent<ecs::TransformComponent>(m_Selection.active);if(!t||m_GizmoOperation!=2)return false;
 if(m_PlaneScaleCaptured){
  if(!ImGui::IsMouseDown(0)){FinishTransaction(app);m_PlaneScaleCaptured=false;}
  else {auto mouse=ImGui::GetMousePos();float amount=((mouse.x-m_ScaleMouse[0])*m_ScaleDirection[0]+(mouse.y-m_ScaleMouse[1])*m_ScaleDirection[1])/(100*ImGui::GetStyle().FontScaleDpi);
   t->scale=ScalePlane(m_ScaleOriginal.scale,m_ScalePlane,std::max(.001f,1+amount),m_Snapping?m_SnapScale:0);app.NotifyEditorMutation(m_Selection.active);}
 }
 const auto camera=GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),size.x/size.y,app.GetCameraNearPlane(),app.GetCameraFarPlane());
 auto geometry=BuildScalePlaneGeometry(model,camera,origin,size,ImGui::GetStyle().FontScaleDpi);
 auto mouse=ImGui::GetMousePos();int hovered=-1;float nearest=INFINITY;
 const bool canHover=m_ViewportHovered&&!m_InputTextActive&&!m_ModalActive&&!app.IsCameraControlActive()&&!ImGui::IsMouseDown(1);
 if(canHover&&!m_PlaneScaleCaptured)for(int plane=0;plane<3;++plane)if(HitScalePlane(geometry.planes[plane],mouse)){
  const auto& p=geometry.planes[plane].points;float distance=std::hypot(mouse.x-(p[0].x+p[2].x)*.5f,mouse.y-(p[0].y+p[2].y)*.5f);
  if(distance<nearest){nearest=distance;hovered=plane;}
 }
 auto* draw=ImGui::GetWindowDrawList();const ImU32 colors[3]={IM_COL32(225,205,60,95),IM_COL32(220,80,160,95),IM_COL32(70,200,210,95)};
 for(int plane=0;plane<3;++plane){const auto& polygon=geometry.planes[plane];if(!polygon.visible)continue;bool highlight=m_PlaneScaleCaptured?m_ScalePlane==plane:hovered==plane;
  draw->AddConvexPolyFilled(polygon.points.data(),4,highlight?IM_COL32(255,220,80,175):colors[plane]);
  draw->AddPolyline(polygon.points.data(),4,highlight?IM_COL32(255,255,180,255):colors[plane],ImDrawFlags_Closed,1.5f);
 }
 if(hovered>=0){ImGui::SetTooltip("Scale %s (local axes); one drag = one Undo. Negative signs preserved.",hovered==0?"XY":hovered==1?"XZ":"YZ");
  if(ImGui::IsMouseClicked(0)){PushUndoBeforeChange(app,"Plane scale");m_PlaneScaleCaptured=true;m_ScalePlane=hovered;m_ScaleOriginal=*t;m_ScaleMouse={mouse.x,mouse.y};
   const auto& points=geometry.planes[hovered].points;ImVec2 c{(points[0].x+points[2].x)*.5f,(points[0].y+points[2].y)*.5f};float dx=c.x-geometry.center.x,dy=c.y-geometry.center.y,n=std::hypot(dx,dy);m_ScaleDirection={dx/std::max(1.f,n),dy/std::max(1.f,n)};
  }
 }
 return m_PlaneScaleCaptured||hovered>=0;
}
}