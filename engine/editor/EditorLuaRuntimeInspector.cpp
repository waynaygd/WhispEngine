#include "EditorLayer.h"
#include "../core/Application.h"
#include "../ecs/components/LuaScriptComponent.h"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
namespace editor {
void EditorLayer::UpdateLuaRuntimeInspector(Application& app,double now,bool refresh){
    if(!app.IsEditorPlayMode()){m_LuaInspection={};m_LuaInspectionCaptured=false;return;}
    auto info=app.GetLuaRuntime().InspectionInfo(m_Selection.active);
    const bool changed=!m_LuaInspection.info.SameInstance(info)||m_LuaInspection.info.state!=info.state;
    if(changed){m_LuaInspection={};m_LuaInspectionCaptured=false;}
    m_LuaInspection.info=info;
    if(!info.CanRead()){m_LuaInspection.fields.clear();m_LuaInspection.definition.clear();m_LuaInspectionCaptured=false;return;}
    if(refresh||(m_ShowLuaRuntimeInspector&&(!m_LuaInspectionCaptured||(m_LuaInspectorAuto&&now-m_LuaInspectionTime>=.25)))){
        m_LuaInspection=app.GetLuaRuntime().Inspect(m_Selection.active);m_LuaInspectionCaptured=true;m_LuaInspectionTime=now;++m_LuaInspectionRefreshes;
    }
}
bool EditorLayer::ShowEventsForEntity(Application& app,ecs::Entity entity){
    if(!app.IsEditorPlayMode()||!app.GetWorld().IsAlive(entity))return false;
    m_EventFilter={};m_EventFilter.entity=std::to_string(entity.world)+":"+std::to_string(entity.index)+":"+std::to_string(entity.generation);
    m_ShowGameplayEvents=true;return true;
}
bool EditorLayer::InspectGameplayEventEntity(Application& app,const scripting::EventEntity& entity){
    if(!app.IsEditorPlayMode()||!app.GetWorld().IsAlive(entity.entity)||!app.GetWorld().HasComponent<ecs::LuaScriptComponent>(entity.entity))return false;
    SelectEntityFromUser(app,entity.entity);m_ShowLuaRuntimeInspector=true;UpdateLuaRuntimeInspector(app,ImGui::GetTime(),true);return true;
}
namespace {
bool Matches(const scripting::LuaInspectedValue& value,const std::string& query){
    if(scripting::GameplayEventFilter::Contains(value.name,query))return true;
    return std::any_of(value.children.begin(),value.children.end(),[&](const auto& child){return Matches(child,query);});
}
void Values(const std::vector<scripting::LuaInspectedValue>& values,const std::string& search,const ecs::World& world){
    for(std::size_t n=0;n<values.size();++n){const auto& value=values[n];if(!Matches(value,search))continue;
        ImGui::PushID(int(n));ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
        const bool leaf=value.children.empty();const auto flags=ImGuiTreeNodeFlags_SpanAvailWidth|(leaf?ImGuiTreeNodeFlags_Leaf|ImGuiTreeNodeFlags_NoTreePushOnOpen:ImGuiTreeNodeFlags_None);
        const bool open=ImGui::TreeNodeEx(value.name.c_str(),flags);
        ImGui::TableSetColumnIndex(1);ImGui::TextUnformatted(value.type.c_str());ImGui::TableSetColumnIndex(2);ImGui::TextUnformatted(value.value.c_str());
        if(value.entity.IsValid()){ImGui::SameLine();const auto* tag=world.GetComponent<ecs::TagComponent>(value.entity);ImGui::TextDisabled("%s",tag?tag->name.c_str():world.IsAlive(value.entity)?"Unnamed":"Destroyed / foreign World");}
        if(value.limited){ImGui::SameLine();ImGui::TextDisabled("(bounded view)");}
        if(open&&!leaf){Values(value.children,search,world);ImGui::TreePop();}ImGui::PopID();
    }
}
}
void EditorLayer::DrawLuaRuntimeInspector(Application& app){
    using namespace scripting;
    UpdateLuaRuntimeInspector(app,ImGui::GetTime());
    if(!ImGui::Begin("Lua Runtime Inspector",&m_ShowLuaRuntimeInspector)){ImGui::End();return;}
    auto& world=app.GetWorld();const auto e=m_Selection.active;
    if(!app.IsEditorPlayMode()){ImGui::TextWrapped("Runtime state is available only during Play. No authored Script Fields are substituted here.");ImGui::End();return;}
    const auto* tag=world.GetComponent<ecs::TagComponent>(e);
    ImGui::TextWrapped("Selected Entity: %s",tag?tag->name.c_str():world.IsAlive(e)?"Unnamed":"None / destroyed");
    if(e.IsValid())ImGui::Text("World %llu : %u : %u",static_cast<unsigned long long>(e.world),e.index,e.generation);
    if(ImGui::Button("Refresh"))UpdateLuaRuntimeInspector(app,ImGui::GetTime(),true);ImGui::SameLine();ImGui::Checkbox("Auto Refresh",&m_LuaInspectorAuto);
    ImGui::SameLine();ImGui::BeginDisabled(!world.IsAlive(e));if(ImGui::Button("Show Events for Entity"))ShowEventsForEntity(app,e);ImGui::EndDisabled();
    const auto& snapshot=m_LuaInspection;const auto& info=snapshot.info;
    ImGui::TextWrapped("Script: %s",info.path.empty()?"Unavailable":info.path.c_str());
    ImGui::TextWrapped("Status: %s%s",LuaInstanceStateName(info.state),app.IsEditorPaused()?" | Play paused":"");
    if(!info.lastCallback.empty())ImGui::Text("Last call: %s | %s",info.lastCallback.c_str(),EventStatusName(info.lastStatus));
    if(!info.lastError.empty()){ImGui::TextColored(ImVec4(1,.35f,.3f,1),"Instance error");ImGui::TextWrapped("%s",info.lastError.c_str());}
    if(!info.loadError.empty()){ImGui::TextColored(ImVec4(1,.7f,.3f,1),"Script load / reload diagnostic (shared source)");ImGui::TextWrapped("%s",info.loadError.c_str());if(info.CanRead())ImGui::TextWrapped("Reload was rejected; the existing instance and its previous revision remain active.");}
    if(!info.CanRead()){ImGui::TextWrapped("No inspectable live instance. Inspection never creates one or runs callbacks.");ImGui::End();return;}
    ImGui::Text("Session %llu | Instance %llu | Revision %llu",static_cast<unsigned long long>(info.session),static_cast<unsigned long long>(info.serial),static_cast<unsigned long long>(info.revision));
    ImGui::TextWrapped("Values are copied snapshots%s. Auto Refresh: at most 4 times/second; instance changes refresh once. Status/error metadata is live.",m_LuaInspectorAuto?"":" (manual mode)");
    if(m_LuaInspectionCaptured)ImGui::TextDisabled("Snapshot age: %.2f s",std::max(0.,ImGui::GetTime()-m_LuaInspectionTime));
    ImGui::TextWrapped("self contains actual instance fields. Definition data is also isolated per instance. Engine self.entity, field declarations and lifecycle callbacks are omitted. Locals, upvalues and environment globals are unavailable.");
    ImGui::SetNextItemWidth(250);ImGui::InputTextWithHint("##luaVariableSearch","Variable name",&m_LuaVariableSearch);
    if(snapshot.limited)ImGui::TextColored(ImVec4(1,.75f,.3f,1),"Bounded snapshot: some entries, text, depth or cycles were omitted.");
    auto table=[&](const char* id,const auto& data){if(ImGui::BeginTable(id,3,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_Resizable)){
        ImGui::TableSetupColumn("Variable");ImGui::TableSetupColumn("Type");ImGui::TableSetupColumn("Value");ImGui::TableHeadersRow();Values(data,m_LuaVariableSearch,world);ImGui::EndTable();}};
    ImGui::BeginChild("Lua fields");ImGui::TextUnformatted("Instance self");table("RuntimeSelf",snapshot.fields);
    if(!snapshot.definition.empty()){ImGui::Separator();ImGui::TextUnformatted("Definition data (same instance)");table("RuntimeDefinition",snapshot.definition);}ImGui::EndChild();
    ImGui::End();
}
}
