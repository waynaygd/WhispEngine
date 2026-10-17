#include "EditorLayer.h"
#include "../core/Application.h"
#include "../core/AssetPaths.h"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cfloat>

namespace editor {
bool EditorLayer::ProjectSettingsDirty()const{
    return m_ProjectSettingsBase&&(m_ProjectStartupDraft!=m_ProjectSettingsBase->startupScene||m_ProjectSettingsDraft!=m_ProjectSettingsBase->settings);
}
void EditorLayer::ResetProjectSettingsDraft(){
    m_ShowProjectSettings=m_FocusProjectSettings=false;
    m_ProjectSettingsBase.reset();m_ProjectStartupDraft.clear();m_ProjectSettingsDraft={};m_ProjectSettingsError.clear();
}
void EditorLayer::ShowProjectSettings(Application& app){
    const auto project=app.GetProject();if(!project)return;
    if(!m_ShowProjectSettings||!m_ProjectSettingsBase){
        m_ProjectSettingsBase=project;m_ProjectSettingsDraft=project->settings;m_ProjectStartupDraft=project->startupScene;m_ProjectSettingsError.clear();
    }
    m_ShowProjectSettings=m_FocusProjectSettings=true;
}
bool EditorLayer::SaveProjectSettingsDraft(Application& app){
    if(!ProjectSettingsDirty())return true;
    if(app.GetProject()!=m_ProjectSettingsBase){m_ProjectSettingsError="Project settings changed. Cancel and reopen this window before saving.";return false;}
    if(!app.SaveProjectSettings(m_ProjectStartupDraft,m_ProjectSettingsDraft,&m_ProjectSettingsError))return false;
    m_ProjectSettingsBase=app.GetProject();m_ProjectSettingsDraft=m_ProjectSettingsBase->settings;m_ProjectStartupDraft=m_ProjectSettingsBase->startupScene;
    return true;
}
void EditorLayer::DrawProjectSettings(Application& app){
    if(!m_ShowProjectSettings)return;
    const auto project=app.GetProject();if(!project){ResetProjectSettingsDraft();return;}
    if(!ProjectSettingsDirty()&&m_ProjectSettingsBase!=project){
        m_ProjectSettingsBase=project;m_ProjectSettingsDraft=project->settings;m_ProjectStartupDraft=project->startupScene;m_ProjectSettingsError.clear();
    }
    const bool conflict=m_ProjectSettingsBase!=project;
    ImGui::SetNextWindowSize(ImVec2(820,560),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),ImGuiCond_FirstUseEver,ImVec2(.5f,.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(480,360),ImVec2(FLT_MAX,FLT_MAX));
    if(m_FocusProjectSettings){ImGui::SetNextWindowFocus();m_FocusProjectSettings=false;}
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(16,16));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(10,10));
    bool open=true;
    const auto flags=ProjectSettingsDirty()?ImGuiWindowFlags_UnsavedDocument:ImGuiWindowFlags_None;
    if(ImGui::Begin("Project Settings",&open,flags)){
        ImGui::TextWrapped("%s",project->name.c_str());
        const float height=std::max(160.f,ImGui::GetContentRegionAvail().y-110.f);
        ImGui::BeginChild("Categories",ImVec2(142,height),ImGuiChildFlags_Borders);
        if(ImGui::Selectable("General",m_ProjectSettingsCategory==0))m_ProjectSettingsCategory=0;
        if(ImGui::Selectable("Rendering",m_ProjectSettingsCategory==1))m_ProjectSettingsCategory=1;
        ImGui::EndChild();ImGui::SameLine();
        ImGui::BeginChild("Project details",ImVec2(0,height),ImGuiChildFlags_Borders);
        if(m_ProjectSettingsCategory==0){
            ImGui::SeparatorText("General");
            ImGui::TextUnformatted("Project Name (read-only)");ImGui::TextWrapped("%s",project->name.c_str());
            ImGui::TextUnformatted("Descriptor (read-only)");ImGui::TextWrapped("%s",AssetPaths::ToUtf8String(project->descriptorPath).c_str());
            ImGui::TextWrapped("Assets directory: %s",project->assetDirectory.c_str());
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",AssetPaths::ToUtf8String(project->AssetRoot()).c_str());
            ImGui::TextWrapped("Scenes directory: %s",project->sceneDirectory.c_str());
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",AssetPaths::ToUtf8String(project->SceneRoot()).c_str());
            ImGui::Separator();ImGui::TextUnformatted("Startup Scene");
            ImGui::SetNextItemWidth(-FLT_MIN);ImGui::InputText("##StartupScene",&m_ProjectStartupDraft);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("A saved scene inside the project's Scenes directory. Save validates the complete scene before atomically updating the descriptor.");
            ImGui::BeginDisabled(app.GetSceneKey().empty());
            if(ImGui::Button("Use current scene"))m_ProjectStartupDraft=app.GetSceneKey();
            ImGui::EndDisabled();ImGui::TextWrapped("Saved startup scene: %s",project->startupScene.c_str());
        }else{
            ImGui::SeparatorText("Rendering");
            ImGui::TextUnformatted("Default Anti-Aliasing");
            int mode=int(m_ProjectSettingsDraft.antiAliasing);
            for(int value=0;value<3;++value){
                if(ImGui::RadioButton(AntiAliasingName(AntiAliasingMode(value)),&mode,value))m_ProjectSettingsDraft.antiAliasing=AntiAliasingMode(mode);
                if(ImGui::IsItemHovered())ImGui::SetTooltip("Post-process anti-aliasing supported by the renderer. New project scenes inherit this value; existing scenes retain their own AA override. These modes do not enable MSAA.");
            }
            ImGui::TextWrapped("Saved Project Default: %s",AntiAliasingName(project->settings.antiAliasing));
            ImGui::TextWrapped("Effective scene AA: %s",AntiAliasingName(app.GetEffectiveAntiAliasing()));
            ImGui::TextWrapped("Scene source: %s",app.GetSceneSettings().antiAliasingOverride?"Scene Override":"Project Default");
            ImGui::TextWrapped("Scene overrides are edited in Scene Settings. Lighting, shadows, environment and background remain scene settings.");
        }
        ImGui::EndChild();
        if(conflict)ImGui::TextWrapped("Project settings changed while this draft was open. Cancel and reopen before saving.");
        else ImGui::TextUnformatted(ProjectSettingsDirty()?"Unsaved project changes":"Project settings saved");
        ImGui::BeginDisabled(!ProjectSettingsDirty()||conflict);
        if(ImGui::Button("Save",ImVec2(110,0)))SaveProjectSettingsDraft(app);
        ImGui::EndDisabled();ImGui::SameLine();
        if(ImGui::Button("Cancel",ImVec2(110,0)))ResetProjectSettingsDraft();
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Discard this window's unsaved draft and close. Saved project and scene settings stay unchanged.");
        if(!m_ProjectSettingsError.empty())ImGui::TextWrapped("%s",m_ProjectSettingsError.c_str());
    }
    if(!open){if(ProjectSettingsDirty())ImGui::OpenPopup("Unsaved Project Settings");else ResetProjectSettingsDraft();}
    if(ImGui::BeginPopupModal("Unsaved Project Settings",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextUnformatted("Save project settings before closing this window?");
        ImGui::BeginDisabled(conflict);
        if(ImGui::Button("Save and close")&&SaveProjectSettingsDraft(app)){ResetProjectSettingsDraft();ImGui::CloseCurrentPopup();}
        ImGui::EndDisabled();ImGui::SameLine();
        if(ImGui::Button("Discard")){ResetProjectSettingsDraft();ImGui::CloseCurrentPopup();}
        ImGui::SameLine();if(ImGui::Button("Keep editing"))ImGui::CloseCurrentPopup();
        if(!m_ProjectSettingsError.empty())ImGui::TextWrapped("%s",m_ProjectSettingsError.c_str());
        ImGui::EndPopup();
    }
    ImGui::End();ImGui::PopStyleVar(2);
}
}
