#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "scene/SceneSerializer.h"
#include "resources/ResourceManager.h"
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#include <fstream>
#include <chrono>
#include <iostream>
#include <Windows.h>
namespace fs=std::filesystem;
static unsigned checks=0;
static void Check(bool value,const std::string& label){if(!value)throw std::runtime_error(label);++checks;}
static std::string Bytes(const fs::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
static nlohmann::json Json(const fs::path& path){return nlohmann::json::parse(Bytes(path));}
static void Text(const fs::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;Check(bool(out),"write owned fixture");}
struct Fixture {
 fs::path root=fs::current_path()/("project-settings-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 Fixture(){Check(fs::create_directory(root),"owned fixture directory");}
 ~Fixture(){ProjectManager::Activate({});std::error_code ec;fs::remove_all(root,ec);}
};
int main(){ScopedEditorPreferenceRestore preferences;Fixture fixture;try{
 auto project=ProjectManager::Create(fixture.root,"OriginalFolder");const auto descriptor=project->descriptorPath,root=project->root,assets=project->AssetRoot();
 auto disk=project->descriptor;disk["custom"]={{"owner","preserved"},{"array",{1,2,3}}};disk["directories"]["futureDirectory"]="Settings";disk["rendering"]={{"futureOption",42}};Text(descriptor,disk.dump(2));
 ecs::World secondWorld;auto second=secondWorld.CreateEntity();secondWorld.AddComponent<ecs::TransformComponent>(second);secondWorld.AddComponent<ecs::TagComponent>(second).name="Second Startup";
 SceneSettings secondSettings;secondSettings.antiAliasing=AntiAliasingMode::FxaaHigh;secondSettings.lighting.ambientIntensity=.37f;std::string error;
 Check(SceneSerializer::SaveWorld(project->ResolveScene("Scenes/Second.json"),secondWorld,&error,&secondSettings),error);
 Text(project->ResolveAsset("models/portable.obj"),"v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");Text(project->ResolveScene("Scenes/Corrupt.json"),"{broken");
 {
  Application app;Check(app.Initialize(Application::StartupMode::ProjectBrowser),"initialize project browser");auto* native=dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle();glfwHideWindow(native);SyntheticEditorInput input;
  auto& ui=app.GetEditorLayer();Check(app.OpenProject(descriptor,Application::ProjectDecision::Discard,&error),error);Check(app.IsGpuInstancingEnabled(),"legacy descriptor defaults to enabled instancing");
  const auto originalScene=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());const auto sceneKey=app.GetSceneKey();const auto history=ui.UndoCount();
  app.SetFrameLimit(1);app.Run();app.Run();
  // Open through the actual File menu, then exercise the separate panel.
  auto click=[&](float x,float y){SyntheticEditorInput::Position(x,y);app.Run();SyntheticEditorInput::Button(0,true);app.Run();SyntheticEditorInput::Button(0,false);app.Run();};
  click(25,12);auto* popup=ImGui::GetCurrentContext()->OpenPopupStack.empty()?nullptr:ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
  Check(popup&&popup->Active,"File menu opens");ImGui::ActivateItemByID(popup->GetID("Project Settings..."));app.Run();app.Run();
  auto* window=ImGui::FindWindowByName("Project Settings");Check(window&&window->Active,"File menu opens distinct Project Settings window");
  auto* browser=ImGui::FindWindowByName("Project Browser");Check(!browser||!browser->Active,"Project Settings does not open Project Browser");
  auto& draft=ui.ProjectSettingsDraft();draft.name="Display Name / no folder rename";draft.startupScene="Scenes/Second.json";
  auto category=[&](const char* label){auto* nav=window->DC.ChildWindows.front();ImGui::ActivateItemByID(nav->GetID(label));app.Run();app.Run();};
  category("Rendering");auto* fields=window->DC.ChildWindows.back();ImGui::ActivateItemByID(fields->GetID("GPU Instancing"));app.Run();app.Run();
  Check(!draft.gpuInstancing&&ui.ProjectSettingsDirty()&&ui.HasProjectEdits(),"UI rendering toggle changes project draft and dirty guard");
  Check(app.IsGpuInstancingEnabled()&&Bytes(descriptor)==disk.dump(2),"draft is not applied or saved early");
  ImGui::ActivateItemByID(window->GetID("Save & Apply"));app.Run();app.Run();
  Check(!ui.ProjectSettingsDirty()&&!app.IsGpuInstancingEnabled(),"Save & Apply persists and applies GPU setting");
  Check(app.GetProject()->name=="Display Name / no folder rename"&&app.GetProject()->startupScene=="Scenes/Second.json","name and startup published");
  Check(app.GetProject()->root==root&&app.GetProject()->descriptorPath==descriptor&&app.GetProject()->AssetRoot()==assets,"display rename preserves physical roots and descriptor filename");
  Check(app.GetSceneKey()==sceneKey&&ui.UndoCount()==history&&!ui.IsSceneDirty()&&SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==originalScene,"saving project does not replace or dirty current scene");
  auto saved=Json(descriptor);Check(saved["custom"]==disk["custom"]&&saved["directories"]==disk["directories"]&&saved["rendering"]["futureOption"]==42,"unknown root and nested fields preserved");
  Check(saved["startupScene"]=="Scenes/Second.json"&&saved["directories"]["assets"]=="Assets","portable relative paths retained");
  for(const char* key:{"surfaceSnap","snapping","grid","visuals","cameraSpeed","environment","lighting","fog","antiAliasing"})Check(!saved.contains(key),"editor/scene values not added to project descriptor");
  Check(ui.RecentProjects().front().name==app.GetProject()->name,"recent project label updates after display rename");
  category("General");std::filesystem::create_directories("diagnostics/project-settings");Check(app.GetRenderAdapter()->SaveFrameImage("diagnostics/project-settings/general.ppm"),"General screenshot");
  for(const char* name:{"Physics","Startup / Runtime","Rendering"})category(name);
  Check(app.GetRenderAdapter()->SaveFrameImage("diagnostics/project-settings/rendering.ppm"),"Rendering screenshot");
  const auto savedBytes=Bytes(descriptor);auto active=app.GetProject();
  for(const auto& invalid:{"../outside.json","C:/outside.json","Assets/Bad.json","Scenes/Missing.json","Scenes/Corrupt.json"}){
   auto values=active->Settings();values.startupScene=invalid;values.gpuInstancing=true;
   Check(!app.SaveProjectSettings(values,&error)&&!error.empty(),"invalid Startup Scene diagnosed");Check(Bytes(descriptor)==savedBytes&&app.GetProject()==active&&!app.IsGpuInstancingEnabled(),"failed validation preserves descriptor, active context and renderer");
  }
  auto values=active->Settings();values.name=" \t";Check(!app.SaveProjectSettings(values,&error)&&Bytes(descriptor)==savedBytes,"blank/control-character name rejected");
  // Force atomic publish failure while allowing descriptor reads.
  struct Lock{HANDLE handle;~Lock(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}} lock{CreateFileW(descriptor.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
  Check(lock.handle!=INVALID_HANDLE_VALUE,"descriptor locked against replacement");values=active->Settings();values.name="Must not be saved";values.gpuInstancing=true;
  Check(!app.SaveProjectSettings(values,&error)&&Bytes(descriptor)==savedBytes&&app.GetProject()==active&&!app.IsGpuInstancingEnabled(),"failed atomic replace preserves saved bytes and live settings");
  CloseHandle(lock.handle);lock.handle=INVALID_HANDLE_VALUE;
  for(auto& entry:fs::directory_iterator(root))Check(entry.path().filename().string().find(".tmp-")==std::string::npos,"failed-save temporary files cleaned");
  ui.ProjectSettingsDraft().name="Unsaved draft";Check(ui.HasProjectEdits()&&ui.ProjectSettingsDirty(),"project-only edit participates in transition guard");ui.RevertProjectSettings(app);Check(!ui.ProjectSettingsDirty()&&ui.ProjectSettingsDraft().name==active->name,"Revert restores saved values");
  ui.ProjectSettingsDraft().name="Save when switching";Check(app.CloseProject(Application::ProjectDecision::Save,&error),error);Check(Json(descriptor)["name"]=="Save when switching","project-only draft saved through existing transition");
  Check(app.OpenProject(descriptor,Application::ProjectDecision::Discard,&error),error);Check(!app.IsGpuInstancingEnabled()&&app.GetSceneKey()=="Scenes/Second.json"&&app.GetWorld().GetAliveCount()==1,"project reopen restores startup and renderer setting");
  Check(app.GetSceneSettings().antiAliasing==AntiAliasingMode::FxaaHigh&&app.GetSceneSettings().lighting.ambientIntensity==.37f,"startup scene retains its own graphics/art settings");
  Check(app.GetResourceManager()->Load<MeshResource>("models/portable.obj")->IsLoaded()&&AssetPaths::ResolveAssetPath("models/portable.obj")==assets/"models/portable.obj","project asset loading and root remain valid");
  active=app.GetProject();auto external=Json(descriptor);external["externalChange"]=true;Text(descriptor,external.dump(2));values=active->Settings();values.name="Conflict";Check(!app.SaveProjectSettings(values,&error)&&Json(descriptor)==external&&app.GetProject()==active,"external descriptor edits are not overwritten");
  Check(app.OpenProject(descriptor,Application::ProjectDecision::Discard,&error),error);app.SetEditorPlayMode(true);auto beforePlaySave=Bytes(descriptor);values=app.GetProject()->Settings();values.name="During Play";Check(!app.SaveProjectSettings(values,&error)&&Bytes(descriptor)==beforePlaySave,"Play blocks project editing without changing disk");app.SetEditorPlayMode(false);
  Check(dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter())->GetValidationErrorCount()==0,"DX12 validation zero");Check(app.CloseProject(Application::ProjectDecision::Discard,&error),error);app.Shutdown();
 }
 // A fresh Application, not just a reread of the saved JSON, must apply the settings.
 {
  Application restarted;Check(restarted.Initialize(Application::StartupMode::ProjectBrowser),"fresh engine initialize");glfwHideWindow(dynamic_cast<GlfwWindow*>(restarted.GetWindow())->GetGlfwHandle());
  Check(restarted.OpenProject(descriptor,Application::ProjectDecision::Discard,&error),error);Check(restarted.GetProject()->name=="Save when switching"&&!restarted.IsGpuInstancingEnabled()&&restarted.GetSceneKey()=="Scenes/Second.json","fresh engine restores and applies saved project settings");
  Check(restarted.GetWorld().GetAliveCount()==1&&restarted.GetProject()->AssetRoot()==assets,"restart keeps scene and asset paths");Check(restarted.CloseProject(Application::ProjectDecision::Discard,&error),error);restarted.Shutdown();
 }
 std::cout<<"Project Settings: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"PROJECT SETTINGS FAILED: "<<e.what()<<'\n';return 1;}}
