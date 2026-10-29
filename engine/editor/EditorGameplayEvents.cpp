#include "EditorLayer.h"
#include "../core/Application.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
namespace editor {
bool EditorLayer::SelectGameplayEventEntity(Application& app,const scripting::EventEntity& item){
    // World/generation identity prevents old session records selecting a reused ID.
    if(!app.GetWorld().IsAlive(item.entity))return false;
    SelectViewportEntity(app,item.entity,false,false);m_ShowHierarchy=true;return true;
}
void EditorLayer::DrawGameplayEvents(Application& app){
    using namespace scripting;
    if(!editor::BeginEditorPanel("Gameplay Events",&m_ShowGameplayEvents)){ImGui::End();return;}
    auto& history=app.GetLuaRuntime().Events();
    if(ImGui::Button(history.Capturing()?"Stop Capture":"Start Capture")){if(history.Capturing())history.StopCapture();else history.StartCapture();}
    ImGui::SameLine();bool paused=history.Paused();ImGui::BeginDisabled(!history.Capturing());if(ImGui::Checkbox("Pause Capture",&paused))history.PauseCapture(paused);ImGui::EndDisabled();
    ImGui::SameLine();if(ImGui::Button("Clear History")){history.Clear();m_SelectedGameplayEvent=0;}
    ImGui::SameLine();ImGui::Checkbox("Follow latest",&m_EventFollow);
    int capacity=int(history.Capacity());ImGui::SetNextItemWidth(100);if(ImGui::InputInt("History limit",&capacity,0,0))history.SetCapacity(std::size_t(std::max(128,capacity)));
    ImGui::SameLine();ImGui::TextDisabled("%zu / %zu | %s",history.Rows().size(),history.Capacity(),app.IsEditorPlayMode()?(app.IsEditorPaused()?"Play paused":"Play running"):"Play stopped");
    ImGui::SetNextItemWidth(180);ImGui::InputTextWithHint("##eventSearch","Event name",&m_EventFilter.name);ImGui::SameLine();
    ImGui::SetNextItemWidth(180);ImGui::InputTextWithHint("##sourceSearch","Source name or ID",&m_EventFilter.source);ImGui::SameLine();
    ImGui::SetNextItemWidth(180);ImGui::InputTextWithHint("##targetSearch","Target name or ID",&m_EventFilter.target);
    ImGui::SetNextItemWidth(270);ImGui::InputTextWithHint("##eventEntitySearch","Entity (source or target)",&m_EventFilter.entity);
    ImGui::SetNextItemWidth(130);int status=m_EventFilter.status+1;if(ImGui::Combo("Status",&status,"All\0Queued\0Processing\0Success\0Error\0Skipped\0"))m_EventFilter.status=status-1;ImGui::SameLine();ImGui::Checkbox("Only errors",&m_EventFilter.errorsOnly);
    auto useSelection=[&](std::string& query){const auto e=m_Selection.active;if(!app.GetWorld().IsAlive(e))return;query=std::to_string(e.world)+":"+std::to_string(e.index)+":"+std::to_string(e.generation);};
    if(ImGui::Button("Selected as source"))useSelection(m_EventFilter.source);ImGui::SameLine();if(ImGui::Button("Selected as target"))useSelection(m_EventFilter.target);ImGui::SameLine();if(ImGui::Button("Reset filters"))m_EventFilter={};
    ImGui::TextDisabled("Chronological deliveries; no inferred event chains. Physics source is unknown.");
    std::vector<const GameplayEvent*> visible;visible.reserve(history.Rows().size());for(const auto& row:history.Rows())if(m_EventFilter.Matches(row))visible.push_back(&row);
    const float height=std::max(110.f,ImGui::GetContentRegionAvail().y*.50f);
    if(ImGui::BeginTable("EventHistory",6,ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY|ImGuiTableFlags_Resizable,ImVec2(0,height))){
        for(const char* column:{"Session / #","Time (s)","Event","Source","Target","Result"})ImGui::TableSetupColumn(column);ImGui::TableHeadersRow();
        ImGuiListClipper clipper;clipper.Begin(int(visible.size()));
        while(clipper.Step())for(int n=clipper.DisplayStart;n<clipper.DisplayEnd;++n){const auto& row=*visible[n];ImGui::PushID(std::to_string(row.sequence).c_str());ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
            const auto label=std::to_string(row.session)+" / "+std::to_string(row.sequence);if(ImGui::Selectable(label.c_str(),row.sequence==m_SelectedGameplayEvent,ImGuiSelectableFlags_SpanAllColumns))m_SelectedGameplayEvent=row.sequence;
            ImGui::TableSetColumnIndex(1);ImGui::Text("%.3f",row.time);ImGui::TableSetColumnIndex(2);ImGui::TextUnformatted(row.name.c_str());
            ImGui::TableSetColumnIndex(3);ImGui::TextUnformatted(row.source.entity.IsValid()?(row.source.name.empty()?"Unnamed":row.source.name.c_str()):"Unknown");ImGui::TableSetColumnIndex(4);ImGui::TextUnformatted(row.target.name.empty()?"Unnamed":row.target.name.c_str());
            ImGui::TableSetColumnIndex(5);if(row.status==EventStatus::Error)ImGui::TextColored(ImVec4(1,.35f,.3f,1),"Error");else ImGui::TextUnformatted(EventStatusName(row.status));ImGui::PopID();
        }
        if(m_EventFollow&&history.Active())ImGui::SetScrollY(ImGui::GetScrollMaxY());ImGui::EndTable();
    }
    if(const auto* row=history.Find(m_SelectedGameplayEvent)){
        ImGui::BeginChild("Event details",ImVec2(0,0),ImGuiChildFlags_None);
        ImGui::Text("%s | %s | Session %llu / #%llu",row->name.c_str(),row->kind.c_str(),static_cast<unsigned long long>(row->session),static_cast<unsigned long long>(row->sequence));
        ImGui::Text("Observed: %.6f s | %s",row->time,EventStatusName(row->status));
        if(row->status==EventStatus::Queued)ImGui::TextWrapped("Delivery not observed yet. Capture may have been paused or stopped before delivery.");
        else if(row->error=="Play ended before delivery")ImGui::TextDisabled("Delivery: unavailable (Play ended)");
        else ImGui::Text("Delivery: %.6f s",row->deliveryTime);
        if(row->handlerStart>=0)ImGui::Text("Handler started: %.6f s | Finished: %.6f s",row->handlerStart,row->handlerEnd);
        else ImGui::TextDisabled("Handler execution: not observed");
        ImGui::TextWrapped("Lua Handler: %s | Script: %s",row->handler.c_str(),row->script.empty()?"Unavailable (no handler delivery captured)":row->script.c_str());
        if(row->kind=="UI")ImGui::TextWrapped("Canvas: %s | Widget: %s",row->canvas.c_str(),row->widget.c_str());
        ImGui::TextWrapped("Payload: %s",row->payload.c_str());if(!row->error.empty())ImGui::TextWrapped("Diagnostic: %s",row->error.c_str());
        auto entity=[&](const char* label,const EventEntity& item){ImGui::PushID(label);ImGui::TextWrapped("%s: %s",label,item.entity.IsValid()?(item.name.empty()?"Unnamed":item.name.c_str()):"Unknown / unavailable");if(item.entity.IsValid()){
            ImGui::TextWrapped("ID: %s | World %llu : %u : %u",item.stableId.empty()?"unavailable":item.stableId.c_str(),static_cast<unsigned long long>(item.entity.world),item.entity.index,item.entity.generation);
            const bool alive=app.GetWorld().IsAlive(item.entity);ImGui::BeginDisabled(!alive);if(ImGui::SmallButton("Select in Hierarchy"))SelectGameplayEventEntity(app,item);ImGui::EndDisabled();
            const auto runtime=app.GetLuaRuntime().InspectionInfo(item.entity);ImGui::SameLine();ImGui::BeginDisabled(!app.IsEditorPlayMode()||!alive||runtime.state==LuaInstanceState::NoScript);if(ImGui::SmallButton("Inspect Script"))InspectGameplayEventEntity(app,item);ImGui::EndDisabled();if(!alive){ImGui::SameLine();ImGui::TextDisabled("Destroyed or session ended");}}
            ImGui::PopID();};
        entity("Source",row->source);entity("Target",row->target);if(row->other.entity.IsValid())entity("Other participant",row->other);
        ImGui::EndChild();
    }else ImGui::TextWrapped("Select an event for details. History survives Stop; the next Play uses a new session ID.");
    ImGui::End();
}
}
