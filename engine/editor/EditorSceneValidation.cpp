#include "EditorLayer.h"
#include "../core/Application.h"
#include "../core/AssetPaths.h"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
namespace editor {
namespace {
std::string Context(Application& app){
    return std::string(app.IsEditorPlayMode()?"Runtime":app.IsPrefabEditing()?"Prefab":"Editor")+" | "+app.GetSceneKey()+" | "+AssetPaths::ToUtf8String(AssetPaths::ResolveAssetRoot());
}

}
void EditorLayer::ValidateCurrentScene(Application& app){
    m_SceneValidation=ValidateScene(app.GetWorld(),app.GetSceneSettings(),&app.GetLuaRuntime(),app.GetResourceManager());
    m_SceneValidation.context=Context(app);m_SelectedValidation=-1;
}
bool EditorLayer::SceneValidationContextMatches(Application& app)const{
    return m_SceneValidation.validated&&m_SceneValidation.world==app.GetWorld().GetIdentity()&&m_SceneValidation.context==Context(app);
}
bool EditorLayer::SelectValidationIssue(Application& app,std::size_t index,bool focus){
    if(!SceneValidationContextMatches(app)||index>=m_SceneValidation.issues.size())return false;
    auto e=m_SceneValidation.issues[index].entity;
    if(!app.GetWorld().IsAlive(e))return false;
    SelectEntityFromUser(app,e);m_ShowHierarchy=true;
    if(focus)app.FocusEntity(e);
    return true;
}
void EditorLayer::DrawSceneValidation(Application& app){
    if(!editor::BeginEditorPanel("Scene Validation",&m_ShowSceneValidation)){ImGui::End();return;}
    if(ImGui::Button("Validate Scene"))ValidateCurrentScene(app);
    if(!m_SceneValidation.validated){ImGui::TextWrapped("Validate the current Editor, Prefab or Runtime World. No scripts are executed and no resources are requested.");ImGui::End();return;}
    ImGui::SameLine();ImGui::Text("Errors: %zu | Warnings: %zu | Info: %zu",m_SceneValidation.Count(ValidationSeverity::Error),m_SceneValidation.Count(ValidationSeverity::Warning),m_SceneValidation.Count(ValidationSeverity::Info));
    const bool current=SceneValidationContextMatches(app);
    ImGui::TextWrapped("%s",m_SceneValidation.context.c_str());
    if(!current)ImGui::TextColored(ImVec4(1,.65f,.2f,1),"Different scene or World. Validate Scene to replace these results.");
    ImGui::TextWrapped("Snapshot: scene and file changes may make results stale. Use Validate Scene after editing. File availability does not prove that a model or texture can be decoded.");
    ImGui::SetNextItemWidth(135);ImGui::Combo("Severity",&m_ValidationSeverity,"All\0Error\0Warning\0Info\0");ImGui::SameLine();
    ImGui::SetNextItemWidth(250);ImGui::InputTextWithHint("##validationSearch","Entity, resource or message",&m_ValidationSearch);
    const auto& issues=m_SceneValidation.issues;
    if(issues.empty())ImGui::TextUnformatted("No issues found by the supported checks.");
    if(ImGui::BeginTable("Validation issues",4,ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY|ImGuiTableFlags_Resizable,ImVec2(0,std::max(100.f,ImGui::GetContentRegionAvail().y*.5f)))){
        for(auto* name:{"Severity","Issue","Entity / ID","Resource / Field"})ImGui::TableSetupColumn(name);ImGui::TableHeadersRow();
        for(std::size_t i=0;i<issues.size();++i){const auto& issue=issues[i];
            if(!issue.Matches(m_ValidationSearch,m_ValidationSeverity-1))continue;
            ImGui::PushID(int(i));ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
            const auto color=issue.severity==ValidationSeverity::Error?ImVec4(1,.35f,.3f,1):issue.severity==ValidationSeverity::Warning?ImVec4(1,.75f,.3f,1):ImVec4(.6f,.8f,1,1);
            ImGui::TextColored(color,"%s",ValidationSeverityName(issue.severity));ImGui::TableSetColumnIndex(1);
            if(ImGui::Selectable(issue.title.c_str(),int(i)==m_SelectedValidation,ImGuiSelectableFlags_SpanAllColumns))m_SelectedValidation=int(i);
            ImGui::TableSetColumnIndex(2);ImGui::TextUnformatted((issue.entityName.empty()?issue.stableId:issue.entityName).c_str());
            ImGui::TableSetColumnIndex(3);ImGui::TextUnformatted((issue.resource.empty()?issue.component+"."+issue.field:issue.resource).c_str());ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if(m_SelectedValidation>=0&&std::size_t(m_SelectedValidation)<issues.size()){
        const auto& issue=issues[m_SelectedValidation];ImGui::BeginChild("Validation details");
        ImGui::TextWrapped("%s: %s",ValidationSeverityName(issue.severity),issue.title.c_str());
        ImGui::TextWrapped("%s",issue.description.c_str());
        ImGui::TextWrapped("Entity: %s | Persistent ID: %s",issue.entityName.c_str(),issue.stableId.c_str());
        if(issue.entity.IsValid())ImGui::Text("World %llu : %u : %u",static_cast<unsigned long long>(issue.entity.world),issue.entity.index,issue.entity.generation);
        ImGui::TextWrapped("Component: %s | Field: %s",issue.component.c_str(),issue.field.c_str());
        if(!issue.resource.empty())ImGui::TextWrapped("Resource: %s",issue.resource.c_str());
        if(!issue.recommendation.empty())ImGui::TextWrapped("Fix: %s",issue.recommendation.c_str());
        const bool alive=current&&app.GetWorld().IsAlive(issue.entity);
        ImGui::BeginDisabled(!alive);if(ImGui::Button("Select Entity"))SelectValidationIssue(app,m_SelectedValidation);ImGui::SameLine();if(ImGui::Button("Focus Entity"))SelectValidationIssue(app,m_SelectedValidation,true);ImGui::EndDisabled();
        if(!alive&&issue.entity.IsValid())ImGui::TextDisabled("Entity deleted or World changed; navigation unavailable.");
        ImGui::EndChild();
    }
    ImGui::End();
}
}
