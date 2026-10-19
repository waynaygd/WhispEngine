#include "PrecisionInputs.h"
#include "EditorLayer.h"
#include "EditorWidgets.h"
#include "../core/Application.h"
#include "../core/AssetPaths.h"
#include "../resources/ResourceManager.h"
#include "../ecs/components/PlayerStartComponent.h"
#include "../ecs/components/FogComponent.h"
#include "../ecs/components/PrefabInstanceComponent.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <misc/cpp/imgui_stdlib.h>
#include <chrono>
#include <algorithm>
namespace editor {
namespace {
bool Matches(const std::string& text,const std::string& filter){
    if(filter.empty())return true;
    return std::search(text.begin(),text.end(),filter.begin(),filter.end(),[](unsigned char a,unsigned char b){return std::tolower(a)==std::tolower(b);})!=text.end();
}
const char* EntityKind(const ecs::World& w,ecs::Entity e){
    if(w.HasComponent<ecs::PrefabInstanceComponent>(e))return "[PF]";
    if(w.HasComponent<ecs::PlayerStartComponent>(e))return "[PlayerStart]";
    if(w.HasComponent<ecs::DirectionalLightComponent>(e))return "[D]";if(w.HasComponent<ecs::PointLightComponent>(e))return "[P]";if(w.HasComponent<ecs::SpotLightComponent>(e))return "[S]";if(w.HasComponent<ecs::FogComponent>(e))return "[Fog]";if(w.HasComponent<ecs::MeshRendererComponent>(e))return "[Mesh]";return "[Entity]";
}
}
void EditorLayer::InitializePreferences(Application& app){
    EditorPreferences p;std::string error;if(LoadPreferences(PreferencesPath(),p,&error))m_ResetLayout=false;else if(!error.empty())Logger::Get().Warn(error);
    if(ImGui::GetCurrentContext()){static std::string layout=AssetPaths::ToUtf8String(AssetPaths::GetExecutableDirectory()/"imgui.ini");ImGui::GetIO().IniFilename=layout.c_str();}
    m_RecentScenes=p.recentScenes;m_RecentProjects=p.recentProjects;
    m_Bookmarks=p.bookmarks;m_SelectionFilters=p.selectionFilters;
    m_ShowHierarchy=p.hierarchy;m_ShowInspector=p.inspector;m_ShowViewport=p.viewport;m_ShowAssetBrowser=p.assets;m_ShowMaterialEditor=p.material;m_ShowSceneSettings=p.settings;m_ShowConsole=p.console;m_ShowStatistics=p.diagnostics;
    m_AssetGrid=p.assetGrid;m_ProjectAssets=p.projectAssets;m_AssetCardSize=p.assetCardSize;m_BackgroundOverride=p.backgroundOverride;m_ProjectileTestInput=p.projectileTestInput;
    m_SurfaceSnap=p.surfaceSnap;m_AlignToNormal=p.alignToNormal;
    m_ShowVisualSettings=p.visualPanel;m_VisualSettings=p.visuals;m_ShowGrid=p.grid;m_ShowLightGizmos=p.lightGizmos;m_LightXray=p.xray;m_LightXrayMode=p.lightXrayMode;m_TransformPrecision=p.transformPrecision;m_RotationSnapping=p.rotationSnapping;m_ShowPhysicsStress=p.physicsStress;m_Snapping=p.snapping;m_GizmoOperation=p.operation;m_GizmoMode=p.space;
    m_SnapTranslation=p.translationSnap;m_SnapRotation=p.rotationSnap;m_SnapScale=p.scaleSnap;app.SetCameraNavigation(p.cameraSpeed,p.cameraSensitivity);
}
void EditorLayer::PersistPreferences(Application& app){
    EditorPreferences p;p.recentScenes=app.GetProject()?std::vector<std::string>{}:m_RecentScenes;p.recentProjects=m_RecentProjects;p.hierarchy=m_ShowHierarchy;p.inspector=m_ShowInspector;p.viewport=m_ShowViewport;p.assets=m_ShowAssetBrowser;p.material=m_ShowMaterialEditor;p.settings=m_ShowSceneSettings;p.console=m_ShowConsole;p.diagnostics=m_ShowStatistics;
    p.assetGrid=m_AssetGrid;p.projectAssets=m_ProjectAssets;p.assetCardSize=m_AssetCardSize;p.backgroundOverride=m_BackgroundOverride;p.projectileTestInput=m_ProjectileTestInput;
    p.bookmarks=m_Bookmarks;p.selectionFilters=m_SelectionFilters;
    p.surfaceSnap=m_SurfaceSnap;p.alignToNormal=m_AlignToNormal;
    p.visualPanel=m_ShowVisualSettings;p.visuals=m_VisualSettings;p.grid=m_ShowGrid;p.lightGizmos=m_ShowLightGizmos;p.xray=m_LightXray;p.lightXrayMode=m_LightXrayMode;p.transformPrecision=m_TransformPrecision;p.rotationSnapping=m_RotationSnapping;p.physicsStress=m_ShowPhysicsStress;p.snapping=m_Snapping;p.operation=m_GizmoOperation;p.space=m_GizmoMode;
    p.translationSnap=m_SnapTranslation;p.rotationSnap=m_SnapRotation;p.scaleSnap=m_SnapScale;p.cameraSpeed=app.GetCameraMoveSpeed();p.cameraSensitivity=app.GetCameraSensitivity();std::string error;if(!SavePreferences(PreferencesPath(),p,&error))Logger::Get().Warn(error);
}
void EditorLayer::ConfigureDocking(){
    auto* viewport=ImGui::GetMainViewport();ImGuiViewport dock=*viewport;const float toolbar=ImGui::GetFrameHeightWithSpacing()+8;
    dock.WorkPos.y+=toolbar;dock.WorkSize.y=std::max(1.f,dock.WorkSize.y-toolbar);
    const auto root=ImHashStr("WhispEngine.Editor.Dockspace");
    if(m_FixedDiagnosticLayout){ImGui::DockSpaceOverViewport(root,&dock,ImGuiDockNodeFlags_PassthruCentralNode);return;}
    if(m_ResetLayout||!ImGui::DockBuilderGetNode(root)){
        ImGui::DockBuilderRemoveNode(root);ImGui::DockBuilderAddNode(root,ImGuiDockNodeFlags_DockSpace);ImGui::DockBuilderSetNodeSize(root,dock.WorkSize);
        ImGuiID center=root,left,right,bottom,settings;
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,.19f,&left,&center);
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,.29f,&right,&center);
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,.40f,&bottom,&center);
        ImGui::DockBuilderSplitNode(right,ImGuiDir_Down,.42f,&settings,&right);
        ImGui::DockBuilderDockWindow("Scene Hierarchy",left);ImGui::DockBuilderDockWindow("Inspector",right);ImGui::DockBuilderDockWindow("Scene Settings",settings);ImGui::DockBuilderDockWindow("Editor Visuals",settings);
        ImGui::DockBuilderDockWindow("Viewport",center);ImGui::DockBuilderDockWindow("Asset Browser",bottom);ImGui::DockBuilderDockWindow("Console / Diagnostics",bottom);ImGui::DockBuilderDockWindow("Gameplay Events",bottom);ImGui::DockBuilderDockWindow("Material Editor",bottom);ImGui::DockBuilderDockWindow("Statistics",bottom);ImGui::DockBuilderDockWindow("Physics Stress Test",bottom);
        ImGui::DockBuilderFinish(root);m_ResetLayout=false;
    }
    ImGui::DockSpaceOverViewport(root,&dock,ImGuiDockNodeFlags_PassthruCentralNode);
}
void EditorLayer::DrawToolbar(Application& app){
    auto* viewport=ImGui::GetMainViewport();ImGui::SetNextWindowPos(viewport->WorkPos);ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x,ImGui::GetFrameHeightWithSpacing()+8));
    ImGui::Begin("Main Toolbar",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);
    if(ImGui::Button(app.IsPrefabEditing()?"Save Prefab":"Save")){std::string error;if(!app.SaveCurrentScene(&error)){m_EditorError=error;Logger::Get().Error(error);}}ImGui::SameLine();
    if(app.IsPrefabEditing()){if(ImGui::Button("Return to Scene")){if(!IsSceneDirty())ClosePrefabEditor(app);else {m_PrefabAction=2;m_PrefabConfirm=true;}}ImGui::SameLine();}
    ImGui::BeginDisabled(app.IsPrefabEditing());
    if(!app.IsEditorPlayMode()){if(ImGui::Button("Play"))app.SetEditorPlayMode(true);}else{
        if(ImGui::Button(app.IsEditorPaused()?"Resume":"Pause"))app.SetEditorPaused(!app.IsEditorPaused());ImGui::SameLine();if(ImGui::Button("Stop"))app.SetEditorPlayMode(false);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);ImGui::SameLine();
    ImGui::RadioButton("Move",&m_GizmoOperation,0);ImGui::SameLine();ImGui::RadioButton("Rotate",&m_GizmoOperation,1);ImGui::SameLine();ImGui::RadioButton("Scale",&m_GizmoOperation,2);ImGui::SameLine();
    ImGui::RadioButton("Local",&m_GizmoMode,0);ImGui::SameLine();auto* t=app.GetWorld().GetComponent<ecs::TransformComponent>(m_Selection.active);const bool localScale=m_Selection.items.size()==1&&m_GizmoOperation==2&&t&&(std::abs(t->rotation.x)+std::abs(t->rotation.y)+std::abs(t->rotation.z)>1e-6f);if(localScale)m_GizmoMode=0;ImGui::BeginDisabled(localScale);ImGui::RadioButton("World",&m_GizmoMode,1);ImGui::EndDisabled();if(m_GizmoOperation==2&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Scale axes/planes use local TRS axes. Rotated world non-uniform scale requires shear and is not represented by Transform.");ImGui::SameLine();ImGui::Checkbox(m_GizmoOperation==1?"Rotation Snap":"Snap",m_GizmoOperation==1?&m_RotationSnapping:&m_Snapping);ImGui::SameLine();ImGui::Checkbox("Grid",&m_ShowGrid);ImGui::SameLine();
    if(ImGui::Button("Focus"))FocusSelection(app);ImGui::SameLine();
    ImGui::Text("%s%s",app.IsPrefabEditing()?m_EditingPrefab.c_str():(app.GetSceneKey().empty()?"Untitled":app.GetSceneKey().c_str()),IsSceneDirty()?" *":"");ImGui::End();
}
void EditorLayer::DrawMainMenu(Application& app){
    if(!ImGui::BeginMainMenuBar())return;
    bool preferencesRequested=false;
    auto action=[&](int kind){m_SceneDialogAction=kind;m_SceneDialogRequested=true;};
    if(ImGui::BeginMenu("File")){
        ImGui::BeginDisabled(app.IsPrefabEditing());
        if(ImGui::MenuItem("New Project...")){m_ShowProjectBrowser=true;m_ProjectTab=1;}
        if(ImGui::MenuItem("Open Project...")){m_ShowProjectBrowser=true;m_ProjectTab=2;}
        if(ImGui::BeginMenu("Recent Projects",!m_RecentProjects.empty())){
            const auto recent=m_RecentProjects;for(const auto& entry:recent)if(ImGui::MenuItem(entry.name.c_str()))RequestProjectOpen(app,entry.descriptor);ImGui::EndMenu();
        }
        if(ImGui::MenuItem("Project Settings...",nullptr,false,bool(app.GetProject())))ShowProjectSettings(app);
        if(ImGui::MenuItem("Close Project",nullptr,false,!app.IsProjectBrowserActive()))RequestProjectOpen(app,{},true);
        ImGui::Separator();
        if(ImGui::MenuItem("New Scene"))action(1);
        if(ImGui::MenuItem("Open Scene"))action(2);
        if(ImGui::BeginMenu("Recent Scenes",!m_RecentScenes.empty())){const auto recent=m_RecentScenes;for(const auto& key:recent)if(ImGui::MenuItem(key.c_str())){RequestRecentScene(app,key);}ImGui::EndMenu();}
        ImGui::EndDisabled();
        if(ImGui::MenuItem("Save","Ctrl+S",false,!app.IsEditorPlayMode())){std::string error;if(!app.SaveCurrentScene(&error)){m_EditorError=error;Logger::Get().Error(error);}}
        if(ImGui::MenuItem("Save As","Ctrl+Shift+S",false,!app.IsEditorPlayMode()&&!app.IsPrefabEditing()))action(3);
        if(ImGui::MenuItem("Reopen Scene",nullptr,false,!app.GetSceneKey().empty()&&!app.IsPrefabEditing())){m_SceneDialogKey=app.GetSceneKey();action(2);}
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Edit")){
        if(ImGui::MenuItem("Undo","Ctrl+Z",false,CanUndo()||HasPendingTransaction()))Undo(app);
        if(ImGui::MenuItem("Redo","Ctrl+Y",false,CanRedo()))Redo(app);
        if(ImGui::MenuItem("Copy","Ctrl+C",false,!app.IsEditorPlayMode()))CopyEntity(app,m_Selection.active);
        if(ImGui::MenuItem("Paste","Ctrl+V",false,!app.IsEditorPlayMode()&&m_Clipboard.is_object()))PasteEntity(app);
        if(ImGui::MenuItem("Clear Selection")){FinishTransaction(app);ClearSelection();}
        if(ImGui::MenuItem("Duplicate","Ctrl+D",false,app.GetWorld().IsAlive(m_Selection.active)))DuplicateEntity(app,m_Selection.active);
        if(ImGui::MenuItem("Delete selection","Delete",false,!m_Selection.items.empty()))DeleteSelection(app);ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("View")){
        ImGui::MenuItem("Editor Visuals",nullptr,&m_ShowVisualSettings);
        ImGui::MenuItem("Game View (shared viewport)","G",&m_GameView);
        ImGui::MenuItem("Scene Hierarchy",nullptr,&m_ShowHierarchy);ImGui::MenuItem("Inspector",nullptr,&m_ShowInspector);ImGui::MenuItem("Viewport",nullptr,&m_ShowViewport);ImGui::MenuItem("Asset Browser",nullptr,&m_ShowAssetBrowser);ImGui::MenuItem("Material Editor",nullptr,&m_ShowMaterialEditor);ImGui::MenuItem("Scene Settings / Lighting",nullptr,&m_ShowSceneSettings);ImGui::MenuItem("Console / Diagnostics",nullptr,&m_ShowConsole);ImGui::MenuItem("Gameplay Events",nullptr,&m_ShowGameplayEvents);ImGui::MenuItem("Statistics",nullptr,&m_ShowStatistics);ImGui::MenuItem("Physics Stress Test",nullptr,&m_ShowPhysicsStress);
        ImGui::Separator();if(ImGui::MenuItem("Reset Layout")){m_ResetLayout=true;m_ShowHierarchy=m_ShowInspector=m_ShowViewport=m_ShowAssetBrowser=m_ShowConsole=true;}
        if(ImGui::MenuItem("Navigation and snapping"))preferencesRequested=true;ImGui::MenuItem("ImGui Demo",nullptr,&m_ShowDemoWindow);ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Scene")){
        if(ImGui::MenuItem("Focus Selected","F"))FocusSelection(app);
        if(ImGui::MenuItem("Drop to Floor",nullptr,false,!app.IsEditorPlayMode()&&!m_Selection.items.empty()))DropSelectionToFloor(app);
        ImGui::Separator();if(ImGui::MenuItem("Play",nullptr,false,!app.IsEditorPlayMode()&&!app.IsPrefabEditing()))app.SetEditorPlayMode(true);if(ImGui::MenuItem("Pause / Resume",nullptr,false,app.IsEditorPlayMode()))app.SetEditorPaused(!app.IsEditorPaused());if(ImGui::MenuItem("Stop",nullptr,false,app.IsEditorPlayMode()))app.SetEditorPlayMode(false);ImGui::EndMenu();
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
    if(!ImGui::Begin("Scene Hierarchy",&m_ShowHierarchy)){ImGui::End();return;}
    m_HierarchyFocused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    auto& world=app.GetWorld();
    ImGui::InputTextWithHint("##filter","Search entities",&m_HierarchyFilter);
    const auto entities=world.Entities();PruneSelection(app);
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
        const std::string label=std::string(EntityKind(world,e))+" "+(tag&&!tag->name.empty()?tag->name:"Entity "+std::to_string(e.index));
        const std::string rowLabel=label+(e==m_Selection.active?" [Active]":"")+"###EntityRow";
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
            if(ImGui::MenuItem("Focus")){if(IsSelected(e))FocusSelection(app);else app.FocusEntity(e);}if(ImGui::MenuItem("Rename")){m_RenameTarget=e;m_RenameBuffer=tag?tag->name:"Entity";m_RenameRequested=true;}
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
        if(row.depth)ImGui::Unindent(row.depth*ImGui::GetStyle().IndentSpacing);ImGui::PopID();ImGui::PopID();
    }
    ImGui::Selectable("Scene Root",false);if(ImGui::IsItemHovered())ImGui::SetTooltip("Drop an Entity here to remove its Parent (Keep World by default).");acceptParent({});
    if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());
    if(ImGui::IsWindowHovered()&&ImGui::IsMouseClicked(0)&&!ImGui::IsAnyItemHovered()&&!m_LightPointerCaptured&&!ImGuizmo::IsUsing()&&!app.IsCameraControlActive()&&!ImGui::IsMouseDown(1)){FinishTransaction(app);ClearSelection();ImGui::ClearActiveID();}
    if(m_RenameRequested){ImGui::OpenPopup("Rename entity");m_RenameRequested=false;}
    if(ImGui::BeginPopupModal("Rename entity",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::InputText("Name",&m_RenameBuffer);if(ImGui::Button("Rename")){if(RenameEntity(app,m_RenameTarget,m_RenameBuffer))ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }
    ImGui::TextDisabled("%zu entities",world.GetAliveCount());ImGui::End();m_Statistics.hierarchyMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
void EditorLayer::DrawConsole(Application& app,IRenderAdapter* renderer){
    ImGui::SetNextWindowSizeConstraints(ImVec2(240,130),ImVec2(FLT_MAX,FLT_MAX));
    if(!ImGui::Begin("Console / Diagnostics",&m_ShowConsole)){ImGui::End();return;}
    if(ImGui::BeginTabBar("ConsoleTabs")){
        if(ImGui::BeginTabItem("Console")){
            auto& log=Logger::Get();if(m_ConsoleRevision!=log.Revision()){m_ConsoleMessages=log.Messages();m_ConsoleRevision=log.Revision();}
            if(ImGui::Button("Clear")){log.ClearMessages();m_ConsoleMessages.clear();m_ConsoleRevision=log.Revision();}ImGui::SameLine();ImGui::Checkbox("INFO",&m_ConsoleInfo);ImGui::SameLine();ImGui::Checkbox("WARN",&m_ConsoleWarning);ImGui::SameLine();ImGui::Checkbox("ERROR",&m_ConsoleError);ImGui::SameLine();ImGui::InputTextWithHint("##search","Search logs",&m_ConsoleFilter);
            ImGui::BeginChild("Messages");for(const auto& entry:m_ConsoleMessages){if((entry.level=="INFO"&&!m_ConsoleInfo)||(entry.level=="WARN"&&!m_ConsoleWarning)||(entry.level=="ERROR"&&!m_ConsoleError)||!Matches(entry.message,m_ConsoleFilter))continue;ImGui::TextWrapped("[%s] %s",entry.level.c_str(),entry.message.c_str());}ImGui::EndChild();ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Diagnostics")){
            auto lights=app.GetLightDiagnostics();ImGui::Text("Editor UI %.3f / hierarchy %.3f / Inspector %.3f ms",m_Statistics.uiMs,m_Statistics.hierarchyMs,m_Statistics.inspectorMs);
            ImGui::Text("World clone %.3f ms / lights %u / cull GPU %.3f",app.GetLastWorldCloneMs(),lights.total,lights.cullingGpuMs);
            if(renderer){auto t=renderer->GetGpuTimings();const auto& s=app.GetRenderStatistics();ImGui::Text("CPU frame %.3f / GPU frame %.3f / main GPU %.3f ms",app.GetFrameDeltaTime()*1000,t.frameMs,t.mainMs);ImGui::Text("Scene draws %llu / instanced %llu / gather %.3f ms",static_cast<unsigned long long>(s.submitted.drawCalls),static_cast<unsigned long long>(s.submitted.instancedDrawCalls),s.gatherMs);}
            if(auto* r=app.GetResourceManager()){auto s=r->GetStats();ImGui::Text("Meshes %zu / textures %zu / materials %zu / loading %zu / failed %zu",s.meshCount,s.textureCount,s.materialCount,s.loadingCount,s.failedCount);}
            ImGui::TextUnformatted("Editor overlays are separate from scene draw statistics.");ImGui::EndTabItem();
        }ImGui::EndTabBar();
    }ImGui::End();
}
}
