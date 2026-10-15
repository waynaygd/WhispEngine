#pragma once
#include "LightGizmoMath.h"
#include <imgui.h>
#include <array>
#include <cfloat>
namespace editor {
inline constexpr float ScaleGizmoClipSize=.1f; // ImGuizmo's default NDC size.
inline constexpr int ScalePlaneAxes[3][2]={{0,1},{0,2},{1,2}};
struct ScalePlanePolygon {std::array<ImVec2,4> points{};bool visible=false;};
struct ScalePlaneGeometry {
 ImVec2 center{};std::array<ImVec2,3> ends{};std::array<ScalePlanePolygon,3> planes{};bool valid=false;
};
inline float ScaleCross(ImVec2 a,ImVec2 b,ImVec2 c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
inline bool HitScalePlane(const ScalePlanePolygon& polygon,ImVec2 mouse){
 if(!polygon.visible||!std::isfinite(mouse.x)||!std::isfinite(mouse.y))return false;
 for(int i=0;i<4;++i)if(ScaleCross(polygon.points[i],polygon.points[(i+1)%4],mouse)<0)return false;return true;
}
inline float ScaleSegmentDistance(ImVec2 p,ImVec2 a,ImVec2 b){
 float dx=b.x-a.x,dy=b.y-a.y,n=dx*dx+dy*dy;
 float t=n>1e-8f?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/n,0.f,1.f):0;
 return std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);
}
inline ScalePlaneGeometry BuildScalePlaneGeometry(const float* model,const LightCamera& camera,ImVec2 origin,ImVec2 size,float dpi=1){
 ScalePlaneGeometry result;if(size.x<=1||size.y<=1||!std::isfinite(dpi)||dpi<=0)return result;
 using V=ecs::Vec3;V position{model[12],model[13],model[14]};
 auto add=[](V a,V b,float scale){return V{a.x+b.x*scale,a.y+b.y*scale,a.z+b.z*scale};};
 // Project without viewport clipping for the one-unit direction probes used by
 // ImGuizmo's axis-flip decision. Screen vertices are clipped separately below.
 auto ndc=[&](V p)->std::optional<ImVec2>{const auto& m=camera.view;float x=m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],y=m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],z=m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14];if(!std::isfinite(z)||std::abs(z)<FLT_EPSILON)return {};ImVec2 q{camera.projection[0]*x/z,camera.projection[5]*y/z};if(!std::isfinite(q.x)||!std::isfinite(q.y))return {};return q;};
 auto project=[&](V p)->std::optional<ImVec2>{auto q=ProjectGizmo(p,camera);if(!q)return {};return ImVec2(origin.x+(*q)[0]*size.x,origin.y+(*q)[1]*size.y);};
 auto center=project(position),zero=ndc(position);if(!center||!zero)return result;result.center=*center;
 const auto& v=camera.view;float depth=v[2]*position.x+v[6]*position.y+v[10]*position.z+v[14],aspect=size.x/size.y;
 // ComputeContext normalizes projected camera-right in aspect-corrected NDC.
 // Thus an unforeshortened axis spans 0.05 * max(viewport width,height).
 float rightLength=std::abs(camera.projection[0]/depth)*std::min(1.f,aspect);
 if(!std::isfinite(rightLength)||rightLength<1e-9f)return result;
 float length=ScaleGizmoClipSize/rightLength;std::array<V,3> directions;
 auto segment=[&](V p)->std::optional<float>{auto q=ndc(p);if(!q)return {};float x=q->x-zero->x,y=q->y-zero->y;if(aspect<1)x*=aspect;else y/=aspect;return std::hypot(x,y);};
 for(int axis=0;axis<3;++axis){
  V d{model[axis*4],model[axis*4+1],model[axis*4+2]};float n=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
  if(!std::isfinite(n)||n<1e-6f)return result;d={d.x/n,d.y/n,d.z/n}; // signed OrthoNormalize, exactly as ImGuizmo
  auto positive=segment(add(position,d,1)),negative=segment(add(position,d,-1));if(!positive||!negative)return result;
  if(*positive<*negative&&std::abs(*positive-*negative)>FLT_EPSILON)d={-d.x,-d.y,-d.z};
  directions[axis]=d;auto end=project(add(position,d,length));if(!end)return result;result.ends[axis]=*end;
 }
 result.valid=true;
 for(int plane=0;plane<3;++plane){auto& polygon=result.planes[plane];int a=ScalePlaneAxes[plane][0],b=ScalePlaneAxes[plane][1];
  // The outer edge stays inside axis length. Inset the inner edge only as far
  // as needed to clear ImGuizmo's fixed 12px axis pick radius and 10px center
  // square. No per-view offsets; ambiguous/edge-on planes are hidden.
  for(float inner=.28f;inner<.85f;inner+=.04f){bool valid=true;
   for(int i=0;i<4;++i){float u=(i==1||i==2)?.85f:inner,w=i>=2?.85f:inner;auto q=project(add(add(position,directions[a],length*u),directions[b],length*w));if(!q){valid=false;break;}polygon.points[i]=*q;}
   if(!valid)continue;float area=0;for(int i=0;i<4;++i){auto p=polygon.points[i],q=polygon.points[(i+1)%4];area+=p.x*q.y-p.y*q.x;}
   if(!std::isfinite(area)||std::abs(area)<50*dpi*dpi)continue;
   if(area<0)std::reverse(polygon.points.begin(),polygon.points.end());
   polygon.visible=true;
   if(HitScalePlane(polygon,*center))valid=false;
   for(int i=0;i<4&&valid;++i){auto p=polygon.points[i],q=polygon.points[(i+1)%4];
    if(ScaleSegmentDistance(*center,p,q)<15)valid=false;
    for(int axis=0;axis<3;++axis){auto end=result.ends[axis];ImVec2 base{center->x+(end.x-center->x)*.1f,center->y+(end.y-center->y)*.1f};
     // Test both segments' endpoints; crossings are also excluded by checking
     // the axis endpoints against the polygon and its side orientation.
     if(ScaleSegmentDistance(p,base,end)<13||ScaleSegmentDistance(q,base,end)<13||ScaleSegmentDistance(base,p,q)<13||ScaleSegmentDistance(end,p,q)<13||(ScaleCross(p,q,base)*ScaleCross(p,q,end)<0&&ScaleCross(base,end,p)*ScaleCross(base,end,q)<0))valid=false;
    }
   }
   if(valid)break;polygon.visible=false;
  }
 }
 return result;
}
}
