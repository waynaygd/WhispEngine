#include "LuaCodeEditor.h"
#include "EditorTheme.h"
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <cmath>
namespace editor {
bool LuaCodeEditor::Draw(bool readOnly){
 auto& io=ImGui::GetIO();auto* parent=ImGui::GetCurrentWindow();const auto id=parent->GetID("##LuaSource");
 const bool owns=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
 if(owns&&io.KeyCtrl&&!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)){
  if(ImGui::IsKeyPressed(ImGuiKey_F,false)||ImGui::IsKeyPressed(ImGuiKey_H,false)){showFind=focusFind=true;showReplace=ImGui::IsKeyPressed(ImGuiKey_H,false);}
  if(!readOnly&&(!io.WantTextInput||ImGui::GetActiveID()==id)&&(ImGui::IsKeyPressed(ImGuiKey_Z,false)||ImGui::IsKeyPressed(ImGuiKey_Y,false)))Undo(io.KeyShift||ImGui::IsKeyPressed(ImGuiKey_Y,false));
 }
 if(showFind){
  if(focusFind){ImGui::SetKeyboardFocusHere();focusFind=false;}
  ImGui::SetNextItemWidth(220);bool enter=ImGui::InputText("Find",&search,ImGuiInputTextFlags_EnterReturnsTrue);ImGui::SameLine();if(ImGui::Button("Next match")||enter)FindNext();ImGui::SameLine();if(ImGui::SmallButton("Close find"))showFind=false;
  if(showReplace){ImGui::SetNextItemWidth(220);ImGui::InputText("Replace with",&replacement);ImGui::SameLine();ImGui::BeginDisabled(readOnly);if(ImGui::Button("Replace match"))ReplaceMatch();ImGui::EndDisabled();}
 }
 if(!readOnly&&ImGui::GetActiveID()==id&&io.KeyShift&&ImGui::IsKeyPressed(ImGuiKey_Tab,ImGuiInputFlags_None,id)){Indent(true);ImGui::SetKeyOwner(ImGuiKey_Tab,id);}
 if(refreshInput){input=text;if(auto* state=ImGui::GetInputTextState(id)){state->ReloadUserBufAndKeepSelection();state->ReloadSelectionStart=selectionStart;state->ReloadSelectionEnd=selectionEnd;state->CursorFollow=true;}else if(focusCode)selectOnActivate=true;refreshInput=false;}
 auto& rows=Lines();const float height=ImGui::GetFontSize();float gutter=ImGui::CalcTextSize(std::to_string(rows.size()).c_str()).x+18;
 if(metricRevision!=revision||metricFontSize!=height){maxLineWidth=0;for(std::size_t i=0;i<rows.size();++i){auto end=i+1<rows.size()?rows[i+1]-1:text.size();maxLineWidth=std::max(maxLineWidth,ImGui::CalcTextSize(text.data()+rows[i],text.data()+end).x);}metricRevision=revision;metricFontSize=height;}
 auto origin=ImGui::GetCursorScreenPos();float available=ImGui::GetContentRegionAvail().x;
 ImGui::SetCursorScreenPos({origin.x+gutter,origin.y});if(focusCode){ImGui::SetKeyboardFocusHere();focusCode=false;}
 auto callback=[](ImGuiInputTextCallbackData* d)->int{
  auto& e=*static_cast<LuaCodeEditor*>(d->UserData);
  if(d->EventFlag==ImGuiInputTextFlags_CallbackCompletion){
   if(ImGui::GetIO().KeyShift||d->SelectionStart!=d->SelectionEnd){e.cursor=d->CursorPos;e.selectionStart=d->SelectionStart;e.selectionEnd=d->SelectionEnd;e.Indent(ImGui::GetIO().KeyShift);d->DeleteChars(0,d->BufTextLen);d->InsertChars(0,e.text.c_str());d->CursorPos=e.cursor;d->SelectionStart=e.selectionStart;d->SelectionEnd=e.selectionEnd;}
   else {d->InsertChars(d->CursorPos,"    ");}
  }
  if(d->EventFlag==ImGuiInputTextFlags_CallbackEdit){
   // Preserve the chosen file EOL only for newly inserted newlines, never rewrite existing lines.
   std::string next(d->Buf,d->BufTextLen);std::size_t a=0;while(a<e.text.size()&&a<next.size()&&e.text[a]==next[a])++a;std::size_t tail=0;while(tail<e.text.size()-a&&tail<next.size()-a&&e.text[e.text.size()-1-tail]==next[next.size()-1-tail])++tail;
   auto inserted=next.substr(a,next.size()-a-tail);if(inserted.find('\n')!=std::string::npos){
    std::string normalized;for(std::size_t n=0;n<inserted.size();++n){if(inserted[n]=='\n'&&(n==0||inserted[n-1]!='\r'))normalized+=e.newline;else normalized+=inserted[n];}
    if(inserted=="\n"){auto begin=a?e.text.rfind('\n',a-1):std::string::npos;begin=begin==std::string::npos?0:begin+1;while(begin<a&&(e.text[begin]==' '||e.text[begin]=='\t'))normalized+=e.text[begin++];}
    if(normalized!=inserted){d->DeleteChars(int(a),int(inserted.size()));d->InsertChars(int(a),normalized.c_str());d->CursorPos=d->SelectionStart=d->SelectionEnd=int(a+normalized.size());}
   }
  }
  if(e.selectOnActivate){d->CursorPos=e.cursor;d->SelectionStart=e.selectionStart;d->SelectionEnd=e.selectionEnd;e.selectOnActivate=false;}
  e.cursor=d->CursorPos;e.selectionStart=d->SelectionStart;e.selectionEnd=d->SelectionEnd;
  return 0;
 };
 ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0,0,0,0));
 const int beforeCursor=cursor;
 if(ImGui::GetActiveID()==id&&ImGui::IsKeyPressed(ImGuiKey_Escape,false)){ImGui::SetKeyOwner(ImGuiKey_Escape,parent->GetID("Lua Escape guard"));ImGui::ClearActiveID();}
 const bool changed=ImGui::InputTextMultiline("##LuaSource",&input,{std::max(80.f,available-gutter),std::max(80.f,ImGui::GetContentRegionAvail().y-ImGui::GetStyle().ScrollbarSize-4-ImGui::GetStyle().ItemSpacing.y)},ImGuiInputTextFlags_CallbackAlways|ImGuiInputTextFlags_CallbackEdit|ImGuiInputTextFlags_CallbackCompletion|ImGuiInputTextFlags_NoUndoRedo|(readOnly?ImGuiInputTextFlags_ReadOnly:0),callback,this);
 if(ImGui::GetActiveID()==id)ImGui::SetKeyOwner(ImGuiKey_Tab,id,ImGuiInputFlags_LockUntilRelease);
 ImGui::PopStyleColor();if(changed&&!readOnly){auto at=cursor,a=selectionStart,b=selectionEnd;cursor=beforeCursor;Edit(input,at);selectionStart=a;selectionEnd=b;refreshInput=input!=text;}
 ImGuiWindow* child=nullptr;for(auto* w:parent->DC.ChildWindows)if(w->ChildId==id)child=w;
 if(!child)return changed;
 auto* state=ImGui::GetInputTextState(id);if(state&&ImGui::GetActiveID()==id)horizontalOffset=state->Scroll.x;float horizontal=horizontalOffset;
 if(jumpLine){float y=float(jumpLine-1)*height;ImGui::SetScrollY(child,std::max(0.f,y-child->InnerRect.GetHeight()*.35f));jumpLine=0;}
 // InputText owns the same child scroll; only visible source lines are drawn here.
 auto base=child->Pos;auto padding=ImGui::GetStyle().FramePadding;const float y0=base.y+padding.y-child->Scroll.y;
 auto clip=child->InnerClipRect;auto* draw=child->DrawList;draw->PushClipRect(clip.Min,clip.Max,true);
 const auto& spans=Tokens();
 const int first=std::max(0,int((clip.Min.y-y0)/height)),last=std::min(int(rows.size()),int((clip.Max.y-y0)/height)+1);
 auto color=[](LuaTokenKind k){switch(k){case LuaTokenKind::Keyword:return ImGui::GetColorU32(Accent);case LuaTokenKind::String:return IM_COL32(204,164,119,255);case LuaTokenKind::Number:return IM_COL32(167,207,160,255);case LuaTokenKind::Comment:return IM_COL32(117,157,113,255);case LuaTokenKind::Function:return IM_COL32(220,207,153,255);case LuaTokenKind::Literal:return IM_COL32(188,153,208,255);default:return ImGui::GetColorU32(ImGuiCol_Text);}};
 for(int line=first;line<last;++line){auto start=rows[line],end=line+1<int(rows.size())?rows[line+1]-1:text.size();if(end>start&&text[end-1]=='\r')--end;float y=y0+line*height;
  if(line+1==highlightLine||(!syntax.valid&&line+1==syntax.line))draw->AddRectFilled({clip.Min.x,y},{clip.Max.x,y+height},IM_COL32(150,65,55,60));
  float x=base.x+padding.x-horizontal;
  auto it=std::lower_bound(spans.begin(),spans.end(),start,[](auto& span,std::size_t value){return span.end<=value;});
  for(;it!=spans.end()&&it->begin<end;++it){auto a=std::max(start,it->begin),b=std::min(end,it->end);if(a>=b)continue;draw->AddText({x,y},color(it->kind),text.data()+a,text.data()+b);x+=ImGui::CalcTextSize(text.data()+a,text.data()+b).x;}
 }draw->PopClipRect();
 auto* numbers=parent->DrawList;numbers->PushClipRect({origin.x,clip.Min.y},{origin.x+gutter-3,clip.Max.y},true);
 for(int line=first;line<last;++line){auto number=std::to_string(line+1);numbers->AddText({origin.x+gutter-10-ImGui::CalcTextSize(number.c_str()).x,y0+line*height},ImGui::GetColorU32(ImGuiCol_TextDisabled),number.c_str());}numbers->PopClipRect();
 ImGui::SetCursorScreenPos({origin.x+gutter,ImGui::GetCursorScreenPos().y});
 ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
 if(ImGui::BeginChild("##LuaHorizontal",{std::max(80.f,available-gutter),ImGui::GetStyle().ScrollbarSize+4},ImGuiChildFlags_None,ImGuiWindowFlags_HorizontalScrollbar|ImGuiWindowFlags_AlwaysHorizontalScrollbar|ImGuiWindowFlags_NoScrollWithMouse)){
  auto* scroll=ImGui::GetCurrentWindow();ImGui::Dummy({maxLineWidth+padding.x*2,0});
  if(ImGui::GetActiveID()==id)ImGui::SetScrollX(scroll,horizontalOffset);else {horizontalOffset=scroll->Scroll.x;if(state)state->Scroll.x=horizontalOffset;}
 }ImGui::EndChild();ImGui::PopStyleVar();
 return changed;
}
}
