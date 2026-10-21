#pragma once
#include <imgui.h>
#include <algorithm>
#include <string_view>
namespace editor {
inline constexpr ImVec4 Accent{.32f,.57f,.79f,1},Warning{.84f,.66f,.35f,1},Error{.88f,.43f,.43f,1};
inline void ErrorText(const char* text){ImGui::PushStyleColor(ImGuiCol_Text,Error);ImGui::TextWrapped("%s",text);ImGui::PopStyleColor();}
inline ImFont* MonoFont(){for(auto* f:ImGui::GetIO().Fonts->Fonts)if(std::string_view(f->GetDebugName())=="Editor Mono")return f;return ImGui::GetIO().FontDefault;}
inline void Help(const char* text){if(text&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",text);}
inline void Inline(const char* label,float extra=0){ImGui::SameLine();if(ImGui::GetContentRegionAvail().x<ImGui::CalcTextSize(label).x+ImGui::GetStyle().FramePadding.x*2+extra*ImGui::GetStyle().FontScaleDpi)ImGui::NewLine();}
inline bool Action(const char* label,const char* help,bool enabled=true,bool selected=false,float width=0){
    ImGui::BeginDisabled(!enabled);if(selected)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.21f,.35f,.47f,1));
    const std::string_view name=label;const bool icon=name=="Save"||name=="Play"||name=="Pause"||name=="Resume"||name=="Stop";
    if(icon)ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,ImVec2(.85f,.5f));
    const bool pressed=ImGui::Button(label,{width*ImGui::GetStyle().FontScaleDpi,0});if(icon)ImGui::PopStyleVar();
    if(icon){auto p=ImGui::GetItemRectMin();const float u=ImGui::GetStyle().FontScaleDpi;const float y=(ImGui::GetItemRectMax().y+p.y)*.5f;auto* d=ImGui::GetWindowDrawList();auto c=ImGui::GetColorU32(ImGuiCol_Text);p.x+=6*u;
        if(name=="Play"||name=="Resume")d->AddTriangleFilled({p.x,y-5*u},{p.x,y+5*u},{p.x+8*u,y},c);
        else if(name=="Pause"){d->AddRectFilled({p.x,y-5*u},{p.x+3*u,y+5*u},c);d->AddRectFilled({p.x+5*u,y-5*u},{p.x+8*u,y+5*u},c);}
        else {d->AddRect({p.x,y-4*u},{p.x+8*u,y+4*u},c,1);if(name=="Save")d->AddLine({p.x+2*u,y},{p.x+6*u,y},c);}
    }
    if(selected)ImGui::PopStyleColor();ImGui::EndDisabled();Help(help);return pressed;
}
inline bool Decision(const char* label,bool destructive=false){
    ImGui::PushStyleColor(ImGuiCol_Button,destructive?ImVec4(.33f,.18f,.19f,1):ImVec4(.18f,.33f,.46f,1));
    bool clicked=ImGui::Button(label);ImGui::PopStyleColor();return clicked;
}
// Shared ImGui tokens; no separate theme/persistence system.
inline void ApplyEditorTheme(float dpi){
    dpi=std::clamp(dpi,.75f,3.f);
    ImGui::GetStyle()=ImGuiStyle{};ImGui::StyleColorsDark();auto& s=ImGui::GetStyle();
    s.WindowPadding={12,10};s.FramePadding={7,4};s.ItemSpacing={7,6};s.ItemInnerSpacing={5,4};s.DisabledAlpha=.5f;
    s.WindowRounding=5;s.ChildRounding=4;s.FrameRounding=4;s.PopupRounding=5;s.TabRounding=4;s.GrabRounding=3;
    s.WindowBorderSize=1;s.ChildBorderSize=1;s.PopupBorderSize=1;s.FrameBorderSize=0;s.ScrollbarSize=13;
    auto* c=s.Colors;
    c[ImGuiCol_Text]={.88f,.90f,.94f,1};c[ImGuiCol_TextDisabled]={.49f,.54f,.62f,1};
    c[ImGuiCol_WindowBg]={.075f,.085f,.105f,1};c[ImGuiCol_ChildBg]={.065f,.074f,.092f,1};
    c[ImGuiCol_PopupBg]={.10f,.115f,.14f,.99f};c[ImGuiCol_Border]={.20f,.23f,.28f,1};
    c[ImGuiCol_FrameBg]={.13f,.15f,.19f,1};c[ImGuiCol_FrameBgHovered]={.19f,.24f,.31f,1};c[ImGuiCol_FrameBgActive]={.22f,.32f,.44f,1};
    c[ImGuiCol_TitleBg]={.055f,.065f,.08f,1};c[ImGuiCol_TitleBgActive]={.10f,.13f,.18f,1};
    c[ImGuiCol_Button]={.14f,.18f,.23f,1};c[ImGuiCol_ButtonHovered]={.20f,.29f,.38f,1};c[ImGuiCol_ButtonActive]={.24f,.38f,.50f,1};
    c[ImGuiCol_Header]={.16f,.24f,.34f,1};c[ImGuiCol_HeaderHovered]={.21f,.34f,.48f,1};c[ImGuiCol_HeaderActive]={.23f,.40f,.58f,1};
    c[ImGuiCol_Tab]={.10f,.13f,.17f,1};c[ImGuiCol_TabSelected]={.17f,.26f,.37f,1};c[ImGuiCol_TabHovered]={.24f,.40f,.57f,1};
    c[ImGuiCol_CheckMark]=c[ImGuiCol_SliderGrab]={.38f,.67f,.95f,1};c[ImGuiCol_SliderGrabActive]={.55f,.78f,1,1};
    c[ImGuiCol_Separator]={.20f,.24f,.30f,1};c[ImGuiCol_TextSelectedBg]={.24f,.43f,.65f,.65f};
    c[ImGuiCol_NavCursor]=Accent;c[ImGuiCol_DockingPreview]={.32f,.57f,.79f,.45f};
    s.ScaleAllSizes(dpi);s.FontSizeBase=16;s.FontScaleDpi=dpi;
}
}
