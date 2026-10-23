#include "EditorLayer.h"
#include "../core/Application.h"
#include "../core/AssetPaths.h"
#include "../platform/GlfwWindow.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
namespace editor {
void EditorLayer::ShowProjectSettings(Application& app){
    if(!m_ProjectSettingsBase||(!ProjectSettingsDirty()&&m_ProjectSettingsBase!=app.GetProject()))RevertProjectSettings(app);
    const auto id=RegisterDocument(app,DocumentType::ProjectSettings,"ProjectSettings").id;
    RequestDocumentNavigation(app,id);m_ShowProjectSettings=m_FocusProjectSettings=true;
}
void EditorLayer::RevertProjectSettings(Application& app){
    m_ProjectSettingsBase=app.GetProject();m_ProjectSettingsDraft=m_ProjectSettingsBase?m_ProjectSettingsBase->Settings():ProjectSettings{};
    m_ProjectSettingsError.clear();m_ProjectSettingsNotice.clear();
    try{m_ProjectSettingsScenes.Refresh({});}catch(const std::exception& e){m_ProjectSettingsError=e.what();}
}
void EditorLayer::ProjectSettingsSaved(Application& app){
    if(auto p=app.GetProject()){
        const auto path=AssetPaths::ToUtf8String(p->descriptorPath);
        for(auto& recent:m_RecentProjects)if(recent.descriptor==path)recent.name=p->name;
        if(app.GetWindow())PersistPreferences(app);
    }
}
bool EditorLayer::SaveProjectSettingsDraft(Application& app){
    m_ProjectSettingsError.clear();m_ProjectSettingsNotice.clear();
    if(!m_ProjectSettingsBase||m_ProjectSettingsBase!=app.GetProject()){
        m_ProjectSettingsError="The active project settings changed. Use Revert to load their current values before saving.";return false;
    }
    if(!app.SaveProjectSettings(m_ProjectSettingsDraft,&m_ProjectSettingsError))return false;
    m_ProjectSettingsBase=app.GetProject();m_ProjectSettingsDraft=m_ProjectSettingsBase->Settings();
    m_ProjectSettingsNotice="Saved. GPU Instancing applied; Startup Scene will load the next time this project opens.";return true;
}
void EditorLayer::DrawProjectSettings(Application& app){
    auto project=app.GetProject();
    if(!ProjectSettingsDirty()&&project!=m_ProjectSettingsBase)RevertProjectSettings(app);
    if(m_FocusProjectSettings){ImGui::SetNextWindowFocus();m_FocusProjectSettings=false;}
    ImGui::SetNextWindowSize(ImVec2(740,480),ImGuiCond_FirstUseEver);ImGui::SetNextWindowSizeConstraints(ImVec2(280,250),ImVec2(FLT_MAX,FLT_MAX));
    bool open=true;
    auto close=[&]{for(const auto& d:m_Documents)if(d.type==DocumentType::ProjectSettings){auto id=d.id;RequestDocumentNavigation(app,id,true);break;}};
    if(!ImGui::Begin("Project Settings",&open,ProjectSettingsDirty()?ImGuiWindowFlags_UnsavedDocument:0)){ImGui::End();if(!open)close();return;}
    if(!project){ImGui::TextWrapped("Open a .whispproject in Project Browser to edit project settings.");ImGui::End();if(!open)close();return;}
    ImGui::TextUnformatted(project->name.c_str());ImGui::SameLine();ImGui::TextDisabled(ProjectSettingsDirty()?"Unsaved changes":"Saved");
    if(app.IsEditorPlayMode())ImGui::TextDisabled("Stop Play to edit or save Project Settings.");
    const float footer=ImGui::GetFrameHeightWithSpacing()*3.5f;
    ImGui::BeginChild("Categories",ImVec2(std::min(150.f,ImGui::GetContentRegionAvail().x*.3f),-footer),ImGuiChildFlags_Borders);
    const char* categories[]={"Project / General","Rendering","Physics (read-only)","Startup / Runtime (read-only)"};
    for(int i=0;i<4;++i)if(ImGui::Selectable(categories[i],m_ProjectSettingsCategory==i))m_ProjectSettingsCategory=i;
    ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("Settings",ImVec2(0,-footer));
    ImGui::PushItemWidth(std::max(120.f,ImGui::GetContentRegionAvail().x*.62f));
    if(m_ProjectSettingsCategory==0){
        ImGui::SeparatorText("General");ImGui::BeginDisabled(app.IsEditorPlayMode());
        ImGui::InputText("Search",&m_ProjectSettingsSearch);
        const auto match=[&](const char* name){auto value=std::string(name),filter=m_ProjectSettingsSearch;for(auto& c:value)c=char(std::tolower(static_cast<unsigned char>(c)));for(auto& c:filter)c=char(std::tolower(static_cast<unsigned char>(c)));return value.find(filter)!=std::string::npos;};
        if(match("Project Name"))ImGui::InputText("Project Name",&m_ProjectSettingsDraft.name);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Display name only. Project folder and descriptor filename stay the same.");
        if(match("Startup Scene")){
        ImGui::InputText("Startup Scene",&m_ProjectSettingsDraft.startupScene);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Project-relative scene path, for example Scenes/Main.json. Applied the next time this project opens.");
        if(ImGui::Button("Choose Scene..."))ImGui::OpenPopup("StartupScenePicker");ImGui::SameLine();
        if(ImGui::Button("Refresh Scenes")){try{m_ProjectSettingsScenes.Refresh({});m_ProjectSettingsError.clear();}catch(const std::exception& e){m_ProjectSettingsError=e.what();}}
        if(ImGui::BeginPopup("StartupScenePicker")){
            for(const auto& scene:m_ProjectSettingsScenes.Entries())if(ImGui::Selectable(scene.key.c_str(),scene.key==m_ProjectSettingsDraft.startupScene))m_ProjectSettingsDraft.startupScene=scene.key;
            if(m_ProjectSettingsScenes.Entries().empty())ImGui::TextDisabled("No project scenes found.");ImGui::EndPopup();
        }
        }
        ImGui::EndDisabled();ImGui::Spacing();
        if(match("Project File")){ImGui::TextDisabled("Project File (read-only)");ImGui::TextWrapped("%s",AssetPaths::ToUtf8String(project->descriptorPath).c_str());}
        if(match("Format Version"))ImGui::Text("Format Version: %d (read-only)",project->formatVersion);
        if(match("Project Root")){ImGui::TextDisabled("Project Root (read-only)");ImGui::TextWrapped("%s",AssetPaths::ToUtf8String(project->root).c_str());}
        if(match("Assets Root")){ImGui::TextDisabled("Assets Root (read-only)");ImGui::TextWrapped("%s",AssetPaths::ToUtf8String(project->AssetRoot()).c_str());}
        if(match("Scenes Root")){ImGui::TextDisabled("Scenes Root (read-only)");ImGui::TextWrapped("%s",AssetPaths::ToUtf8String(project->SceneRoot()).c_str());}
        ImGui::TextWrapped("Root changes are not supported here because existing resource paths depend on them.");
    }else if(m_ProjectSettingsCategory==1){
        ImGui::SeparatorText("Rendering");ImGui::Text("Backend: %s (read-only)",app.GetRenderBackend()==RenderBackend::DX12?"DirectX 12":"Vulkan");
        ImGui::TextWrapped("Changing the graphics API requires engine startup configuration and a restart.");
        ImGui::BeginDisabled(app.IsEditorPlayMode());ImGui::Checkbox("GPU Instancing",&m_ProjectSettingsDraft.gpuInstancing);ImGui::EndDisabled();
        ImGui::TextWrapped("Batch compatible meshes. Applies immediately after Save & Apply, and when this project opens.");
        ImGui::Text("Current renderer: instancing %s",app.IsGpuInstancingEnabled()?"enabled":"disabled");ImGui::Separator();
        const auto aa=app.GetSceneSettings().antiAliasing;const char* modes[]={"Off","FXAA Low","FXAA High"};
        ImGui::Text("Scene Anti-Aliasing: %s",modes[std::clamp(int(aa),0,2)]);
        ImGui::TextWrapped("Anti-aliasing, shadow quality, environment, lighting and fog are saved with each scene.");
        if(ImGui::Button("Open Scene Settings"))ShowSceneBackground();
        ImGui::TextWrapped("VSync has no exposed engine control and is not a project setting.");
    }else if(m_ProjectSettingsCategory==2){
        ImGui::SeparatorText("Physics");ImGui::Text("Fixed timestep: %.6f seconds (%.0f Hz)",FixedStepClock::TickSeconds,1/FixedStepClock::TickSeconds);
        ImGui::TextWrapped("Read-only: the existing physics clock uses a fixed engine timestep. Project-level gravity and solver controls are not supported.");
        ImGui::TextWrapped("Physics settings in the engine startup configuration are separate from this project descriptor.");
    }else{
        ImGui::SeparatorText("Startup / Runtime");
        ImGui::TextWrapped("Startup Scene is selected once in General and loads when the project opens. Saving its path does not replace the scene you are editing.");
        ImGui::TextUnformatted("Window mode: Windowed (read-only)");
        if(auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow())){int width=0,height=0;glfwGetWindowSize(window->GetGlfwHandle(),&width,&height);ImGui::Text("Current window: %d x %d",width,height);}
        ImGui::TextWrapped("Window dimensions come from engine startup configuration. Project window overrides are not supported.");
        ImGui::Separator();ImGui::TextWrapped("UI, editor camera, grid, gizmo, placement tools and layout remain in Editor Preferences.");
    }
    ImGui::PopItemWidth();ImGui::EndChild();ImGui::Separator();
    const bool needsApply=project->gpuInstancing!=app.IsGpuInstancingEnabled();
    ImGui::BeginDisabled(app.IsEditorPlayMode()||(!ProjectSettingsDirty()&&!needsApply));if(ImGui::Button("Save & Apply"))SaveProjectSettingsDraft(app);ImGui::EndDisabled();
    ImGui::SameLine();if(ImGui::Button("Revert"))RevertProjectSettings(app);
    ImGui::SameLine();if(ImGui::Button("Close"))open=false;
    if(!m_ProjectSettingsError.empty())ImGui::TextWrapped("Error: %s",m_ProjectSettingsError.c_str());
    else if(!m_ProjectSettingsNotice.empty())ImGui::TextWrapped("%s",m_ProjectSettingsNotice.c_str());
    else ImGui::TextDisabled("Project descriptor: %s",AssetPaths::ToUtf8String(project->descriptorPath.filename()).c_str());
    ImGui::End();if(!open)close();
}
}
