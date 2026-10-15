#include "EditorLayer.h"
#include "TransformMath.h"
#include "LightGizmoMath.h"
#include "../core/Application.h"
#include <imgui.h>
#include <imgui_internal.h>
namespace editor {
bool EditorLayer::DrawScalePlanes(Application& app,const ImVec2& origin,const ImVec2& size){
 auto* t=app.GetWorld().GetComponent<ecs::TransformComponent>(m_SelectedEntity);if(!t)return false;
 if(m_PlaneScaleCaptured){
  if(!ImGui::IsMouseDown(0)){FinishTransaction(app);m_PlaneScaleCaptured=false;return false;}
  auto mouse=ImGui::GetMousePos();float amount=((mouse.x-m_ScaleMouse[0])*m_ScaleDirection[0]+(mouse.y-m_ScaleMouse[1])*m_ScaleDirection[1])/(100*ImGui::GetStyle().FontScaleDpi);
  t->scale=ScalePlane(m_ScaleOriginal.scale,m_ScalePlane,std::max(.001f,1+amount),m_Snapping?m_SnapScale:0);app.NotifyEditorMutation(m_SelectedEntity);return true;
 }
 if(m_GizmoOperation!=2)return false;
 const auto camera=GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),size.x/size.y,app.GetCameraNearPlane(),app.GetCameraFarPlane());
 auto project=[&](ecs::Vec3 p)->std::optional<ImVec2>{auto q=ProjectGizmo(p,camera);if(!q)return {};return ImVec2(origin.x+(*q)[0]*size.x,origin.y+(*q)[1]*size.y);};
 auto center=project(t->position);if(!center)return false;
 float model[16];auto pose=ecs::RenderPose::From(*t);pose.scale={1,1,1};ecs::BuildRenderModelMatrix(model,pose);
 ecs::Vec3 eye=app.GetCameraPosition(),delta{t->position.x-eye.x,t->position.y-eye.y,t->position.z-eye.z};float depth=camera.view[2]*delta.x+camera.view[6]*delta.y+camera.view[10]*delta.z;
 float length=std::max(.001f,depth*std::tan(app.GetCameraVerticalFovRadians()*.5f)*.24f);
 const int axes[3][2]={{0,1},{0,2},{1,2}};auto* draw=ImGui::GetWindowDrawList();auto mouse=ImGui::GetMousePos();
 for(int plane=0;plane<3;++plane){int a=axes[plane][0],b=axes[plane][1];ImVec2 points[4];bool valid=true;
  for(int i=0;i<4;++i){float u=(i==1||i==2)?.55f:.30f,v=i>=2?.55f:.30f;auto p=t->position;p.x+=length*(model[a*4]*u+model[b*4]*v);p.y+=length*(model[a*4+1]*u+model[b*4+1]*v);p.z+=length*(model[a*4+2]*u+model[b*4+2]*v);auto q=project(p);if(!q){valid=false;break;}points[i]=*q;}
  if(!valid)continue;float area=0;for(int i=0;i<4;++i){auto p=points[i],q=points[(i+1)%4];area+=p.x*q.y-p.y*q.x;}if(std::abs(area)<30)continue;if(area<0)std::reverse(points,points+4);
  bool inside=true;float sign=0;for(int i=0;i<4;++i){auto p=points[i],q=points[(i+1)%4];float cross=(q.x-p.x)*(mouse.y-p.y)-(q.y-p.y)*(mouse.x-p.x);if(i==0)sign=cross;else if(cross*sign<0)inside=false;}
  bool hovered=inside&&m_ViewportHovered&&!m_InputTextActive&&!m_ModalActive&&!app.IsCameraControlActive()&&!ImGui::IsMouseDown(1);
  const ImU32 colors[3]={IM_COL32(225,205,60,95),IM_COL32(220,80,160,95),IM_COL32(70,200,210,95)};draw->AddConvexPolyFilled(points,4,hovered?IM_COL32(255,220,80,175):colors[plane]);draw->AddPolyline(points,4,hovered?IM_COL32(255,255,180,255):colors[plane],ImDrawFlags_Closed,1.5f);
  if(hovered){ImGui::SetTooltip("Scale %s (local axes); one drag = one Undo. Negative signs preserved.",plane==0?"XY":plane==1?"XZ":"YZ");
   if(ImGui::IsMouseClicked(0)){PushUndoBeforeChange(app,"Plane scale");m_PlaneScaleCaptured=true;m_ScalePlane=plane;m_ScaleOriginal=*t;m_ScaleMouse={mouse.x,mouse.y};ImVec2 c{(points[0].x+points[2].x)*.5f,(points[0].y+points[2].y)*.5f};float dx=c.x-center->x,dy=c.y-center->y,n=std::hypot(dx,dy);m_ScaleDirection={dx/std::max(1.f,n),dy/std::max(1.f,n)};return true;}
  }
 }
 return false;
}
}
