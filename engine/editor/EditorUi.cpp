#include "EditorLayer.h"
#include "EditorWidgets.h"
#include "../core/Application.h"
#include "../core/AssetPaths.h"
#include "../resources/ResourceManager.h"
#include <imgui.h>
#include <imgui_internal.h>
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
    if(w.HasComponent<ecs::DirectionalLightComponent>(e))return "[D]";if(w.HasComponent<ecs::PointLightComponent>(e))return "[P]";if(w.HasComponent<ecs::SpotLightComponent>(e))return "[S]";if(w.HasComponent<ecs::MeshRendererComponent>(e))return "[Mesh]";return "[Entity]";
}
}
void EditorLayer::InitializePreferences(Application& app){
    EditorPreferences p;std::string error;if(LoadPreferences("editor-preferences.json",p,&error))m_ResetLayout=false;else if(!error.empty())Logger::Get().Warn(error);
    m_ShowHierarchy=p.hierarchy;m_ShowInspector=p.inspector;m_ShowViewport=p.viewport;m_ShowAssetBrowser=p.assets;m_ShowMaterialEditor=p.material;m_ShowSceneSettings=p.settings;m_ShowConsole=p.console;m_ShowStatistics=p.diagnostics;
    m_AssetGrid=p.assetGrid;m_BackgroundOverride=p.backgroundOverride;m_ProjectileTestInput=p.projectileTestInput;
    m_ShowVisualSettings=p.visualPanel;m_VisualSettings=p.visuals;m_ShowGrid=p.grid;m_ShowLightGizmos=p.lightGizmos;m_LightXray=p.xray;m_Snapping=p.snapping;m_GizmoOperation=p.operation;m_GizmoMode=p.space;
    m_SnapTranslation=p.translationSnap;m_SnapRotation=p.rotationSnap;m_SnapScale=p.scaleSnap;app.SetCameraNavigation(p.cameraSpeed,p.cameraSensitivity);
}
void EditorLayer::PersistPreferences(Application& app){
    EditorPreferences p;p.hierarchy=m_ShowHierarchy;p.inspector=m_ShowInspector;p.viewport=m_ShowViewport;p.assets=m_ShowAssetBrowser;p.material=m_ShowMaterialEditor;p.settings=m_ShowSceneSettings;p.console=m_ShowConsole;p.diagnostics=m_ShowStatistics;
    p.assetGrid=m_AssetGrid;p.backgroundOverride=m_BackgroundOverride;p.projectileTestInput=m_ProjectileTestInput;
    p.visualPanel=m_ShowVisualSettings;p.visuals=m_VisualSettings;p.grid=m_ShowGrid;p.lightGizmos=m_ShowLightGizmos;p.xray=m_LightXray;p.snapping=m_Snapping;p.operation=m_GizmoOperation;p.space=m_GizmoMode;
    p.translationSnap=m_SnapTranslation;p.rotationSnap=m_SnapRotation;p.scaleSnap=m_SnapScale;p.cameraSpeed=app.GetCameraMoveSpeed();p.cameraSensitivity=app.GetCameraSensitivity();std::string error;if(!SavePreferences("editor-preferences.json",p,&error))Logger::Get().Warn(error);
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
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,.25f,&bottom,&center);
        ImGui::DockBuilderSplitNode(right,ImGuiDir_Down,.42f,&settings,&right);
        ImGui::DockBuilderDockWindow("Scene Hierarchy",left);ImGui::DockBuilderDockWindow("Inspector",right);ImGui::DockBuilderDockWindow("Scene Settings",settings);ImGui::DockBuilderDockWindow("Editor Visuals",settings);
        ImGui::DockBuilderDockWindow("Viewport",center);ImGui::DockBuilderDockWindow("Asset Browser",bottom);ImGui::DockBuilderDockWindow("Console / Diagnostics",bottom);ImGui::DockBuilderDockWindow("Material Editor",bottom);ImGui::DockBuilderDockWindow("Statistics",bottom);ImGui::DockBuilderDockWindow("Physics Stress Test",bottom);
        ImGui::DockBuilderFinish(root);m_ResetLayout=false;
    }
    ImGui::DockSpaceOverViewport(root,&dock,ImGuiDockNodeFlags_PassthruCentralNode);
}
void EditorLayer::DrawToolbar(Application& app){
    auto* viewport=ImGui::GetMainViewport();ImGui::SetNextWindowPos(viewport->WorkPos);ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x,ImGui::GetFrameHeightWithSpacing()+8));
    ImGui::Begin("Main Toolbar",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);
    if(ImGui::Button("Save")){std::string error;if(!app.SaveCurrentScene(&error)){m_EditorError=error;Logger::Get().Error(error);}}ImGui::SameLine();
    if(!app.IsEditorPlayMode()){if(ImGui::Button("Play"))app.SetEditorPlayMode(true);}else{
        if(ImGui::Button(app.IsEditorPaused()?"Resume":"Pause"))app.SetEditorPaused(!app.IsEditorPaused());ImGui::SameLine();if(ImGui::Button("Stop"))app.SetEditorPlayMode(false);
    }
    ImGui::SameLine();ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);ImGui::SameLine();
    ImGui::RadioButton("Move",&m_GizmoOperation,0);ImGui::SameLine();ImGui::RadioButton("Rotate",&m_GizmoOperation,1);ImGui::SameLine();ImGui::RadioButton("Scale",&m_GizmoOperation,2);ImGui::SameLine();
    ImGui::RadioButton("Local",&m_GizmoMode,0);ImGui::SameLine();ImGui::RadioButton("World",&m_GizmoMode,1);ImGui::SameLine();ImGui::Checkbox("Snap",&m_Snapping);ImGui::SameLine();ImGui::Checkbox("Grid",&m_ShowGrid);ImGui::SameLine();
    if(ImGui::Button("Focus"))app.FocusEntity(m_SelectedEntity);ImGui::SameLine();
    ImGui::Text("%s%s",app.GetSceneKey().empty()?"Untitled":app.GetSceneKey().c_str(),IsSceneDirty()?" *":"");ImGui::End();
}
void EditorLayer::DrawMainMenu(Application& app){
    if(!ImGui::BeginMainMenuBar())return;
    bool preferencesRequested=false;
    auto action=[&](int kind){m_SceneDialogAction=kind;m_SceneDialogRequested=true;};
    if(ImGui::BeginMenu("File")){
        if(ImGui::MenuItem("New Scene"))action(1);
        if(ImGui::MenuItem("Open Scene"))action(2);
        if(ImGui::MenuItem("Save","Ctrl+S",false,!app.IsEditorPlayMode())){std::string error;if(!app.SaveCurrentScene(&error)){m_EditorError=error;Logger::Get().Error(error);}}
        if(ImGui::MenuItem("Save As","Ctrl+Shift+S",false,!app.IsEditorPlayMode()))action(3);
        if(ImGui::MenuItem("Reopen Scene",nullptr,false,!app.GetSceneKey().empty())){m_SceneDialogKey=app.GetSceneKey();action(2);}
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Edit")){
        if(ImGui::MenuItem("Undo","Ctrl+Z",false,CanUndo()||HasPendingTransaction()))Undo(app);
        if(ImGui::MenuItem("Redo","Ctrl+Y",false,CanRedo()))Redo(app);
        if(ImGui::MenuItem("Duplicate","Ctrl+D",false,app.GetWorld().IsAlive(m_SelectedEntity)))DuplicateEntity(app,m_SelectedEntity);
        if(ImGui::MenuItem("Delete","Delete",false,app.GetWorld().IsAlive(m_SelectedEntity)))DeleteEntity(app,m_SelectedEntity);ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("View")){
        ImGui::MenuItem("Editor Visuals",nullptr,&m_ShowVisualSettings);
        ImGui::MenuItem("Scene Hierarchy",nullptr,&m_ShowHierarchy);ImGui::MenuItem("Inspector",nullptr,&m_ShowInspector);ImGui::MenuItem("Viewport",nullptr,&m_ShowViewport);ImGui::MenuItem("Asset Browser",nullptr,&m_ShowAssetBrowser);ImGui::MenuItem("Material Editor",nullptr,&m_ShowMaterialEditor);ImGui::MenuItem("Scene Settings / Lighting",nullptr,&m_ShowSceneSettings);ImGui::MenuItem("Console / Diagnostics",nullptr,&m_ShowConsole);ImGui::MenuItem("Statistics",nullptr,&m_ShowStatistics);
        ImGui::Separator();if(ImGui::MenuItem("Reset Layout")){m_ResetLayout=true;m_ShowHierarchy=m_ShowInspector=m_ShowViewport=m_ShowAssetBrowser=m_ShowConsole=true;}
        if(ImGui::MenuItem("Navigation and snapping"))preferencesRequested=true;ImGui::MenuItem("ImGui Demo",nullptr,&m_ShowDemoWindow);ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Scene")){
        if(ImGui::MenuItem("Create Entity"))CreateEntity(app);
        if(ImGui::BeginMenu("Create Light")){if(ImGui::MenuItem("Directional"))CreateLight(app,LightType::Directional);if(ImGui::MenuItem("Point"))CreateLight(app,LightType::Point);if(ImGui::MenuItem("Spot"))CreateLight(app,LightType::Spot);ImGui::EndMenu();}
        if(ImGui::MenuItem("Focus Selected","F"))app.FocusEntity(m_SelectedEntity);
        ImGui::Separator();if(ImGui::MenuItem("Play",nullptr,false,!app.IsEditorPlayMode()))app.SetEditorPlayMode(true);if(ImGui::MenuItem("Pause / Resume",nullptr,false,app.IsEditorPlayMode()))app.SetEditorPaused(!app.IsEditorPaused());if(ImGui::MenuItem("Stop",nullptr,false,app.IsEditorPlayMode()))app.SetEditorPlayMode(false);ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Help")){ImGui::TextUnformatted("WhispEngine Editor 2.0");ImGui::TextUnformatted("RMB + WASD: camera, F: focus, W/E/R: gizmo");ImGui::TextUnformatted("Play clones scene data; Stop discards Runtime edits.");ImGui::TextUnformatted("Hierarchy is flat; parent transforms are not implemented.");ImGui::EndMenu();}
    if(preferencesRequested)ImGui::OpenPopup("EditorPreferences");
    if(ImGui::BeginPopup("EditorPreferences")){
        ImGui::Checkbox("Enable Projectile Test Input (Play / R)",&m_ProjectileTestInput);
        float speed=app.GetCameraMoveSpeed(),sensitivity=app.GetCameraSensitivity();if(ImGui::DragFloat("Camera speed",&speed,.1f,.2f,25))app.SetCameraNavigation(speed,sensitivity);if(ImGui::DragFloat("Sensitivity",&sensitivity,.0001f,.0001f,.02f))app.SetCameraNavigation(speed,sensitivity);
        ImGui::DragFloat("Move snap",&m_SnapTranslation,.05f,.001f,1000);ImGui::DragFloat("Rotate snap (deg)",&m_SnapRotation,.5f,.1f,180);ImGui::DragFloat("Scale snap",&m_SnapScale,.01f,.001f,100);if(ImGui::Button("Save preferences"))PersistPreferences(app);ImGui::EndPopup();
    }
    ImGui::EndMainMenuBar();
}
void EditorLayer::DrawSceneDialogs(Application& app){
    if(m_SceneDialogRequested){ImGui::OpenPopup("Scene action");m_SceneDialogRequested=false;}
    if(ImGui::BeginPopupModal("Scene action",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        if(IsSceneDirty()&&m_SceneDialogAction!=3)ImGui::TextWrapped("Unsaved scene edits will be discarded. Save first, or explicitly discard.");
        if(m_SceneDialogAction!=1)ImGui::InputText("Scene key (scenes/*.json)",&m_SceneDialogKey);
        const auto path=AssetPaths::ResolveAssetOutputPath(m_SceneDialogKey);
        const bool exists=m_SceneDialogAction==3&&!path.empty()&&std::filesystem::exists(path);
        static bool confirmed=false;
        if(exists)ImGui::Checkbox("Confirm replacement of existing scene",&confirmed);
        if(ImGui::Button(m_SceneDialogAction==1?"Discard and New":m_SceneDialogAction==2?"Discard and Open":"Save As")){
            bool ok=true;std::string error;if(m_SceneDialogAction==1)app.NewScene();else if(m_SceneDialogAction==2)ok=app.OpenScene(m_SceneDialogKey,&error);else if(exists&&!confirmed){ok=false;error="Confirm destination replacement or choose another key";}else ok=app.SaveSceneAs(m_SceneDialogKey,exists&&confirmed,&error);
            if(ok){confirmed=false;ImGui::CloseCurrentPopup();}else {m_EditorError=error;Logger::Get().Error(error);}
        }
        ImGui::SameLine();if(ImGui::Button("Save current")){std::string error;if(!app.SaveCurrentScene(&error)){m_EditorError=error;Logger::Get().Error(error);}}
        ImGui::SameLine();if(ImGui::Button("Cancel")){confirmed=false;ImGui::CloseCurrentPopup();}if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::EndPopup();
    }
}
void EditorLayer::DrawSceneHierarchy(Application& app){
    const auto start=std::chrono::steady_clock::now();ImGui::SetNextWindowSizeConstraints(ImVec2(180,150),ImVec2(FLT_MAX,FLT_MAX));
    if(!ImGui::Begin("Scene Hierarchy",&m_ShowHierarchy)){ImGui::End();return;}
    auto& world=app.GetWorld();if(ImGui::Button("Create"))ImGui::OpenPopup("CreateEntity");
    if(ImGui::BeginPopup("CreateEntity")){if(ImGui::MenuItem("Empty Entity"))CreateEntity(app);if(ImGui::BeginMenu("Light")){if(ImGui::MenuItem("Directional"))CreateLight(app,LightType::Directional);if(ImGui::MenuItem("Point"))CreateLight(app,LightType::Point);if(ImGui::MenuItem("Spot"))CreateLight(app,LightType::Spot);ImGui::EndMenu();}ImGui::EndPopup();}
    ImGui::InputTextWithHint("##filter","Search entities",&m_HierarchyFilter);
    auto entities=world.Entities();std::erase_if(entities,[&](auto e){auto* tag=world.GetComponent<ecs::TagComponent>(e);return !Matches(tag?tag->name:std::string(),m_HierarchyFilter);});
    if(!world.IsAlive(m_SelectedEntity)&&m_SelectedEntity.IsValid())m_SelectedEntity={};
    ImGuiListClipper clipper;clipper.Begin(int(entities.size()));while(clipper.Step())for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row){
        const auto e=entities[row];const auto* tag=world.GetComponent<ecs::TagComponent>(e);ImGui::PushID(int(e.index));
        const std::string label=std::string(EntityKind(world,e))+" "+(tag&&!tag->name.empty()?tag->name:"Entity "+std::to_string(e.index));
        if(ImGui::Selectable(label.c_str(),e==m_SelectedEntity)){FinishTransaction(app);m_PropertyDrafts.clear();m_SelectedEntity=e;}
        if(ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0))app.FocusEntity(e);
        if(ImGui::BeginDragDropSource()){ImGui::SetDragDropPayload("WHISP_ENTITY",&e,sizeof(e));ImGui::TextUnformatted(label.c_str());ImGui::EndDragDropSource();}
        if(ImGui::BeginPopupContextItem("EntityActions")){
            if(ImGui::MenuItem("Focus"))app.FocusEntity(e);if(ImGui::MenuItem("Rename")){m_RenameTarget=e;m_RenameBuffer=tag?tag->name:"Entity";m_RenameRequested=true;}
            if(ImGui::MenuItem("Duplicate"))DuplicateEntity(app,e);if(ImGui::MenuItem("Delete"))DeleteEntity(app,e);ImGui::EndPopup();
        }ImGui::PopID();
    }
    if(m_RenameRequested){ImGui::OpenPopup("Rename entity");m_RenameRequested=false;}
    if(ImGui::BeginPopupModal("Rename entity",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::InputText("Name",&m_RenameBuffer);if(ImGui::Button("Rename")){if(RenameEntity(app,m_RenameTarget,m_RenameBuffer))ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }
    ImGui::TextDisabled("%zu entities (flat hierarchy)",world.GetAliveCount());ImGui::End();m_Statistics.hierarchyMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
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
