#include "EditorTheme.h"
#include "EditorLayer.h"
#include "../core/Application.h"
#include "../core/AssetPaths.h"
#include "../resources/ResourceManager.h"
#include "../resources/loaders/MaterialLoader.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cfloat>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <commdlg.h>
#endif

namespace editor {
bool EditorLayer::ResolveProjectEdits(Application& app,int decision,std::string* error){
    if(decision==int(Application::ProjectDecision::Cancel))return false;
    if(app.IsPrefabEditing()){
        if(!(decision==int(Application::ProjectDecision::Save)?SavePrefabEditor(app):ClosePrefabEditor(app,true))){if(error)*error=m_EditorError;return false;}
    }
    if(decision!=int(Application::ProjectDecision::Save))return true;
    if(UIDraftsDirty()){
        if(app.IsEditorPlayMode()){if(error)*error="Stop Play before saving UI Documents and Styles";return false;}
        std::vector<std::string> documents,styles(m_DirtyUIStyles.begin(),m_DirtyUIStyles.end());
        for(auto& [origin,s]:m_UIDocuments)if(s.dirty)documents.push_back(s.resourceKey);
        for(auto& key:documents)if(!SaveUIDocument(app,key)){if(error)*error=m_EditorError;return false;}
        for(auto& key:styles)if(!SaveUIStyle(app,key)){if(error)*error=m_EditorError;return false;}
    }
    const bool sceneDirty=m_EditHistory?(m_EditHistory->external||m_EditHistory->revision!=m_EditHistory->saved):IsSceneDirty();
    if(sceneDirty&&!app.SaveAuthoringSceneForTransition(error))return false;
    if(sceneDirty&&m_EditHistory){m_EditHistory->saved=m_EditHistory->revision;m_EditHistory->external=false;}
    if(m_MaterialDirty&&!SaveMaterialDraft(app)){if(error)*error=m_MaterialError;return false;}

    if(ProjectSettingsDirty()&&!SaveProjectSettingsDraft(app)){if(error)*error=m_ProjectSettingsError;return false;}
    return true;
}
void EditorLayer::ResetProjectState(IRenderAdapter* renderer){
    ReleaseMaterialEditor(renderer);m_MaterialSessions.clear();
    StoreDocumentSession();m_Documents.clear();m_ActiveDocument.clear();m_ObservedScene="<initial>";m_DocumentProject.clear();m_PendingDocument.clear();m_DocumentPrompt=false;m_WorkspaceFocus=false;m_RestoreDocuments=true;m_ScriptKey.clear();m_ScriptText.clear();
    m_AssetDoubleClick.clear();m_PrefabActionKey.clear();m_PrefabConfirm=false;m_PrefabCreatePopup=false;m_PrefabAction=0;m_PrefabActionEntity={};m_PrefabRoot={};m_EditingPrefab.clear();m_PrefabDiskBase={};m_PrefabHistory.reset();
    m_UIDocuments.clear();m_UIStyles.clear();m_DirtyUIStyles.clear();m_DesignDocument.clear();m_UIDesignerKeys.clear();m_UIWidgetDrag.reset();m_UI4Selection.clear();m_UIStyleKey.clear();m_UIStyleId.clear();m_UIClipId.clear();
    m_EventFilter={};m_SelectedGameplayEvent=0;
    m_ShowProjectSettings=m_FocusProjectSettings=false;m_ProjectSettingsBase.reset();m_ProjectSettingsDraft={};m_ProjectSettingsScenes={};m_ProjectSettingsError.clear();m_ProjectSettingsNotice.clear();
    CancelPlacement();m_PlacementCancelled=false;
    if(ImGui::GetCurrentContext()){ImGui::ClearActiveID();ImGui::ClearDragDrop();}
    if(renderer)renderer->ReleaseEditorSky();m_RequestedSky.clear();m_FailedSkyVersion=0;
    m_BackgroundAssetDraft.clear();m_BackgroundAssetEditing=false;m_BackgroundDraftWorld=0;
    ReleaseAssetPreview(renderer);ResetHistory();m_EditHistory.reset();m_Clipboard={};
    m_SelectedAsset.clear();m_SelectedAssetKind.clear();m_EditingMaterial.clear();m_MaterialDirty=false;m_MaterialError.clear();
    if(!m_BrowserProject.empty())m_BrowserSessions[m_BrowserProject]={{"folder",m_AssetDirectory},{"expanded",m_AssetExpanded}};
    const bool all=m_AssetNavigation.projectSearch;m_BrowserProject.clear();m_AssetExpanded.clear();m_AssetSelection.clear();m_AssetAnchor.clear();m_AssetCategory=0;m_AssetKindFilter=0;
    m_AssetDirectory.clear();m_AssetNavigation={};m_AssetNavigation.projectSearch=all;m_AssetCatalog=AssetCatalog{};
    m_AssetMetadataKey.clear();m_AssetMetadata.clear();m_AssetMetadataVersion=0;
    m_MeshAssets.clear();m_TextureAssets.clear();m_MaterialAssets.clear();m_ShaderAssets.clear();m_EnvironmentAssets.clear();
    m_AssetListsDirty=true;m_RecentScenes.clear();m_SceneCatalog={};m_RenameTarget={};m_DragOriginal={};
    m_HierarchyVisible.clear();m_AssetActionSource.clear();m_AssetActionPath.clear();m_AssetActionBlockers.clear();m_AssetActionWarnings.clear();
    m_AssetActionRequested=m_FolderPopup=m_ImportPopup=m_RenameRequested=m_SceneDialogRequested=false;
    m_ImportSource.clear();m_ImportRoot.clear();m_ImportTarget.clear();m_ImportPreview.clear();m_ImportTargetFolder.clear();m_ImportPackageName.clear();
    m_SceneSearch.clear();m_AssetFilter.clear();m_EditorError.clear();m_BackgroundOverride=false;
}
void EditorLayer::ProjectOpened(Application& app){
    m_ShowProjectBrowser=app.IsProjectBrowserActive();
    m_SceneDialogKey=m_UnsavedSaveKey=app.DefaultSceneKey();
    if(!app.GetSceneKey().empty())RememberScene(app.GetSceneKey());
    if(auto p=app.GetProject()){
        const auto path=AssetPaths::ToUtf8String(p->descriptorPath);
        std::erase_if(m_RecentProjects,[&](const auto& entry){return entry.descriptor==path;});
        m_RecentProjects.insert(m_RecentProjects.begin(),{p->name,path});if(m_RecentProjects.size()>12)m_RecentProjects.resize(12);
    }
    if(app.GetWindow())PersistPreferences(app);
}

void EditorLayer::RequestProjectOpen(Application& app,const std::string& path,bool close){
    m_PendingProject=path;m_ProjectClose=close;m_ProjectExit=false;m_ProjectError.clear();
    if(HasProjectEdits())m_ProjectConfirm=true;
    else if(!ExecuteProjectAction(app,int(Application::ProjectDecision::Discard)))m_ShowProjectBrowser=true;
}
bool EditorLayer::ExecuteProjectAction(Application& app,int decision){
    if(m_ProjectExit){
        if(!ResolveProjectEdits(app,decision,&m_ProjectError))return false;
        m_ProjectExitConfirmed=true;return true;
    }
    const auto choice=static_cast<Application::ProjectDecision>(decision);
    bool ok=m_ProjectClose?app.CloseProject(choice,&m_ProjectError):app.OpenProject(std::filesystem::u8path(m_PendingProject),choice,&m_ProjectError);
    if(ok){m_ShowProjectBrowser=m_ProjectClose;m_PendingProject.clear();}
    return ok;
}
void EditorLayer::DrawProjectBrowser(Application& app){
    const bool startup=app.IsProjectBrowserActive();
    if(startup||m_ShowProjectBrowser){
        ImGuiWindowFlags flags=ImGuiWindowFlags_NoDocking;
        if(startup){
            const auto* viewport=ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->Pos);ImGui::SetNextWindowSize(viewport->Size);
            flags|=ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings;
        }else ImGui::SetNextWindowSize(ImVec2(760,620),ImGuiCond_FirstUseEver);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(24,24));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(10,10));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(10,7));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,12);
        ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(.055f,.065f,.085f,1));
        if(ImGui::Begin("Project Browser",startup?nullptr:&m_ShowProjectBrowser,flags)){
            if(startup){
                const auto space=ImGui::GetContentRegionAvail();
                const ImVec2 size(std::min(920.f*ImGui::GetStyle().FontScaleDpi,space.x),std::min((m_RecentProjects.empty()?500.f:660.f)*ImGui::GetStyle().FontScaleDpi,space.y));
                const auto cursor=ImGui::GetCursorPos();
                ImGui::SetCursorPos(ImVec2(cursor.x+(space.x-size.x)*.5f,cursor.y+(space.y-size.y)*.5f));
                ImGui::BeginChild("Workspace chooser",size,ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding);
                ImGui::PushFont(ImGui::GetIO().FontDefault,24);ImGui::TextUnformatted("WhispEngine");ImGui::PopFont();
                ImGui::TextUnformatted("Projects and workspaces");
                ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
            }
            if(auto p=app.GetProject()){
                ImGui::Text("Active: %s",p->name.c_str());
                ImGui::TextWrapped("%s",AssetPaths::ToUtf8String(p->root).c_str());
                ImGui::Text("Assets: %s | Scenes: %s",p->assetDirectory.c_str(),p->sceneDirectory.c_str());
                ImGui::TextWrapped("Startup: %s",p->startupScene.c_str());
                if(ImGui::Button("Use current scene as Startup"))app.SetStartupScene(app.GetSceneKey(),&m_ProjectError);
                ImGui::SameLine();if(ImGui::Button("Copy FPS Controller")){
                    try{auto target=p->ResolveAsset("scripts/FirstPersonController.lua");
                        if(!std::filesystem::copy_file(AssetPaths::ResolveEngineAssetRoot()/"scripts/FirstPersonController.lua",target,std::filesystem::copy_options::none))throw std::runtime_error("Controller already exists");
                        m_AssetListsDirty=true;m_ProjectError.clear();
                    }catch(const std::exception& e){m_ProjectError=e.what();}
                }
                ImGui::Separator();
            }else if(!startup)ImGui::TextWrapped("Create or open a game project, or continue in the current Legacy Workspace.");
            if(ImGui::BeginTabBar("ProjectTabs")){
                if(ImGui::BeginTabItem("Recent",nullptr,m_ProjectTab==0?ImGuiTabItemFlags_SetSelected:0)){
                    if(!m_RecentProjects.empty()&&ImGui::Button("Continue last project"))RequestProjectOpen(app,m_RecentProjects.front().descriptor);
                    const auto recent=m_RecentProjects;
                    ImGui::BeginChild("Recent projects",ImVec2(0,std::clamp(ImGui::GetContentRegionAvail().y-120*ImGui::GetStyle().FontScaleDpi,80.f,360*ImGui::GetStyle().FontScaleDpi)));
                    for(const auto& entry:recent){ImGui::PushID(entry.descriptor.c_str());
                        // Availability is validated by Open, not by presentation each frame.
                        if(ImGui::Selectable(entry.name.c_str()))RequestProjectOpen(app,entry.descriptor);
                        ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));ImGui::TextWrapped("%s",entry.descriptor.c_str());ImGui::PopStyleColor();
                        if(ImGui::SmallButton("Remove from recent")){std::erase(m_RecentProjects,entry);PersistPreferences(app);}ImGui::PopID();
                    }
                    if(recent.empty())ImGui::TextWrapped("No recent projects yet. Create a New Project or open an existing .whispproject file.");
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if(ImGui::BeginTabItem("New Project",nullptr,m_ProjectTab==1?ImGuiTabItemFlags_SetSelected:0)){
                    ImGui::TextUnformatted("Project Name");ImGui::SetNextItemWidth(-FLT_MIN);ImGui::InputText("##ProjectName",&m_ProjectName);
                    ImGui::TextUnformatted("Location (existing folder)");ImGui::SetNextItemWidth(-FLT_MIN);ImGui::InputText("##ProjectLocation",&m_ProjectLocation);
                    ImGui::TextUnformatted("Template");ImGui::SetNextItemWidth(-FLT_MIN);
                    int templateIndex=0;ImGui::Combo("##Template",&templateIndex,"Empty Project\0");
                    ImGui::TextWrapped("Creates a new project folder with Assets, Scenes/Main.json and Settings.");
                    if(ImGui::Button("Create")){
                        try{auto project=ProjectManager::Create(std::filesystem::u8path(m_ProjectLocation),m_ProjectName);
                            RequestProjectOpen(app,AssetPaths::ToUtf8String(project->descriptorPath));
                        }catch(const std::exception& e){m_ProjectError=e.what();}
                    }ImGui::EndTabItem();
                }
                if(ImGui::BeginTabItem("Open Project",nullptr,m_ProjectTab==2?ImGuiTabItemFlags_SetSelected:0)){
                    ImGui::TextUnformatted("Descriptor (.whispproject)");ImGui::SetNextItemWidth(-FLT_MIN);ImGui::InputText("##ProjectDescriptor",&m_ProjectDescriptor);
#ifdef _WIN32
                    if(ImGui::Button("Browse...")){
                        wchar_t file[32768]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.lpstrFile=file;dialog.nMaxFile=32768;
                        dialog.lpstrFilter=L"Whisp projects\0*.whispproject\0";dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
                        if(GetOpenFileNameW(&dialog))m_ProjectDescriptor=AssetPaths::ToUtf8String(std::filesystem::path(file));
                    }ImGui::SameLine();
#endif
                    if(ImGui::Button("Open"))RequestProjectOpen(app,m_ProjectDescriptor);
                    ImGui::EndTabItem();
                }ImGui::EndTabBar();m_ProjectTab=-1;
            }
            if(!m_ProjectError.empty()){ImGui::PushStyleColor(ImGuiCol_Text,Error);ImGui::TextWrapped("%s",m_ProjectError.c_str());ImGui::PopStyleColor();}
            if(startup){
                ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
                if(ImGui::Button("Legacy Workspace",ImVec2(180,0)))app.OpenLegacyWorkspace(&m_ProjectError);
                ImGui::SameLine();if(ImGui::Button("Exit",ImVec2(100,0)))m_ProjectExitConfirmed=true;
                ImGui::TextDisabled("Legacy Workspace uses the existing engine assets and scenes.");
                ImGui::EndChild();
            }
        }ImGui::End();
        ImGui::PopStyleColor();ImGui::PopStyleVar(4);
    }
    if(m_ProjectConfirm){ImGui::OpenPopup("Unsaved project changes");m_ProjectConfirm=false;}
    if(ImGui::BeginPopupModal("Unsaved project changes",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextWrapped("Save scene, material and Project Settings edits before continuing?");
        if(app.GetSceneKey().empty()){ImGui::InputText("Save untitled as",&m_UnsavedSaveKey);ImGui::TextDisabled("Choose a fresh scene filename inside the current scene directory.");}
        if(Decision("Save and continue")){
            if(app.GetSceneKey().empty()&&!app.IsEditorPlayMode()&&IsSceneDirty()){
                if(app.SaveSceneAs(m_UnsavedSaveKey,false,&m_ProjectError)&&ExecuteProjectAction(app,int(Application::ProjectDecision::Save)))ImGui::CloseCurrentPopup();
            }else if(ExecuteProjectAction(app,int(Application::ProjectDecision::Save)))ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();if(Decision("Discard",true)){if(ExecuteProjectAction(app,int(Application::ProjectDecision::Discard)))ImGui::CloseCurrentPopup();}
        ImGui::SameLine();if(ImGui::Button("Cancel")){m_PendingProject.clear();m_ProjectExit=false;ImGui::CloseCurrentPopup();}
        if(!m_ProjectError.empty()){ImGui::PushStyleColor(ImGuiCol_Text,Error);ImGui::TextWrapped("%s",m_ProjectError.c_str());ImGui::PopStyleColor();}ImGui::EndPopup();
    }
}
}
