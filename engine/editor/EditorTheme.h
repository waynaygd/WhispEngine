#pragma once
#include <imgui.h>
#include <algorithm>
namespace editor {
// The bundled font keeps the editor style; the UI backend supplies missing glyphs.
inline void ApplyEditorTheme(float dpi){
    dpi=std::clamp(dpi,.75f,3.f);
    ImGui::GetStyle()=ImGuiStyle{};ImGui::StyleColorsDark();auto& s=ImGui::GetStyle();
    s.WindowPadding={12,10};s.FramePadding={8,5};s.ItemSpacing={8,7};s.ItemInnerSpacing={6,5};
    s.WindowRounding=5;s.ChildRounding=4;s.FrameRounding=4;s.PopupRounding=5;s.TabRounding=4;s.GrabRounding=3;
    s.WindowBorderSize=1;s.ChildBorderSize=1;s.PopupBorderSize=1;s.FrameBorderSize=0;s.ScrollbarSize=13;
    auto* c=s.Colors;
    c[ImGuiCol_Text]={.88f,.90f,.94f,1};c[ImGuiCol_TextDisabled]={.49f,.54f,.62f,1};
    c[ImGuiCol_WindowBg]={.075f,.085f,.105f,1};c[ImGuiCol_ChildBg]={.065f,.074f,.092f,1};
    c[ImGuiCol_PopupBg]={.10f,.115f,.14f,.99f};c[ImGuiCol_Border]={.20f,.23f,.28f,1};
    c[ImGuiCol_FrameBg]={.13f,.15f,.19f,1};c[ImGuiCol_FrameBgHovered]={.19f,.24f,.31f,1};c[ImGuiCol_FrameBgActive]={.22f,.32f,.44f,1};
    c[ImGuiCol_TitleBg]={.055f,.065f,.08f,1};c[ImGuiCol_TitleBgActive]={.10f,.13f,.18f,1};
    c[ImGuiCol_Button]={.16f,.21f,.28f,1};c[ImGuiCol_ButtonHovered]={.22f,.36f,.51f,1};c[ImGuiCol_ButtonActive]={.24f,.44f,.64f,1};
    c[ImGuiCol_Header]={.16f,.24f,.34f,1};c[ImGuiCol_HeaderHovered]={.21f,.34f,.48f,1};c[ImGuiCol_HeaderActive]={.23f,.40f,.58f,1};
    c[ImGuiCol_Tab]={.10f,.13f,.17f,1};c[ImGuiCol_TabSelected]={.17f,.26f,.37f,1};c[ImGuiCol_TabHovered]={.24f,.40f,.57f,1};
    c[ImGuiCol_CheckMark]=c[ImGuiCol_SliderGrab]={.38f,.67f,.95f,1};c[ImGuiCol_SliderGrabActive]={.55f,.78f,1,1};
    c[ImGuiCol_Separator]={.20f,.24f,.30f,1};c[ImGuiCol_TextSelectedBg]={.24f,.43f,.65f,.65f};
    s.ScaleAllSizes(dpi);s.FontSizeBase=16;s.FontScaleDpi=dpi;
}
}
