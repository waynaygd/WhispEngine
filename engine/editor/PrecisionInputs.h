#pragma once
#include <imgui.h>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <array>
namespace editor {
inline void NumericHint(double lo,double hi,bool invalid=false){
 if(invalid||ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)){
  ImGui::BeginTooltip();ImGui::TextUnformatted("Drag to adjust. Ctrl+click to type; Drag fields also accept double-click.");
  ImGui::TextUnformatted("Enter commits, Escape cancels. Display rounding does not round stored values.");
  if(lo<hi)ImGui::Text("Valid range: %.9g .. %.9g",lo,hi);else ImGui::TextUnformatted("Finite values required.");
  if(invalid)ImGui::TextColored(ImVec4(1,.45f,.25f,1),"Invalid value: previous valid value retained.");ImGui::EndTooltip();
 }
}
// Value snapshots only: no pointers into ECS/storage are retained across frames.
inline bool NumericCancel(ImGuiID id,float* values,int count,const float* previous,bool changed){
 static thread_local std::unordered_map<ImGuiID,std::array<float,3>> original;
 if(ImGui::IsItemActivated()){auto& saved=original[id];for(int i=0;i<count;++i)saved[i]=previous[i];}
 if(ImGui::IsKeyPressed(ImGuiKey_Escape,false)){auto found=original.find(id);if(found!=original.end()){for(int i=0;i<count;++i){changed|=values[i]!=found->second[i];values[i]=found->second[i];}}}
 if(ImGui::IsItemDeactivated())original.erase(id);return changed;
}
inline bool PrecisionDragFloat(const char* label,float* v,float speed=1,float lo=0,float hi=0,const char* format="%.6g",ImGuiSliderFlags flags=0){
 auto id=ImGui::GetID(label);float previous=*v;bool changed=ImGui::DragFloat(label,v,speed,lo,hi,format,flags|ImGuiSliderFlags_NoRoundToFormat);
 bool invalid=changed&&(!std::isfinite(*v)||(lo<hi&&(*v<lo||*v>hi)));if(invalid)*v=previous;changed=NumericCancel(id,v,1,&previous,changed&&!invalid);NumericHint(lo,hi,invalid);return changed;
}
inline bool PrecisionSliderFloat(const char* label,float* v,float lo,float hi,const char* format="%.6g",ImGuiSliderFlags flags=0){
 auto id=ImGui::GetID(label);float previous=*v;bool changed=ImGui::SliderFloat(label,v,lo,hi,format,flags|ImGuiSliderFlags_NoRoundToFormat);
 bool invalid=changed&&(!std::isfinite(*v)||*v<lo||*v>hi);if(invalid)*v=previous;changed=NumericCancel(id,v,1,&previous,changed&&!invalid);NumericHint(lo,hi,invalid);return changed;
}
inline bool PrecisionDragFloat3(const char* label,float* v,float speed=1,float lo=0,float hi=0,const char* format="%.6g",ImGuiSliderFlags flags=0){
 auto id=ImGui::GetID(label);float previous[3]={v[0],v[1],v[2]};bool changed=ImGui::DragFloat3(label,v,speed,lo,hi,format,flags|ImGuiSliderFlags_NoRoundToFormat);bool invalid=false;
 if(changed)for(int i=0;i<3;++i)invalid|=!std::isfinite(v[i])||(lo<hi&&(v[i]<lo||v[i]>hi));if(invalid)std::copy(previous,previous+3,v);changed=NumericCancel(id,v,3,previous,changed&&!invalid);NumericHint(lo,hi,invalid);return changed;
}
inline bool PrecisionSliderInt(const char* label,int* v,int lo,int hi,const char* format="%d",ImGuiSliderFlags flags=0){
 int previous=*v;bool changed=ImGui::SliderInt(label,v,lo,hi,format,flags);bool invalid=changed&&(*v<lo||*v>hi);if(invalid)*v=previous;NumericHint(lo,hi,invalid);return changed&&!invalid;
}
inline bool PrecisionDragInt(const char* label,int* v,float speed=1,int lo=0,int hi=0,const char* format="%d",ImGuiSliderFlags flags=0){
 int previous=*v;bool changed=ImGui::DragInt(label,v,speed,lo,hi,format,flags);bool invalid=changed&&lo<hi&&(*v<lo||*v>hi);if(invalid)*v=previous;NumericHint(lo,hi,invalid);return changed&&!invalid;
}
}
