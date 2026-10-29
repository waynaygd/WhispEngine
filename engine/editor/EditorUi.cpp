#include "EditorTheme.h"
#include "PrecisionInputs.h"
#include "EditorLayer.h"
#include "ScriptEditor.h"
#include "EditorWidgets.h"
#include "../core/Application.h"
#include "../core/AssetPaths.h"
#include "../resources/ResourceManager.h"
#include "../ecs/components/PlayerStartComponent.h"
#include "../ecs/components/CameraComponent.h"
#include "../ecs/components/FogComponent.h"
#include "../ecs/components/UICanvasComponent.h"
#include "../ecs/components/PostProcessVolumeComponent.h"
#include "../ecs/components/PrefabInstanceComponent.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <misc/cpp/imgui_stdlib.h>
#include <chrono>
#include <algorithm>
#include <cstdio>
namespace editor {
namespace {
bool Matches(const std::string& text,const std::string& filter){
    if(filter.empty())return true;
    return std::search(text.begin(),text.end(),filter.begin(),filter.end(),[](unsigned char a,unsigned char b){return std::tolower(a)==std::tolower(b);})!=text.end();
}
const char* EntityKind(const ecs::World& w,ecs::Entity e){
    if(w.HasComponent<ecs::CameraComponent>(e))return "[Camera]";
    if(w.HasComponent<ecs::UICanvasComponent>(e))return "[UI]";
    if(w.HasComponent<ecs::PostProcessVolumeComponent>(e))return "[PPV]";
    if(w.HasComponent<ecs::PrefabInstanceComponent>(e))return "[PF]";
    if(w.HasComponent<ecs::PlayerStartComponent>(e))return "[PlayerStart]";
    if(w.HasComponent<ecs::DirectionalLightComponent>(e))return "[D]";if(w.HasComponent<ecs::PointLightComponent>(e))return "[P]";if(w.HasComponent<ecs::SpotLightComponent>(e))return "[S]";if(w.HasComponent<ecs::FogComponent>(e))return "[Fog]";if(w.HasComponent<ecs::MeshRendererComponent>(e))return "[Mesh]";return "[Entity]";
}
}
void EditorLayer::InitializePreferences(Application& app){
    EditorPreferences p;std::string error;if(!LoadPreferences(PreferencesPath(),p,&error)&&!error.empty())Logger::Get().Warn(error);
    m_DocumentSessions=p.documentSessions;
    m_PlayInNewWindow=p.playInNewWindow;app.SetStandaloneWindowSize(p.gameWindowWidth,p.gameWindowHeight);
    m_RecentScenes=p.recentScenes;m_RecentProjects=p.recentProjects;
    m_Bookmarks=p.bookmarks;m_SelectionFilters=p.selectionFilters;
    m_ShowHierarchy=p.hierarchy;m_ShowInspector=p.inspector;m_ShowViewport=p.viewport;m_ShowAssetBrowser=p.assets;m_ShowMaterialEditor=p.material;m_ShowSceneSettings=p.settings;m_ShowConsole=p.console;m_ShowStatistics=p.diagnostics;
    m_AssetView=p.assetView;m_AssetTree=p.assetTree;m_AssetTreeWidth=p.assetTreeWidth;m_AssetDetails=p.assetDetails;m_AssetNavigation.projectSearch=p.assetSearchAll;m_BrowserSessions=p.browserSessions;
    m_AssetGrid=m_AssetView==0;m_ProjectAssets=p.projectAssets;m_AssetCardSize=p.assetCardSize;m_BackgroundOverride=p.backgroundOverride;m_ProjectileTestInput=p.projectileTestInput;
    m_SurfaceSnap=p.surfaceSnap;m_AlignToNormal=p.alignToNormal;m_SurfaceOffset=p.surfaceOffset;m_SurfaceRotationOffset=p.surfaceRotationOffset;
    m_ShowRuntimeUI=p.uiPreview;m_ShowVisualSettings=p.visualPanel;m_VisualSettings=p.visuals;m_ShowGrid=p.grid;m_ShowLightGizmos=p.lightGizmos;m_ShowCameraGizmos=p.cameraGizmos;m_ShowCameraFrustum=p.cameraFrustum;m_ShowCameraPreview=p.cameraPreview;m_LightXray=p.xray;m_LightXrayMode=p.lightXrayMode;m_TransformPrecision=p.transformPrecision;m_RotationSnapping=p.rotationSnapping;m_ShowPhysicsStress=p.physicsStress;m_Snapping=p.snapping;m_GizmoOperation=p.operation;m_GizmoMode=p.space;
    m_SnapTranslation=p.translationSnap;m_SnapRotation=p.rotationSnap;m_SnapScale=p.scaleSnap;app.SetCameraNavigation(p.cameraSpeed,p.cameraSensitivity);
    InitializeWorkspaces(p);
    RestoreViewportPlacement(app);
}
bool EditorLayer::PersistPreferences(Application& app){
    // End-of-session persistence retains the last normal layout, never F11's temporary viewport.
    if(ImGui::GetCurrentContext()&&!ImGui::GetCurrentContext()->WithinFrameScope)CaptureCurrentWorkspace();
    CaptureSceneDocument(app);StoreDocumentSession();
    EditorPreferences p;p.documentSessions=m_DocumentSessions;p.activeWorkspace=m_Workspace;p.workspaces=m_Workspaces;p.workspacesInitialized=true;p.recentScenes=app.GetProject()?std::vector<std::string>{}:m_RecentScenes;p.recentProjects=m_RecentProjects;p.hierarchy=m_ShowHierarchy;p.inspector=m_ShowInspector;p.viewport=m_ShowViewport;p.assets=m_ShowAssetBrowser;p.material=m_ShowMaterialEditor;p.settings=m_ShowSceneSettings;p.console=m_ShowConsole;p.diagnostics=m_ShowStatistics;
    const auto size=app.StandaloneWindowSize();p.gameWindowWidth=size[0];p.gameWindowHeight=size[1];p.playInNewWindow=m_PlayInNewWindow;
    if(!m_BrowserProject.empty())m_BrowserSessions[m_BrowserProject]={{"folder",m_AssetDirectory},{"expanded",m_AssetExpanded}};
    p.assetView=m_AssetView;p.assetTree=m_AssetTree;p.assetTreeWidth=m_AssetTreeWidth;p.assetDetails=m_AssetDetails;p.assetSearchAll=m_AssetNavigation.projectSearch;p.browserSessions=m_BrowserSessions;
    p.assetGrid=m_AssetGrid;p.projectAssets=m_ProjectAssets;p.assetCardSize=m_AssetCardSize;p.backgroundOverride=m_BackgroundOverride;p.projectileTestInput=m_ProjectileTestInput;
    p.bookmarks=m_Bookmarks;p.selectionFilters=m_SelectionFilters;
    p.surfaceSnap=m_SurfaceSnap;p.alignToNormal=m_AlignToNormal;p.surfaceOffset=m_SurfaceOffset;p.surfaceRotationOffset=m_SurfaceRotationOffset;
    p.uiPreview=m_ShowRuntimeUI;p.visualPanel=m_ShowVisualSettings;p.visuals=m_VisualSettings;p.grid=m_ShowGrid;p.lightGizmos=m_ShowLightGizmos;p.cameraGizmos=m_ShowCameraGizmos;p.cameraFrustum=m_ShowCameraFrustum;p.cameraPreview=m_ShowCameraPreview;p.xray=m_LightXray;p.lightXrayMode=m_LightXrayMode;p.transformPrecision=m_TransformPrecision;p.rotationSnapping=m_RotationSnapping;p.physicsStress=m_ShowPhysicsStress;p.snapping=m_Snapping;p.operation=m_GizmoOperation;p.space=m_GizmoMode;
    p.translationSnap=m_SnapTranslation;p.rotationSnap=m_SnapRotation;p.scaleSnap=m_SnapScale;p.cameraSpeed=app.GetCameraMoveSpeed();p.cameraSensitivity=app.GetCameraSensitivity();std::string error;if(!SavePreferences(PreferencesPath(),p,&error)){m_EditorError=error;Logger::Get().Warn(error);return false;}return true;
}
void EditorLayer::DrawRotationSnapControls(){
    ImGui::Checkbox("Rotate Snap",&m_RotationSnapping);
    Help("Snap gizmo rotations in Scene and Prefab Editor. Uses the same toggle and step as Inspector Transform Options; no Entity data or Undo entry.");
    ImGui::SameLine();ImGui::SetNextItemWidth(86*ImGui::GetStyle().FontScaleDpi);
    char preview[32];std::snprintf(preview,sizeof(preview),"%g deg",double(m_SnapRotation));
    if(ImGui::BeginCombo("##Rotation step",preview)){
        for(float step:{5.f,10.f,15.f,30.f,45.f,90.f}){
            char label[32];std::snprintf(label,sizeof(label),"%g deg",double(step));
            if(ImGui::Selectable(label,m_SnapRotation==step))m_SnapRotation=step;
        }
        ImGui::Separator();ImGui::SetNextItemWidth(130*ImGui::GetStyle().FontScaleDpi);
        PrecisionDragFloat("Custom step (deg)",&m_SnapRotation,.1f,.1f,180);
        ImGui::EndCombo();
    }
    Help("Rotation snap step: 5, 10, 15, 30, 45, 90 degrees, or Custom. Ctrl-click Custom to type. Saved by the existing Editor Preferences policy.");
}
void EditorLayer::DrawViewportTransformControls(Application& app){
    const float dpi=ImGui::GetStyle().FontScaleDpi;
    ImGui::PushID("ViewportTransform");
    const bool blocked=app.IsEditorPlayMode()||(!ActiveDocumentIs(DocumentType::Scene)&&!ActiveDocumentIs(DocumentType::Prefab));
    auto tools=[&]{
        ImGui::BeginDisabled(blocked);
        for(int i=0;i<3;++i){if(i)ImGui::SameLine();const char* names[]={"Move","Rotate","Scale"};const char* tips[]={"Move selection (W)","Rotate selection (E)","Scale selection (R)"};
            if(Action(names[i],blocked?"Activate a Scene/Prefab document and stop Play to edit transforms.":tips[i],true,m_GizmoOperation==i,80))m_GizmoOperation=i;
        }
        ImGui::SameLine();ImGui::SetNextItemWidth(82*dpi);
        auto* t=app.GetWorld().GetComponent<ecs::TransformComponent>(m_Selection.active);
        const bool localScale=m_Selection.items.size()==1&&m_GizmoOperation==2&&t&&(std::abs(t->rotation.x)+std::abs(t->rotation.y)+std::abs(t->rotation.z)>1e-6f);
        if(localScale)m_GizmoMode=0;
        ImGui::BeginDisabled(localScale);ImGui::Combo("##Transform space",&m_GizmoMode,"Local\0World\0");ImGui::EndDisabled();
        Help(localScale?"Rotated scale uses Local axes to avoid unsupported shear.":"Local / World axes. Shared with the main toolbar, Scene and Prefab gizmos.");
        ImGui::EndDisabled();
    };
    const bool narrow=ImGui::GetContentRegionAvail().x<600*dpi;
    if(narrow){
        if(ImGui::Button("Transform"))ImGui::OpenPopup("Transform tools");Help("Move (W), Rotate (E), Scale (R), Local / World. Expanded here when the viewport is narrow.");
        if(ImGui::BeginPopup("Transform tools")){tools();ImGui::EndPopup();}
    }else tools();
    // Snap is always reachable; wrap the whole group together on very narrow docks.
    ImGui::SameLine();if(ImGui::GetContentRegionAvail().x<215*dpi)ImGui::NewLine();
    ImGui::BeginDisabled(blocked);
    if(ImGui::GetContentRegionAvail().x<215*dpi){
        char label[64];std::snprintf(label,sizeof(label),"Snap: %s / %g deg",m_RotationSnapping?"ON":"OFF",double(m_SnapRotation));
        if(ImGui::Button(label))ImGui::OpenPopup("Rotation snap options");Help("Rotation snap toggle, presets and custom step.");
        if(ImGui::BeginPopup("Rotation snap options")){DrawRotationSnapControls();ImGui::EndPopup();}
    }else DrawRotationSnapControls();
    ImGui::EndDisabled();
    ImGui::PopID();
}
void EditorLayer::DrawToolbar(Application& app){
    auto* viewport=ImGui::GetMainViewport();const float dpi=ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextWindowPos(viewport->WorkPos);ImGui::SetNextWindowSize({viewport->WorkSize.x,ImGui::GetFrameHeightWithSpacing()+8});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{10*dpi,4*dpi});
    editor::BeginEditorPanel("Main Toolbar",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);
    const bool play=app.IsEditorPlayMode(),prefab=app.IsPrefabEditing();const bool compact=viewport->WorkSize.x<1080*dpi;
    auto separator=[](){ImGui::SameLine();ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);ImGui::SameLine();};
    if(Action("Save",play?"Stop Play before saving.":"Save the active document (Ctrl+S).",!play,false,58))if(!SaveActiveDocument(app))Logger::Get().Error(m_EditorError);
    separator();
    if(Action("Play",prefab?"Return to the Scene document before starting Play.":!ActiveDocumentIs(DocumentType::Scene)?"Activate the Scene document to start Play.":m_PlayInNewWindow?"Run Game Startup in a separate native window.":"Start the scene in an isolated Runtime World.",!play&&!prefab&&ActiveDocumentIs(DocumentType::Scene),play,55)){if(m_PlayInNewWindow)app.RequestPlayInNewWindow();else app.SetEditorPlayMode(true);}
    ImGui::SameLine();ImGui::BeginDisabled(play);
    if(ImGui::ArrowButton("Play mode",ImGuiDir_Down))ImGui::OpenPopup("Play mode options");
    Help("Play In Editor / Play In New Window. New Window runs the project's Game Startup settings.");
    if(ImGui::BeginPopup("Play mode options")){
        if(ImGui::MenuItem("In Editor",nullptr,!m_PlayInNewWindow))m_PlayInNewWindow=false;
        if(ImGui::MenuItem("New Window",nullptr,m_PlayInNewWindow,app.SupportsPlayInNewWindow()))m_PlayInNewWindow=true;
        if(!app.SupportsPlayInNewWindow())ImGui::TextDisabled("New Window is currently supported by DX12.");
        ImGui::EndPopup();
    }ImGui::EndDisabled();
    ImGui::SameLine();if(Action(app.IsEditorPaused()?"Resume":"Pause","Pause/resume the running scene. Start Play first.",play,app.IsEditorPaused(),75))app.SetEditorPaused(!app.IsEditorPaused());
    ImGui::SameLine();if(Action("Stop","Discard the Runtime World and return to editing. Start Play first.",play,false,55))app.SetEditorPlayMode(false);
    separator();
    auto transforms=[&](){
        const char* blocked=play?"Stop Play to edit authored transforms.":(!ActiveDocumentIs(DocumentType::Scene)&&!ActiveDocumentIs(DocumentType::Prefab))?"Activate a Scene or Prefab document to use transform tools.":nullptr;
        ImGui::BeginDisabled(blocked!=nullptr);
        for(int i=0;i<3;++i){if(i)ImGui::SameLine();const char* names[]={"Move","Rotate","Scale"};if(Action(names[i],blocked?blocked:i==0?"Move selection (W)":i==1?"Rotate selection (E)":"Scale selection (R)",true,m_GizmoOperation==i,80))m_GizmoOperation=i;}
        auto* t=app.GetWorld().GetComponent<ecs::TransformComponent>(m_Selection.active);
        const bool localScale=m_Selection.items.size()==1&&m_GizmoOperation==2&&t&&(std::abs(t->rotation.x)+std::abs(t->rotation.y)+std::abs(t->rotation.z)>1e-6f);if(localScale)m_GizmoMode=0;
        ImGui::SameLine();if(Action("Local",blocked?blocked:"Use local transform axes.",true,m_GizmoMode==0,52))m_GizmoMode=0;
        ImGui::SameLine();if(Action("World",blocked?blocked:localScale?"Rotated non-uniform World scale needs unsupported shear; use Local.":"Use world transform axes.",!localScale,m_GizmoMode==1,55))m_GizmoMode=1;
        if(compact)ImGui::NewLine();else ImGui::SameLine();
        ImGui::Checkbox(m_GizmoOperation==1?"Rotation Snap":"Snap",m_GizmoOperation==1?&m_RotationSnapping:&m_Snapping);Help(blocked?blocked:"Snap the active transform tool. Steps remain in Transform Options.");
        ImGui::EndDisabled();
    };
    if(compact){if(ImGui::Button("Tools"))ImGui::OpenPopup("Transform tools");if(ImGui::BeginPopup("Transform tools")){transforms();ImGui::EndPopup();}}else transforms();
    separator();
    if(ImGui::Button("Workspace"))ImGui::OpenPopup("Workspace actions");
    if(ImGui::BeginPopup("Workspace actions")){
        DrawWorkspaceMenu(app);
        if(ImGui::MenuItem(m_WorkspaceFocus?"Restore Layout":"Maximize Workspace",nullptr,m_WorkspaceFocus,!m_ViewportMaximized))SetWorkspaceFocused(!m_WorkspaceFocus);
        if(m_ViewportMaximized)ImGui::TextDisabled("Leave F11 before maximizing a workspace.");
        if(prefab&&ImGui::MenuItem("Return to Scene"))BackToSceneDocument(app);
        if(ImGui::MenuItem("Focus Selection","F",false,!m_Selection.items.empty()))FocusSelection(app);
        ImGui::EndPopup();
    }
    if(!compact){ImGui::SameLine();if(Action(m_WorkspaceFocus?"Restore Layout":"Maximize Workspace","Expand the current document workspace; F11 remains the Scene viewport command.",!m_ViewportMaximized,m_WorkspaceFocus,155))SetWorkspaceFocused(!m_WorkspaceFocus);}
    ImGui::End();ImGui::PopStyleVar();
    if(!app.GetPlayError().empty()){
        bool visible=true;ImGui::SetNextWindowSize({520*dpi,0},ImGuiCond_Appearing);
        if(editor::BeginEditorPanel("Play failed",&visible,ImGuiWindowFlags_AlwaysAutoResize))ImGui::TextWrapped("%s",app.GetPlayError().c_str());
        ImGui::End();if(!visible)app.DismissPlayError();
    }
}
void EditorLayer::DrawMainMenu(Application& app){
    if(!ImGui::BeginMainMenuBar())return;
    bool preferencesRequested=false;
    auto action=[&](int kind){m_SceneDialogAction=kind;m_SceneDialogRequested=true;};
    if(ImGui::BeginMenu("File")){
        if(ImGui::MenuItem("New Project...")){m_ShowProjectBrowser=true;m_ProjectTab=1;}
        if(ImGui::MenuItem("Open Project...")){m_ShowProjectBrowser=true;m_ProjectTab=2;}
        if(ImGui::BeginMenu("Recent Projects",!m_RecentProjects.empty())){
            const auto recent=m_RecentProjects;for(const auto& entry:recent)if(ImGui::MenuItem(entry.name.c_str()))RequestProjectOpen(app,entry.descriptor);ImGui::EndMenu();
        }
        if(ImGui::MenuItem("Project Settings...",nullptr,false,true))ShowProjectSettings(app);
        if(ImGui::MenuItem("Close Project",nullptr,false,!app.IsProjectBrowserActive()))RequestProjectOpen(app,{},true);
        ImGui::Separator();
        if(ImGui::MenuItem("Open Document..."))m_OpenDocumentPrompt=true;
        ImGui::BeginDisabled(app.IsPrefabEditing());
        if(ImGui::MenuItem("New Scene"))action(1);
        if(app.IsPrefabEditing()&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Return to the Scene document before creating a new scene.");
        if(ImGui::MenuItem("Open Scene"))action(2);
        if(app.IsPrefabEditing()&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Use Open Document or the Scene tab to leave Prefab Editor safely.");
        if(ImGui::BeginMenu("Recent Scenes",!m_RecentScenes.empty())){const auto recent=m_RecentScenes;for(const auto& key:recent)if(ImGui::MenuItem(key.c_str())){RequestRecentScene(app,key);}ImGui::EndMenu();}
        ImGui::EndDisabled();
        if(ImGui::MenuItem("Save","Ctrl+S",false,!app.IsEditorPlayMode())){if(!SaveActiveDocument(app))Logger::Get().Error(m_EditorError);}
        if(ImGui::MenuItem("Save All",nullptr,false,!app.IsEditorPlayMode()))SaveAllDocuments(app);
        if(ImGui::MenuItem("Save As","Ctrl+Shift+S",false,!app.IsEditorPlayMode()&&!app.IsPrefabEditing()&&ActiveDocumentIs(DocumentType::Scene)))action(3);
        if(ImGui::MenuItem("Reopen Scene",nullptr,false,!app.GetSceneKey().empty()&&!app.IsPrefabEditing())){m_SceneDialogKey=app.GetSceneKey();action(2);}
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Edit")){
        if(ImGui::MenuItem("Undo","Ctrl+Z",false,CanUndoActiveDocument()))UndoActiveDocument(app);
        if(ImGui::MenuItem("Redo","Ctrl+Y",false,CanUndoActiveDocument(true)))UndoActiveDocument(app,true);
        ImGui::BeginDisabled(!ActiveDocumentIs(DocumentType::Scene)&&!app.IsPrefabEditing());
        if(ImGui::MenuItem("Copy","Ctrl+C",false,!app.IsEditorPlayMode()))CopyEntity(app,m_Selection.active);
        if(ImGui::MenuItem("Paste","Ctrl+V",false,!app.IsEditorPlayMode()&&m_Clipboard.is_object()))PasteEntity(app);
        if(ImGui::MenuItem("Clear Selection",nullptr,false,!m_SelectionLocked)){FinishTransaction(app);ClearSelection();}
        if(ImGui::MenuItem("Duplicate","Ctrl+D",false,app.GetWorld().IsAlive(m_Selection.active)))DuplicateEntity(app,m_Selection.active);
        if(ImGui::MenuItem("Delete selection","Delete",false,!m_Selection.items.empty()))DeleteSelection(app);ImGui::EndDisabled();ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("View")){
        ImGui::MenuItem("Editor Visuals",nullptr,&m_ShowVisualSettings);
        ImGui::MenuItem("Game View (shared viewport)","G",&m_GameView);
        ImGui::MenuItem("Scene Hierarchy",nullptr,&m_ShowHierarchy);ImGui::MenuItem("Inspector",nullptr,&m_ShowInspector);ImGui::MenuItem("Viewport",nullptr,&m_ShowViewport);ImGui::MenuItem("Asset Browser",nullptr,&m_ShowAssetBrowser);ImGui::MenuItem("Material Editor",nullptr,&m_ShowMaterialEditor);ImGui::MenuItem("Scene Settings / Lighting",nullptr,&m_ShowSceneSettings);ImGui::MenuItem("Console / Diagnostics",nullptr,&m_ShowConsole);ImGui::MenuItem("Runtime Errors",nullptr,&m_ShowRuntimeErrors);ImGui::MenuItem("Gameplay Events",nullptr,&m_ShowGameplayEvents);ImGui::MenuItem("Scene Validation",nullptr,&m_ShowSceneValidation);ImGui::MenuItem("Lua Runtime Inspector",nullptr,&m_ShowLuaRuntimeInspector);ImGui::MenuItem("Statistics",nullptr,&m_ShowStatistics);ImGui::MenuItem("Physics Stress Test",nullptr,&m_ShowPhysicsStress);
        ImGui::MenuItem("Canvas Designer",nullptr,&m_ShowCanvasDesigner);ImGui::MenuItem("Widget Hierarchy",nullptr,&m_ShowWidgetHierarchy);ImGui::MenuItem("Widget Inspector",nullptr,&m_ShowWidgetInspector);
        DrawWorkspaceMenu(app);
        ImGui::Separator();if(ImGui::MenuItem("Reset Layout"))ResetCurrentWorkspace(app);
        if(ImGui::MenuItem("Navigation and snapping"))preferencesRequested=true;ImGui::MenuItem("ImGui Demo",nullptr,&m_ShowDemoWindow);ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Scene")){
        if(ImGui::MenuItem("Focus Selected","F"))FocusSelection(app);
        if(ImGui::MenuItem("Drop to Floor",nullptr,false,!app.IsEditorPlayMode()&&!m_Selection.items.empty()))DropSelectionToFloor(app);
        ImGui::Separator();if(ImGui::MenuItem("Play From Game Startup Map",nullptr,false,bool(app.GetProject())&&!app.IsEditorPlayMode()&&!app.IsPrefabEditing()))app.PlayFromGameStartupMap();if(ImGui::MenuItem("Play",nullptr,false,!app.IsEditorPlayMode()&&!app.IsPrefabEditing()))app.SetEditorPlayMode(true);if(ImGui::MenuItem("Pause / Resume",nullptr,false,app.IsEditorPlayMode()))app.SetEditorPaused(!app.IsEditorPaused());if(ImGui::MenuItem("Stop",nullptr,false,app.IsEditorPlayMode()))app.SetEditorPlayMode(false);ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Help")){ImGui::TextUnformatted("WhispEngine Editor 2.5");ImGui::TextUnformatted("RMB + WASD: camera, F: focus, W/E/R: gizmo");ImGui::TextUnformatted("Play clones scene data; Stop discards Runtime edits.");ImGui::TextUnformatted("Ctrl-click: toggle selection; Shift-click: Hierarchy range / Viewport add.");ImGui::TextUnformatted("Multiple selection: shared W/E/R gizmo; Inspector edits Active Entity only.");ImGui::TextUnformatted("Drag in Hierarchy to parent; drop on Scene Root to unparent.");ImGui::EndMenu();}
    if(preferencesRequested)ImGui::OpenPopup("EditorPreferences");
    if(ImGui::BeginPopup("EditorPreferences")){
        ImGui::Checkbox("Enable Projectile Test Input (Play / R)",&m_ProjectileTestInput);
        float speed=app.GetCameraMoveSpeed(),sensitivity=app.GetCameraSensitivity();if(PrecisionDragFloat("Camera speed",&speed,.1f,.2f,25))app.SetCameraNavigation(speed,sensitivity);if(PrecisionDragFloat("Sensitivity",&sensitivity,.0001f,.0001f,.02f))app.SetCameraNavigation(speed,sensitivity);
        PrecisionDragFloat("Move snap",&m_SnapTranslation,.05f,.001f,1000);PrecisionDragFloat("Rotate snap (deg)",&m_SnapRotation,.5f,.1f,180);PrecisionDragFloat("Scale snap",&m_SnapScale,.01f,.001f,100);if(ImGui::Button("Save preferences"))PersistPreferences(app);ImGui::EndPopup();
    }
    ImGui::EndMainMenuBar();
}
void EditorLayer::DrawSceneDialogs(Application& app){
    if(m_SceneDialogRequested){
        try{if(m_SceneDialogAction==2)m_SceneCatalog.Refresh({AssetPaths::ResolveAssetRoot(),AssetImport::ProjectRoot()});}catch(const std::exception& e){m_EditorError=e.what();}
        ImGui::OpenPopup(m_SceneDialogAction==5?"Scene error":"Scene action");m_SceneDialogRequested=false;
    }
    auto* main=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(main->GetWorkCenter(),ImGuiCond_Appearing,ImVec2(.5f,.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(640.f,main->WorkSize.x*.9f),std::min(460.f,main->WorkSize.y*.9f)),ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(300.f,main->WorkSize.x*.9f),std::min(160.f,main->WorkSize.y*.9f)),ImVec2(main->WorkSize.x*.95f,main->WorkSize.y*.95f));
    if(ImGui::IsPopupOpen("Scene error")){if(ImGui::BeginPopupModal("Scene error",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextWrapped("%s",m_EditorError.c_str());if(ImGui::Button("Close"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}return;}
    ImGui::SetNextWindowPos(main->GetWorkCenter(),ImGuiCond_Appearing,ImVec2(.5f,.5f));
    if(ImGui::BeginPopupModal("Scene action",nullptr,ImGuiWindowFlags_None)){
        if(app.IsEditorPlayMode()&&m_SceneDialogAction!=3)ImGui::TextWrapped("Opening or creating a scene stops Play and discards the Runtime World. Cancel keeps Play running.");
        if(IsSceneDirty()&&m_SceneDialogAction!=3)ImGui::TextWrapped("Unsaved scene edits will be discarded. Save first, or explicitly discard.");
        if(m_SceneDialogAction==2){
            ImGui::InputTextWithHint("##sceneSearch","Search project scenes",&m_SceneSearch);
            ImGui::BeginChild("SceneBrowser",ImVec2(0,std::clamp(ImGui::GetContentRegionAvail().y*.55f,80.f,240.f)),ImGuiChildFlags_Borders);
            ImGui::TextDisabled("Recent scenes (availability checked on Open)");
            for(const auto& key:m_RecentScenes)if(ImGui::Selectable(("Recent: "+key).c_str(),key==m_SceneDialogKey)){m_SceneDialogKey=key;}
            ImGui::Separator();for(const auto& entry:m_SceneCatalog.Entries())if(Matches(entry.key,m_SceneSearch)){const auto& key=entry.key;
                if(ImGui::Selectable((entry.name+" | "+key+" | "+entry.modified).c_str(),key==m_SceneDialogKey))m_SceneDialogKey=key;
                if(ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0)){m_SceneDialogKey=key;if(!IsSceneDirty()){std::string error;if(OpenSceneGuarded(app,key,OpenDecision::Discard,&error))ImGui::CloseCurrentPopup();else m_EditorError=error;}}
            }ImGui::EndChild();if(ImGui::Button("New Scene..."))m_SceneDialogAction=1;
        }
        if(m_SceneDialogAction==4)ImGui::TextWrapped("Open recent scene: %s",m_SceneDialogKey.c_str());
        if(m_SceneDialogAction!=1&&m_SceneDialogAction!=4)ImGui::InputText("Scene key (project-relative JSON)",&m_SceneDialogKey);
        if(IsSceneDirty()&&app.GetSceneKey().empty()&&m_SceneDialogAction!=3){ImGui::InputText("Save current as (new key)",&m_UnsavedSaveKey);ImGui::TextWrapped("Untitled: choose a fresh scene key. Existing files are protected.");}
        std::filesystem::path path;try{path=app.GetProject()?app.GetProject()->ResolveScene(m_SceneDialogKey):AssetPaths::ResolveAssetOutputPath(m_SceneDialogKey);}catch(...){}
        const bool exists=m_SceneDialogAction==3&&!path.empty()&&std::filesystem::exists(path);
        static bool confirmed=false;
        if(exists)ImGui::Checkbox("Confirm replacement of existing scene",&confirmed);
        if(ImGui::Button(m_SceneDialogAction==1?"Discard and New":(m_SceneDialogAction==2||m_SceneDialogAction==4)?"Discard and Open":"Save As")){
            bool ok=true;std::string error;if(m_SceneDialogAction==1)app.NewScene();else if(m_SceneDialogAction==2||m_SceneDialogAction==4)ok=OpenSceneGuarded(app,m_SceneDialogKey,OpenDecision::Discard,&error);else if(exists&&!confirmed){ok=false;error="Confirm destination replacement or choose another key";}else ok=app.SaveSceneAs(m_SceneDialogKey,exists&&confirmed,&error);
            if(ok){confirmed=false;ImGui::CloseCurrentPopup();}else {m_EditorError=error;Logger::Get().Error(error);}
        }
        ImGui::SameLine();if(ImGui::Button("Save current")){std::string error;const bool saved=app.GetSceneKey().empty()?app.SaveSceneAs(m_UnsavedSaveKey,false,&error):app.SaveCurrentScene(&error);if(!saved){m_EditorError=error;Logger::Get().Error(error);}else if(m_SceneDialogAction==4&&OpenSceneGuarded(app,m_SceneDialogKey,OpenDecision::Discard,&m_EditorError))ImGui::CloseCurrentPopup();}
        ImGui::SameLine();if(ImGui::Button("Cancel")){confirmed=false;ImGui::CloseCurrentPopup();}if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::EndPopup();
    }
}
void EditorLayer::DrawSceneHierarchy(Application& app){
    const auto start=std::chrono::steady_clock::now();ImGui::SetNextWindowSizeConstraints(ImVec2(180,150),ImVec2(FLT_MAX,FLT_MAX));
    if(!editor::BeginEditorPanel("Scene Hierarchy",&m_ShowHierarchy)){ImGui::End();return;}
    m_HierarchyFocused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    auto& world=app.GetWorld();
    ImGui::InputTextWithHint("##filter","Search entities",&m_HierarchyFilter);
    const auto entities=world.Entities();PruneSelection(app);
    if(!app.IsEditorPlayMode()&&!ImGui::GetIO().WantTextInput&&!m_ModalActive&&ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)&&ImGui::IsKeyPressed(ImGuiKey_F2,false)&&world.IsAlive(m_Selection.active)){
        m_RenameTarget=m_Selection.active;auto* tag=world.GetComponent<ecs::TagComponent>(m_RenameTarget);m_RenameBuffer=tag?tag->name:"Entity";m_RenameRequested=true;
    }
    if(app.IsEditorPlayMode()||!world.IsAlive(m_RenameTarget))m_RenameTarget={};
    std::unordered_map<ecs::Entity,std::vector<ecs::Entity>,ecs::EntityHash> children;
    std::unordered_set<ecs::Entity,ecs::EntityHash> matches;
    for(auto e:entities){children[world.Parent(e)].push_back(e);auto* tag=world.GetComponent<ecs::TagComponent>(e);
        if(Matches(tag?tag->name:std::string(),m_HierarchyFilter)){matches.insert(e);for(auto p=world.Parent(e);p.IsValid();p=world.Parent(p))matches.insert(p);}}
    struct Row{ecs::Entity entity;int depth;bool branch;};std::vector<Row> rows;
    auto flatten=[&](auto&& self,ecs::Entity e,int depth)->void{
        if(!matches.contains(e))return;const bool branch=!children[e].empty();rows.push_back({e,depth,branch});
        ImGui::PushID(int(e.index));ImGui::PushID(int(e.generation));const bool expanded=!m_HierarchyFilter.empty()||ImGui::GetStateStorage()->GetInt(ImGui::GetID("###EntityRow"),1);ImGui::PopID();ImGui::PopID();
        if(expanded)for(auto child:children[e])self(self,child,depth+1);
    };for(auto root:children[{}])flatten(flatten,root,0);
    m_HierarchyVisible.clear();for(const auto& row:rows)m_HierarchyVisible.push_back(row.entity);
    auto acceptParent=[&](ecs::Entity parent){if(ImGui::BeginDragDropTarget()){
        if(const auto* payload=ImGui::AcceptDragDropPayload("WHISP_ENTITY"))if(payload->DataSize==sizeof(ecs::Entity)&&!app.IsEditorPlayMode())ReparentEntity(app,*static_cast<const ecs::Entity*>(payload->Data),parent,!m_HierarchyKeepLocal);
        ImGui::EndDragDropTarget();}};
    ImGuiListClipper clipper;clipper.Begin(int(rows.size()));while(clipper.Step())for(int index=clipper.DisplayStart;index<clipper.DisplayEnd;++index){
        const auto& row=rows[index];const auto e=row.entity;const auto* tag=world.GetComponent<ecs::TagComponent>(e);
        ImGui::PushID(int(e.index));ImGui::PushID(int(e.generation));if(row.depth)ImGui::Indent(row.depth*ImGui::GetStyle().IndentSpacing);
        std::string provenance;if(!m_PrefabAuthored.is_null()){auto member=PrefabMember(app,e);bool local=false;for(auto& n:m_PrefabAuthored.at("addedEntities"))if(n.at("id")==member)local=true;provenance=local?"[Added] ":"[Inherited] ";}
        const std::string label=provenance+std::string(EntityKind(world,e))+" "+(tag&&!tag->name.empty()?tag->name:"Entity "+std::to_string(e.index));
        const std::string rowLabel=(m_RenameTarget==e?std::string{}:label+(e==m_Selection.active?" [Active]":""))+"###EntityRow";
        if(!m_HierarchyFilter.empty())ImGui::SetNextItemOpen(true);
        ImGui::TreeNodeEx(rowLabel.c_str(),ImGuiTreeNodeFlags_NoTreePushOnOpen|ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_SpanAvailWidth|ImGuiTreeNodeFlags_DefaultOpen|(row.branch?0:ImGuiTreeNodeFlags_Leaf)|(IsSelected(e)?ImGuiTreeNodeFlags_Selected:0));
        // Keep the current Inspector while dragging another entity into one of
        // its reference fields. A normal row click selects on release.
        const auto down=ImGui::GetIO().MouseClickedPos[0];const auto minimum=ImGui::GetItemRectMin(),maximum=ImGui::GetItemRectMax();
        const bool beganHere=down.x>=minimum.x&&down.x<maximum.x&&down.y>=minimum.y&&down.y<maximum.y;
        const bool arrow=row.branch&&down.x<minimum.x+ImGui::GetTreeNodeToLabelSpacing();
        if(beganHere&&!arrow&&ImGui::IsItemHovered()&&ImGui::IsMouseReleased(0)&&!ImGui::IsMouseDragPastThreshold(0))SelectHierarchyEntity(app,e,ImGui::GetIO().KeyCtrl,ImGui::GetIO().KeyShift,m_HierarchyVisible);
        if(ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0))app.FocusEntity(e);
        acceptParent(e);
        if(ImGui::BeginDragDropSource()){ImGui::SetDragDropPayload("WHISP_ENTITY",&e,sizeof(e));ImGui::TextUnformatted(label.c_str());ImGui::TextDisabled(m_HierarchyKeepLocal?"Keep Local Transform":"Keep World Transform");ImGui::EndDragDropSource();}
        if(ImGui::BeginPopupContextItem("EntityActions")){
            if(ImGui::MenuItem("Focus")){if(IsSelected(e))FocusSelection(app);else app.FocusEntity(e);}if(ImGui::MenuItem("Rename","F2",false,!app.IsEditorPlayMode())){m_RenameTarget=e;m_RenameBuffer=tag?tag->name:"Entity";m_RenameRequested=true;}
            ImGui::BeginDisabled(app.IsEditorPlayMode());
            if(ImGui::MenuItem("Create Child"))CreateChild(app,e);
            if(ImGui::MenuItem("Create Prefab...",nullptr,false,!app.IsPrefabEditing())){SelectEntity(e);m_PrefabCreatePopup=true;}
            if(ImGui::MenuItem("Unparent",nullptr,false,world.Parent(e).IsValid()))ReparentEntity(app,e,{},!m_HierarchyKeepLocal);
            ImGui::Checkbox("Keep Local Transform",&m_HierarchyKeepLocal);
            if(ImGui::BeginMenu("Reparent...")){for(auto candidate:entities)if(candidate!=e){auto* name=world.GetComponent<ecs::TagComponent>(candidate);ImGui::PushID(int(candidate.index));if(ImGui::MenuItem(name?name->name.c_str():"Entity"))ReparentEntity(app,e,candidate,!m_HierarchyKeepLocal);ImGui::PopID();}ImGui::EndMenu();}
            if(ImGui::MenuItem(IsSelected(e)&&m_Selection.items.size()>1?"Duplicate selection":"Duplicate entity"))DuplicateEntity(app,e);
            if(ImGui::MenuItem("Drop to Floor")){if(!IsSelected(e))SelectEntity(e);DropSelectionToFloor(app);}
            if(ImGui::MenuItem("Delete selection")){if(!IsSelected(e))SelectEntity(e);DeleteSelection(app);}ImGui::EndDisabled();ImGui::EndPopup();
        }
        if(m_RenameTarget==e){
            ImGui::SameLine();ImGui::SetNextItemWidth(-FLT_MIN);if(m_RenameRequested){ImGui::SetKeyboardFocusHere();m_RenameRequested=false;}
            if(ImGui::InputText("##Entity name",&m_RenameBuffer,ImGuiInputTextFlags_EnterReturnsTrue)&&RenameEntity(app,e,m_RenameBuffer))m_RenameTarget={};
            if((ImGui::IsItemActive()||ImGui::IsItemDeactivated())&&ImGui::IsKeyPressed(ImGuiKey_Escape,false)){m_RenameTarget={};m_EscapeConsumed=true;}
        }
        if(row.depth)ImGui::Unindent(row.depth*ImGui::GetStyle().IndentSpacing);ImGui::PopID();ImGui::PopID();
    }
    ImGui::Selectable("Scene Root",false);if(ImGui::IsItemHovered())ImGui::SetTooltip("Drop an Entity here to remove its Parent (Keep World by default).");acceptParent({});
    if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());
    if(!m_SelectionLocked&&ImGui::IsWindowHovered()&&ImGui::IsMouseClicked(0)&&!ImGui::IsAnyItemHovered()&&!m_LightPointerCaptured&&!ImGuizmo::IsUsing()&&!app.IsCameraControlActive()&&!ImGui::IsMouseDown(1)){FinishTransaction(app);ClearSelection();ImGui::ClearActiveID();}
    ImGui::TextDisabled("%zu entities",world.GetAliveCount());ImGui::End();m_Statistics.hierarchyMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
void EditorLayer::DrawRuntimeErrors(Application& app){
 auto& log=Logger::Get();auto session=log.RuntimeSessionId();
 if(log.RuntimeSessionActive()||!session){m_RuntimeErrorSession=0;m_ShowRuntimeErrors=false;m_LastRuntimeErrors={};return;}
 if(session!=m_RuntimeErrorSession)m_LastRuntimeErrors=log.RuntimeErrors();
 const auto& summary=m_LastRuntimeErrors;
 // Observe completion once. Closing this non-modal window does not reopen it on every frame.
 if(summary.active||!summary.session){m_RuntimeErrorSession=0;m_ShowRuntimeErrors=false;}
 else if(summary.session!=m_RuntimeErrorSession){m_RuntimeErrorSession=summary.session;m_ShowRuntimeErrors=summary.total>0;}
 if(!m_ShowRuntimeErrors)return;
 ImGui::SetNextWindowSize({640,380},ImGuiCond_FirstUseEver);
 if(editor::BeginEditorPanel("Runtime Errors",&m_ShowRuntimeErrors)){
  ImGui::Text("Session %llu: %llu occurrences / %zu issues",(unsigned long long)summary.session,(unsigned long long)summary.total,summary.issues.size());
  if(summary.dropped||summary.truncated)ImGui::TextWrapped("Bounded summary: %llu dropped messages, %llu truncated text fields",(unsigned long long)summary.dropped,(unsigned long long)summary.truncated);
  if(ImGui::Button("Copy All"))ImGui::SetClipboardText(summary.Copy().c_str());
  ImGui::TextWrapped("Original entries remain in Console / Diagnostics until its bounded history is cleared or overwritten.");
  ImGui::BeginChild("Runtime issues");
  if(m_ConsoleRevision!=log.Revision()){m_ConsoleMessages=log.Messages();m_ConsoleRevision=log.Revision();}
  const auto& messages=m_ConsoleMessages;
  for(std::size_t i=0;i<summary.issues.size();++i){auto& e=summary.issues[i];ImGui::PushID(int(i));
   auto title="["+e.level+"] "+e.source+(e.line?":"+std::to_string(e.line):"")+(e.callback.empty()?"":" / "+e.callback);
   ImGui::SeparatorText(title.c_str());ImGui::TextWrapped("%s",e.message.c_str());
   ImGui::Text("Occurrences: %llu | First / last Unix time: %.3f / %.3f",(unsigned long long)e.count,e.firstTime,e.lastTime);
   bool available=std::any_of(messages.begin(),messages.end(),[&](auto& row){return row.sequence==e.firstSequence;});
   ImGui::BeginDisabled(!available);if(ImGui::SmallButton("Show in Console")){m_WorkspaceFocus=false;m_ShowConsole=true;m_ConsoleInfo=m_ConsoleWarning=m_ConsoleError=true;m_ConsoleFilter.clear();m_ConsoleTargetSequence=e.firstSequence;ImGui::SetWindowFocus("Console / Diagnostics");}ImGui::EndDisabled();
   if(!available&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Original entry was cleared or overwritten in Console history.");ImGui::SameLine();
   if(ImGui::SmallButton("Copy Error"))ImGui::SetClipboardText(summary.Copy(i).c_str());
   std::string sourceError;bool scriptAvailable=false;try{(void)ResolveEditableLuaScript(e.source);scriptAvailable=true;}catch(const std::exception& failure){sourceError=failure.what();}
   ImGui::SameLine();ImGui::BeginDisabled(!scriptAvailable);if(ImGui::SmallButton("Open Script at Line"))OpenScriptAtLine(app,e.source,e.line);ImGui::EndDisabled();if(!scriptAvailable&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",sourceError.c_str());
   if((!e.firstTrace.empty()||!e.lastTrace.empty())&&ImGui::TreeNode("Stack traceback")){ImGui::TextWrapped("%s",e.firstTrace.c_str());if(e.lastTrace!=e.firstTrace){ImGui::SeparatorText("Last occurrence");ImGui::TextWrapped("%s",e.lastTrace.c_str());}ImGui::TreePop();}
   ImGui::PopID();
  }ImGui::EndChild();
 }ImGui::End();
}

void EditorLayer::DrawConsole(Application& app,IRenderAdapter* renderer){
    ImGui::SetNextWindowSizeConstraints(ImVec2(240,130),ImVec2(FLT_MAX,FLT_MAX));
    if(!editor::BeginEditorPanel("Console / Diagnostics",&m_ShowConsole)){ImGui::End();return;}
    if(ImGui::BeginTabBar("ConsoleTabs")){
        if(ImGui::BeginTabItem("Console",nullptr,m_ConsoleTargetSequence?ImGuiTabItemFlags_SetSelected:ImGuiTabItemFlags_None)){
            auto& log=Logger::Get();if(m_ConsoleRevision!=log.Revision()){m_ConsoleMessages=log.Messages();m_ConsoleRevision=log.Revision();}
            if(ImGui::Button("Clear")){log.ClearMessages();m_ConsoleMessages.clear();m_ConsoleRevision=log.Revision();}ImGui::SameLine();ImGui::Checkbox("INFO",&m_ConsoleInfo);ImGui::SameLine();ImGui::Checkbox("WARN",&m_ConsoleWarning);ImGui::SameLine();ImGui::Checkbox("ERROR",&m_ConsoleError);ImGui::SameLine();ImGui::InputTextWithHint("##search","Search logs",&m_ConsoleFilter);
            ImGui::BeginChild("Messages");ImGui::PushFont(MonoFont(),0);for(const auto& entry:m_ConsoleMessages){if((entry.level=="INFO"&&!m_ConsoleInfo)||(entry.level=="WARN"&&!m_ConsoleWarning)||(entry.level=="ERROR"&&!m_ConsoleError)||!Matches(entry.message,m_ConsoleFilter))continue;ImGui::TextWrapped("[%s] %s",entry.level.c_str(),entry.message.c_str());if(m_ConsoleTargetSequence==entry.sequence){ImGui::SetScrollHereY(.5f);m_ConsoleTargetSequence=0;}}ImGui::PopFont();ImGui::EndChild();ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Diagnostics")){
            auto lights=app.GetLightDiagnostics();ImGui::Text("Editor UI %.3f / hierarchy %.3f / Inspector %.3f ms",m_Statistics.uiMs,m_Statistics.hierarchyMs,m_Statistics.inspectorMs);
            ImGui::Text("World clone %.3f ms / lights %u / cull GPU %.3f",app.GetLastWorldCloneMs(),lights.total,lights.cullingGpuMs);
            if(renderer){auto t=renderer->GetGpuTimings();const auto& s=app.GetRenderStatistics();ImGui::Text("CPU frame %.3f / GPU frame %.3f / main GPU %.3f ms",app.GetFrameDeltaTime()*1000,t.frameMs,t.mainMs);ImGui::Text("Scene draws %llu / instanced %llu / gather %.3f ms",static_cast<unsigned long long>(s.submitted.drawCalls),static_cast<unsigned long long>(s.submitted.instancedDrawCalls),s.gatherMs);}
            if(renderer){auto p=renderer->GetPipelineCacheDiagnostics();ImGui::Text("Mesh PSO cache: hits %llu / misses %llu / creations %llu / cached %llu",static_cast<unsigned long long>(p.hits),static_cast<unsigned long long>(p.misses),static_cast<unsigned long long>(p.creations),static_cast<unsigned long long>(p.cachedCount));}
            if(auto* r=app.GetResourceManager()){auto s=r->GetStats();ImGui::Text("Meshes %zu / textures %zu / materials %zu / loading %zu / failed %zu",s.meshCount,s.textureCount,s.materialCount,s.loadingCount,s.failedCount);}
            ImGui::TextUnformatted("Editor overlays are separate from scene draw statistics.");ImGui::EndTabItem();
        }ImGui::EndTabBar();
    }ImGui::End();
}
}
