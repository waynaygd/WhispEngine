#pragma once
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace editor::inspector {
// Presentation only. Callers retain validation, drafts and transaction ownership.
inline bool Matches(std::string_view text,std::string_view query){
    return query.empty()||std::search(text.begin(),text.end(),query.begin(),query.end(),[](unsigned char a,unsigned char b){return std::tolower(a)==std::tolower(b);})!=text.end();
}
inline bool Narrow(){return ImGui::GetContentRegionAvail().x<340*ImGui::GetStyle().FontScaleDpi;}
inline bool BeginProperties(const char* id){
    const bool narrow=Narrow();
    if(!ImGui::BeginTable(id,narrow?1:2,ImGuiTableFlags_SizingStretchProp))return false;
    if(!narrow){ImGui::TableSetupColumn("Property",ImGuiTableColumnFlags_WidthFixed,std::min(145*ImGui::GetStyle().FontScaleDpi,ImGui::GetContentRegionAvail().x*.36f));ImGui::TableSetupColumn("Value",ImGuiTableColumnFlags_WidthStretch);}
    return true;
}
inline void PropertyRow(const char* label,const char* tooltip=""){
    ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::AlignTextToFramePadding();
    ImGui::TextWrapped("%s",label);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s%s%s",label,*tooltip?"\n":"",tooltip);
    if(ImGui::TableGetColumnCount()>1)ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x,360*ImGui::GetStyle().FontScaleDpi));
}
inline void Section(const char* text){ImGui::Spacing();ImGui::SeparatorText(text);}
}
