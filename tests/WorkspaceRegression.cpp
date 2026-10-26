#define NOMINMAX
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "editor/EditorPreferences.h"
#include "scene/SceneSerializer.h"
#include "ecs/components/TagComponent.h"
#include "SyntheticEditorInput.h"
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <imgui_internal.h>
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>
using namespace editor;using namespace ecs;
static unsigned checks=0;
static void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);++checks;}
static ImGuiWindow* Window(const char* name){auto* window=ImGui::FindWindowByName(name);Check(window&&window->Active,std::string("active panel ")+name);return window;}
static Entity Named(World& world,const char* name){for(auto e:world.Entities())if(auto* tag=world.GetComponent<TagComponent>(e);tag&&tag->name==name)return e;throw std::runtime_error(name);}
int main(int argc,char** argv){try{
 const auto directory=AssetPaths::GetExecutableDirectory();Check(directory.filename()=="workspace-regression","isolated executable/settings directory");const std::string mode=argc>1?argv[1]:"";const bool restore=mode=="--restore",corrupt=mode=="--corrupt",legacyMode=mode=="--legacy";
 std::string legacyBytes;
 if(!restore&&!corrupt&&!legacyMode){std::filesystem::remove(PreferencesPath());std::filesystem::remove(directory/"imgui.ini");std::ofstream legacy(directory/"imgui.ini");legacy<<"damaged layout";}
 if(corrupt||legacyMode){EditorPreferences previous;std::string error;Check(LoadPreferences(PreferencesPath(),previous,&error),error);
   if(corrupt){auto data=PreferencesToJson(previous);data["activeWorkspace"]="level";data["workspaces"]["level"]["ini"]="[Window][Viewport]\n[Docking][Data]\nDockSpace ID=0x516E9E8C Parent=0x516E9E8C\n";data["cameraSpeed"]=4.2;std::ofstream file(PreferencesPath());file<<data.dump(2);}
   else {legacyBytes=previous.workspaces.at("level").ini;const auto begin=legacyBytes.find("[Window][Lua Runtime Inspector]");if(begin!=std::string::npos){auto end=legacyBytes.find("\n[",begin+1);legacyBytes.erase(begin,end==std::string::npos?std::string::npos:end-begin+1);}Check(ValidWorkspaceIni(legacyBytes),"valid legacy native layout");std::ofstream file(directory/"imgui.ini",std::ios::binary);file<<legacyBytes;file.close();previous.workspaces.clear();previous.workspacesInitialized=false;previous.activeWorkspace=EditorWorkspace::LevelEditing;Check(SavePreferences(PreferencesPath(),previous,&error),error);}
 }
 Application app;Check(app.Initialize(),"initialize editor");if(legacyMode)app.GetEditorLayer().ShowLuaRuntimeInspector();SyntheticEditorInput input;auto& ui=app.GetEditorLayer();auto frame=[&](unsigned count=3){app.SetFrameLimit(count);Check(app.Run()==0,"native frames");};frame(5);
 Check(ImGui::GetIO().IniFilename==nullptr,"global imgui.ini does not compete with workspace storage");Check(ui.GetWorkspace()==(restore?EditorWorkspace::Scripting:EditorWorkspace::LevelEditing),"default/restored active workspace");
 std::string error;Check(app.OpenScene("scenes/interactive_room.json",&error),error);auto before=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());auto entity=Named(app.GetWorld(),"DecorTable");ui.SelectEntity(entity);const auto undo=ui.UndoCount();const auto identity=app.GetWorld().GetIdentity();
 if(corrupt){Check(Window("Inspector")->DockId!=0,"corrupt active layout falls back to preset at startup");Check(app.GetCameraMoveSpeed()==4.2f,"bad layout does not discard unrelated preferences");Check(ui.SaveCurrentWorkspace(app),ui.GetError());app.Shutdown();std::cout<<"Workspace corruption: "<<checks<<" checks passed\n";return 0;}
 if(legacyMode){Check(Window("Lua Runtime Inspector")->DockId!=0,"new panel gets ordinary docking slot in old layout");Check(ui.SaveCurrentWorkspace(app),ui.GetError());EditorPreferences migrated;Check(LoadPreferences(PreferencesPath(),migrated,&error)&&migrated.workspacesInitialized&&migrated.workspaces.contains("level"),"legacy layout migrates once to workspace preferences");app.Shutdown();std::ifstream file(directory/"imgui.ini",std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(file)),{});Check(bytes==legacyBytes,"legacy imgui.ini remains unchanged");std::cout<<"Workspace migration: "<<checks<<" checks passed\n";return 0;}
 auto menu=[&](const char* label,bool workspace=true){
   auto* bar=Window("##MainMenuBar");ImGui::FocusWindow(bar);ImGui::ActivateItemByID(ImHashStr("View",0,ImHashStr("##MenuBar",0,bar->ID)));frame(1);
   auto& stack=ImGui::GetCurrentContext()->OpenPopupStack;Check(!stack.empty()&&stack.back().Window,"View menu opens");auto* popup=stack.back().Window;
   if(workspace){ImGui::ActivateItemByID(ImHashStr("Workspaces",0,popup->ID));frame(1);Check(ImGui::GetCurrentContext()->OpenPopupStack.Size>=2,"Workspaces submenu opens");popup=ImGui::GetCurrentContext()->OpenPopupStack.back().Window;}
   ImGui::ActivateItemByID(ImHashStr(label,0,popup->ID));frame(1);frame();
 };
 if(restore){
   ui.SwitchWorkspace(EditorWorkspace::LevelEditing);frame();
   auto* inspector=Window("Inspector");Check(!inspector->DockId&&std::abs(inspector->Pos.x-45)<3&&std::abs(inspector->SizeFull.x-300)<3,"new process restores floating Inspector geometry");
   // Switching now captures the outgoing layout in memory. Establish the disk
   // baseline after that capture before checking that Reset changes only Level.
   Check(ui.PersistPreferences(app),ui.GetError());
   EditorPreferences saved;Check(LoadPreferences(PreferencesPath(),saved,&error),error);auto other=saved.workspaces.at("scripting");
   ui.SetTransformPrecision(2);ui.SetAssetCardSize(220);ui.GetVisualSettings().gridOpacity=.123f;
   menu("Reset Current Layout");frame();Check(Window("Inspector")->DockId!=0,"Reset restores builtin docking");Check(LoadPreferences(PreferencesPath(),saved,&error),error);Check(saved.workspaces.at("scripting")==other,"Reset does not erase another workspace");Check(saved.transformPrecision==2&&saved.assetCardSize==220&&saved.visuals.gridOpacity==.123f,"Reset preserves unrelated preferences");
   Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==before&&ui.GetSelectedEntity()==entity,"Reset preserves scene and selection");
   // Corrupt one record without discarding unrelated settings or healthy presets.
   auto data=PreferencesToJson(saved);data["workspaces"]["level"]["ini"]="[Docking][Data]\nDockSpace ID=0x516E9E8C Parent=0x516E9E8C\n";data["cameraSpeed"]=4.2;
   auto recovered=PreferencesFromJson(data);Check(!recovered.workspaces.contains("level")&&recovered.workspaces.contains("scripting")&&recovered.cameraSpeed==4.2f,"damaged workspace is isolated from other preferences");
   Check(!ValidWorkspaceIni("broken")&&!ValidWorkspaceIni(std::string(1024*1024+1,'x')),"invalid and oversized layouts rejected");
   app.Shutdown();std::cout<<"Workspace restart: "<<checks<<" checks passed\n";return 0;
 }
 Window("Scene Hierarchy");auto* viewport=Window("Viewport");auto* assets=Window("Asset Browser");Check(viewport->Size.x>assets->Size.y&&viewport->Size.y>assets->Size.y,"Level Editing emphasizes viewport");
 auto* inspector=Window("Inspector");ImGui::SetWindowDock(inspector,0,ImGuiCond_Always);ImGui::SetWindowPos(inspector,{45,110});ImGui::SetWindowSize(inspector,{300,360});
 auto* assetNode=ImGui::DockBuilderGetNode(assets->DockId);Check(assetNode!=nullptr,"Asset Browser is docked");ImGui::DockBuilderSetNodeSize(assetNode->ID,{assetNode->Size.x,145});frame();const float assetHeight=Window("Asset Browser")->Size.y;
 menu("Console / Diagnostics",false);Check(!ImGui::FindWindowByName("Console / Diagnostics")->Active,"panel can be closed via View");{std::size_t count=0;const auto* data=ImGui::SaveIniSettingsToMemory(&count);std::ofstream dump(directory/"native-layout.ini");dump.write(data,count);}menu("Save Current Layout");Check(ui.SaveCurrentWorkspace(app),"Save Current Layout: "+ui.GetError());
 EditorPreferences saved;Check(LoadPreferences(PreferencesPath(),saved,&error),error);Check(saved.workspaces.contains("level")&&ValidWorkspaceIni(saved.workspaces.at("level").ini),"native ini saved in preferences");Check(!saved.workspaces.at("level").visible.at("Console / Diagnostics"),"visibility saved per workspace");
 menu("Scripting");Check(ui.GetWorkspace()==EditorWorkspace::Scripting,"native workspace menu selection");Window("Lua Runtime Inspector");Window("Gameplay Events");Check(!app.IsEditorPlayMode(),"scripting workspace available in Edit");Check(ui.SaveCurrentWorkspace(app),ui.GetError());
 menu("Level Editing");inspector=Window("Inspector");Check(!inspector->DockId&&std::abs(inspector->Pos.x-45)<3&&std::abs(inspector->SizeFull.x-300)<3,"switch restores moved Inspector");Check(std::abs(Window("Asset Browser")->Size.y-assetHeight)<4,"switch restores resized Asset Browser");Check(!ImGui::FindWindowByName("Console / Diagnostics")->Active,"closed panel remains closed after restore");
 menu("Asset Management");Check(Window("Asset Browser")->Size.x>Window("Viewport")->Size.x,"Asset Management emphasizes existing Asset Browser");
 menu("Gameplay Debugging");Window("Gameplay Events");Window("Lua Runtime Inspector");Window("Scene Validation");Window("Console / Diagnostics");
 Check(app.GetWorld().GetIdentity()==identity&&SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==before&&ui.GetSelectedEntity()==entity&&ui.UndoCount()==undo,"switching preserves World, scene, selection and Undo");
 app.SetEditorPlayMode(true);frame(5);auto& runtime=app.GetWorld();auto runtimeIdentity=runtime.GetIdentity();auto door=Named(runtime,"DoorLeaf");ui.SelectEntity(door);ui.UpdateLuaRuntimeInspector(app,ImGui::GetTime(),true);auto instance=ui.GetLuaInspection().info.serial;auto& history=app.GetLuaRuntime().Events();history.StartCapture();auto session=history.Session();app.SetEditorPaused(true);const auto rows=history.Rows().size();auto runtimeBefore=SceneSerializer::CaptureWorld(runtime,app.GetSceneSettings());
 for(auto workspace:{EditorWorkspace::LevelEditing,EditorWorkspace::AssetManagement,EditorWorkspace::Scripting,EditorWorkspace::GameplayDebugging}){ui.SwitchWorkspace(workspace);frame();Check(app.IsEditorPlayMode()&&app.IsEditorPaused()&&runtime.GetIdentity()==runtimeIdentity&&ui.GetSelectedEntity()==door,"workspace switch preserves paused runtime and selection");Check(history.Session()==session&&history.Rows().size()==rows&&history.Capturing(),"event history and capture survive workspace switch");Check(app.GetLuaRuntime().InspectionInfo(door).serial==instance,"live Lua instance survives workspace switch");}
 Check(SceneSerializer::CaptureWorld(runtime,app.GetSceneSettings())==runtimeBefore,"layout changes do not mutate runtime data");app.SetEditorPaused(false);app.SetEditorPlayMode(false);Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==before,"Editor World and prefab links preserved after Play");
 // Resolution changes and a repeated native restore do not leave floating panels unreachable.
 auto* handle=dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle();glfwSetWindowSize(handle,960,640);frame();ui.SwitchWorkspace(EditorWorkspace::LevelEditing);frame();inspector=Window("Inspector");Check(inspector->Pos.x>=0&&inspector->Pos.y>=0,"restored floating panel reachable on smaller window");
 // Leave a known saved layout for the separate-process restart fixture.
 ImGui::SetWindowDock(inspector,0,ImGuiCond_Always);ImGui::SetWindowPos(inspector,{45,110});ImGui::SetWindowSize(inspector,{300,360});frame();Check(ui.SaveCurrentWorkspace(app),ui.GetError());std::filesystem::create_directories(directory/"diagnostics");auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(gpu->SaveFrameImage("workspace-regression/diagnostics/workspace.ppm"),"native workspace screenshot");Check(gpu->GetValidationErrorCount()==0,"no DX12 validation errors");ui.SwitchWorkspace(EditorWorkspace::Scripting);frame();app.Shutdown();
 std::cout<<"Workspaces: "<<checks<<" checks passed\n";return 0;
 }catch(const std::exception& error){std::cerr<<"WORKSPACE FAILED after "<<checks<<": "<<error.what()<<'\n';return 1;}}
