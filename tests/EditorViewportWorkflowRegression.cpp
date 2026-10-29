#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "ApplicationTestAccess.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "editor/AssetCatalog.h"
#include "editor/EditorTheme.h"
#include "scene/SceneSerializer.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "ecs/components/PostProcessVolumeComponent.h"
#include "editor/LightGizmoMath.h"
#include "resources/ResourceManager.h"
#include <ImGuizmo.h>
#include <algorithm>
#include <cstdio>
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
using namespace ecs;using namespace editor;
static unsigned checks=0;
static bool Same(Vec3 a,Vec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
static void Check(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
static double Difference(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b){Check(!a.empty()&&a.size()==b.size(),"comparable GPU readbacks");double d=0;for(size_t i=0;i<a.size();++i)d+=std::abs(int(a[i])-int(b[i]));return d/a.size();}
static void Ppm(const fs::path& path,const std::vector<unsigned char>& pixels,int w,int h){std::ofstream out(path,std::ios::binary);out<<"P6\n"<<w<<" "<<h<<"\n255\n";for(size_t i=0;i<pixels.size();i+=4)out.write(reinterpret_cast<const char*>(pixels.data()+i),3);Check(bool(out),"GPU image saved");}
namespace editor {struct EditorDocumentTestAccess {
 static float Step(const EditorLayer& e){return e.m_SnapRotation;}
 static int Tool(const EditorLayer& e){return e.m_GizmoOperation;}
 static int Space(const EditorLayer& e){return e.m_GizmoMode;}
 static void Debug(const EditorLayer& e){std::cout<<"gizmo modal="<<e.m_ModalActive<<" game="<<e.m_GameView<<" cancel="<<e.m_CancelGizmoUntilRelease<<" bounds="<<e.m_ViewportHasBounds<<" selection="<<e.m_Selection.items.size()<<" tool="<<e.m_GizmoOperation<<" undoCapture="<<e.m_GizmoUndoCaptured<<std::endl;}
};}
int main(int argc,char** argv){try{
 if(argc>1&&std::string(argv[1])=="--paths"){std::cout<<"cwd="<<fs::current_path()<<" preferences="<<PreferencesPath()<<" exe="<<AssetPaths::GetExecutableDirectory()<<'\n';return 0;}
 ScopedEditorPreferenceRestore prefs;
 const bool before=argc>1&&std::string(argv[1])=="--before";
 Application app;Check(app.Initialize(),"initialize real editor");auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle();glfwHideWindow(window);SyntheticEditorInput input;ApplicationTestAccess::FrameDelta(app,1.f/30);
 auto output=AssetPaths::GetExecutableDirectory()/"diagnostics/editor32";fs::create_directories(output);
 const auto parent=fs::current_path()/("fixture-"+NewSceneIdentity());Check(fs::create_directory(parent),"owned fixture root");
 struct Cleanup{fs::path root;~Cleanup(){ProjectManager::Activate({});std::error_code ec;fs::remove_all(root,ec);}} cleanup{parent};
 auto project=ProjectManager::Create(parent,"ViewportLab");std::string error;Check(app.OpenProject(project->descriptorPath,Application::ProjectDecision::Discard,&error),error);
 auto& ui=app.GetEditorLayer();auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(gpu,"DX12 renderer");auto frame=[&](unsigned n=5){app.SetFrameLimit(n);Check(app.Run()==0,"native frames");};
 ui.SetDiagnosticViewport(480,270);auto cube=ui.CreatePrimitive(app,"cube");Check(cube.IsValid(),"actual cube");ui.ClearSelection();ui.SetGameView(true);ui.SetGridVisible(false);
 auto* transform=app.GetWorld().GetComponent<TransformComponent>(cube);transform->position={0,0,0};transform->scale={2.5f,2.5f,2.5f};
 const auto meshKey=app.GetWorld().GetComponent<MeshRendererComponent>(cube)->meshPath;auto mesh=app.GetResourceManager()->Load<MeshResource>(meshKey);Check(mesh->IsLoaded()&&!mesh->GetData().placeholder,"actual mesh, no placeholder");
 Check(app.SetEditorCameraPose({0,1,-5},0,-.12f),"fixed camera");frame(20);
 Check(gpu->SaveFrameImage((output/(before?"before-toolbar.ppm":"after-toolbar.ppm")).string()),"toolbar GPU capture");
 if(!before){
  auto* viewport=ImGui::FindWindowByName("Viewport");Check(viewport,"native Viewport window");
  ImGui::SetWindowDock(viewport,0,ImGuiCond_Always);ImGui::SetWindowPos(viewport,{10,100},ImGuiCond_Always);ImGui::SetWindowSize(viewport,{1120,580},ImGuiCond_Always);ImGui::SetWindowFocus("Viewport");frame();
  auto press=[&](ImGuiWindow* w,const char* label,ImGuiID seed=0){Check(w&&w->Active,std::string("active UI window: ")+label);ImGui::ActivateItemByID(ImHashStr(label,0,seed?seed:w->ID));frame(2);};
  auto scope=[&]{return ImHashStr("ViewportTransform",0,viewport->ID);};
  auto* bar=ImGui::FindWindowByName("Main Toolbar");SyntheticEditorInput::Position(bar->WorkRect.Min.x+29,bar->WorkRect.Min.y+bar->FontRefSize*.5f+ImGui::GetStyle().FramePadding.y);frame(12);
  bool tooltip=false;for(auto* w:GImGui->Windows)tooltip|=w->Active&&std::string(w->Name).starts_with("##Tooltip_");Check(tooltip,"real toolbar hover tooltip");SyntheticEditorInput::Position(-100,-100);frame();
  auto shading=[&](bool unlit){if(app.IsViewportUnlit()!=unlit)press(viewport,app.IsViewportUnlit()?"Unlit":"Lit");frame();Check(app.IsViewportUnlit()==unlit,"native Lit/Unlit toggle");};
  auto settings=app.GetSceneSettings();settings.lighting.enabled=false;settings.environment.exposure=1;settings.environment.toneMapping=true;settings.antiAliasing=AntiAliasingMode::FxaaHigh;Check(app.SetSceneSettings(settings),"fixed HDR/output and lighting fixture");
  auto volumeEntity=ui.CreatePostProcessVolume(app,true);auto localEntity=ui.CreatePostProcessVolume(app,false);ui.ClearSelection();ui.SetGameView(true);
  auto volume=[&]()->PostProcessVolumeComponent&{return *app.GetWorld().GetComponent<PostProcessVolumeComponent>(volumeEntity);};
  auto local=[&]()->PostProcessVolumeComponent&{return *app.GetWorld().GetComponent<PostProcessVolumeComponent>(localEntity);};
  volume()={};local()={};local().global=false;local().extents={10,10,10};local().enabled=false;
  frame(30);Check(app.GetRenderStatistics().visibleSubmeshes>0,"fixture geometry is visible");auto image=[&]{return gpu->ReadBackViewportPixels();};auto clean=image();
  Ppm(output/"clean.ppm",clean,480,270);
  shading(true);auto cleanUnlit=image();Ppm(output/"clean-unlit.ppm",cleanUnlit,480,270);shading(false);
  std::ofstream(output/"authored-clean.json")<<SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings()).dump(2);
  std::ofstream numbers(output/"unlit-gpu.csv");numbers<<"case,lit_clean_mae,unlit_clean_mae,restored_lit_mae\n";
  for(int effect=0;effect<5;++effect){
    volume()={};local().enabled=false;
    if(effect==0)volume().vignetteIntensity=1;
    if(effect==1)volume().grainIntensity=.7f;
    if(effect==2)volume().aberrationIntensity=1;
    if(effect==3){volume().brightness=.25f;volume().tint=std::array<float,4>{1,.3f,.5f,1};}
    if(effect==4){volume().vignetteIntensity=.6f;local().enabled=true;local().priority=10;local().brightness=.2f;}
    frame();auto lit=image();const auto authored=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());auto undo=ui.UndoCount();
    shading(true);auto unlit=image();Ppm(output/("unlit-effect-"+std::to_string(effect)+".ppm"),unlit,480,270);Check(Difference(cleanUnlit,unlit)<=.01,"Unlit suppresses artistic screen effect on real GPU");
    Check(ui.UndoCount()==undo&&SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==authored,"view mode preserves authored World/settings/history");
    shading(false);auto restored=image();Check(Difference(clean,lit)>.05,"effect has measurable Lit pixels");if(effect!=1)Check(Difference(lit,restored)<=.01,"static Lit stack restored exactly");
    numbers<<effect<<','<<Difference(clean,lit)<<','<<Difference(cleanUnlit,unlit)<<','<<Difference(lit,restored)<<'\n';
    Ppm(output/("lit-effect-"+std::to_string(effect)+".ppm"),lit,480,270);Ppm(output/("unlit-effect-"+std::to_string(effect)+".ppm"),unlit,480,270);
  }
  volume()={};local().enabled=false;AssetCatalog catalog;Check(catalog.CreateStarter("effects/Lens.postfx.json","Lens Distortion",&error),error);Check(ui.AddCustomPostFx(app,volumeEntity,"effects/Lens.postfx.json"),ui.GetError());ui.ClearSelection();ui.SetGameView(true);
  for(int i=0;i<100&&gpu->CustomPostFxStatus("effects/Lens.postfx.json")!="Ready";++i)frame(2);Check(gpu->CustomPostFxStatus("effects/Lens.postfx.json")=="Ready","actual Lens shader compiled");
  frame();auto lens=image();Check(Difference(clean,lens)>.01,"actual custom Lens changes pixels");auto authored=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());auto undo=ui.UndoCount();
  shading(true);Check(Difference(cleanUnlit,image())<=.01,"Unlit suppresses custom Lens");Ppm(output/"unlit-lens.ppm",image(),480,270);
  Check(gpu->GetSubmissionStatistics().outputDrawCalls==1,"Unlit submits no custom fullscreen pass");shading(false);Check(Difference(lens,image())<=.01,"Lit restores original Lens stack pixels");Ppm(output/"lit-lens.ppm",image(),480,270);
  Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==authored&&ui.UndoCount()==undo,"Lens mode toggle is not an authoring edit");
  // Keep the requested editor Unlit while Runtime uses its authored stack.
  shading(true);press(ImGui::FindWindowByName("Main Toolbar"),"Play");Check(app.IsEditorPlayMode(),"native Play");frame(15);Check(gpu->GetSubmissionStatistics().outputDrawCalls==2,"Runtime keeps custom PostFX despite editor Unlit request");press(ImGui::FindWindowByName("Main Toolbar"),"Pause");Check(app.IsEditorPaused(),"native Pause");press(ImGui::FindWindowByName("Main Toolbar"),"Stop");Check(!app.IsEditorPlayMode(),"native Stop");frame();Check(Difference(cleanUnlit,image())<=.01,"Stop restores editor Unlit suppression");Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==authored,"Play/Stop preserves authored stack");
  for(auto size:{std::pair{320,180},std::pair{640,360},std::pair{480,270}}){ui.SetDiagnosticViewport(size.first,size.second);frame();Check(image().size()==size_t(size.first*size.second*4),"Unlit HDR output survives resize");}shading(false);
  // Real toolbar toggle, all presets and typed custom value; no helper changes settings.
  ui.SelectEntity(cube);ui.SetGameView(false);press(viewport,"Rotate",scope());Check(EditorDocumentTestAccess::Tool(ui)==1,"viewport Rotate control");
  auto choose=[&](float value){press(viewport,"##Rotation step",scope());auto* popup=GImGui->OpenPopupStack.back().Window;char label[32];std::snprintf(label,sizeof(label),"%g deg",double(value));press(popup,label);Check(EditorDocumentTestAccess::Step(ui)==value,"native snap preset");};
  press(viewport,"Rotate Snap",scope());Check(ui.IsRotationSnapping(),"native rotation snap ON");
  const auto beforeTools=ui.UndoCount();for(float step:{5.f,10.f,15.f,30.f,45.f,90.f})choose(step);
  press(viewport,"##Rotation step",scope());auto* popup=GImGui->OpenPopupStack.back().Window;ImGui::ActivateItemByID(popup->GetID("Custom step (deg)"));GImGui->NavNextActivateFlags=ImGuiActivateFlags_PreferInput;frame(2);Check(ImGui::GetIO().WantTextInput,"custom step actual numeric field");SyntheticEditorInput::Queue([](ImGuiIO& io){io.AddInputCharactersUTF8("7.5");});frame(1);SyntheticEditorInput::Key(ImGuiKey_Enter,true);frame(1);SyntheticEditorInput::Key(ImGuiKey_Enter,false);frame();Check(EditorDocumentTestAccess::Step(ui)==7.5f,"custom rotation snap committed");ImGui::ClosePopupToLevel(0,true);frame();Check(ui.UndoCount()==beforeTools,"toolbar state creates no scene Undo");
  for(auto item:{std::pair{ImGuiKey_W,0},std::pair{ImGuiKey_E,1},std::pair{ImGuiKey_R,2}}){ImGui::SetWindowFocus("Viewport");frame(1);SyntheticEditorInput::Key(item.first,true);frame(1);SyntheticEditorInput::Key(item.first,false);frame();Check(EditorDocumentTestAccess::Tool(ui)==item.second,"existing W/E/R hotkey routing");}press(viewport,"Rotate",scope());
  auto space=[&](int mode){press(viewport,"##Transform space",scope());auto* menu=GImGui->OpenPopupStack.back().Window;press(menu,mode?"World":"Local",ImHashData(&mode,sizeof(mode),menu->ID));Check(EditorDocumentTestAccess::Space(ui)==mode,"native Local/World");Check(GImGui->OpenPopupStack.empty(),"space combo selection closes popup");};
  auto rotate=[&](Entity entity,float step,int mode){
    choose(step);space(mode);ui.SelectEntity(entity);ui.SetGameView(false);Check(app.SetEditorCameraPose({0,0,-6},0,0),"front rotation camera");
    SyntheticEditorInput::Position(-100,-100);frame();auto bounds=ui.GetViewportBounds();ImVec2 center{(bounds[0]+bounds[2])*.5f,(bounds[1]+bounds[3])*.5f};
    std::vector<float> radii;for(const auto& v:viewport->DrawList->VtxBuffer)if(v.col==IM_COL32_WHITE){float r=std::hypot(v.pos.x-center.x,v.pos.y-center.y);if(r>15&&r<150)radii.push_back(r);}
    if(radii.size()<=50){EditorDocumentTestAccess::Debug(ui);std::cout<<"bounds "<<bounds[0]<<','<<bounds[1]<<','<<bounds[2]<<','<<bounds[3]<<" vertices="<<viewport->DrawList->VtxBuffer.Size<<" radii="<<radii.size()<<" alive="<<app.GetWorld().IsAlive(entity)<<std::endl;gpu->SaveFrameImage((output/"gizmo-debug.ppm").string());std::ofstream(output/"gizmo-world.json")<<SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings()).dump(2);}
    Check(radii.size()>50,"actual ImGuizmo screen-rotation circle vertices");std::sort(radii.begin(),radii.end());float radius=radii[radii.size()/2];
    auto point=[&](float degrees){float angle=degrees*.01745329252f;return ImVec2{center.x+radius*std::cos(angle),center.y+radius*std::sin(angle)};};auto start=point(38),end=point(101);
    const auto original=*app.GetWorld().GetComponent<TransformComponent>(entity);const auto panelPosition=viewport->Pos;auto history=ui.UndoCount();SyntheticEditorInput::Position(start.x,start.y);frame(2);SyntheticEditorInput::Button(0,true);frame(1);Check(ImGuizmo::IsUsing(),"actual gizmo rotation capture");auto handle=ImGuizmo::GetActiveHandleType();SyntheticEditorInput::Position(end.x,end.y);frame(2);Check(viewport->Pos.x==panelPosition.x&&viewport->Pos.y==panelPosition.y,"gizmo drag must not move the floating panel");
    auto held=*app.GetWorld().GetComponent<TransformComponent>(entity);auto delta=ImGui::GetMouseDragDelta(0);std::cout<<"held rotation="<<held.rotation.x<<','<<held.rotation.y<<','<<held.rotation.z<<" mouse="<<ImGui::GetIO().MousePos.x<<','<<ImGui::GetIO().MousePos.y<<" expected="<<end.x<<','<<end.y<<" drag="<<delta.x<<','<<delta.y<<" using="<<ImGuizmo::IsUsing()<<" selected="<<(ui.GetSelectedEntity()==entity)<<std::endl;EditorDocumentTestAccess::Debug(ui);
    SyntheticEditorInput::Button(0,false);frame();
    auto rotated=*app.GetWorld().GetComponent<TransformComponent>(entity);auto q=RenderRotation(rotated.rotation),p=RenderRotation(original.rotation);double dot=std::clamp(std::abs(double(q.x)*p.x+double(q.y)*p.y+double(q.z)*p.z+double(q.w)*p.w),0.,1.);double angle=2*std::acos(dot)*57.295779513;std::cout<<"rotation step="<<step<<" mode="<<mode<<" snap="<<ui.IsRotationSnapping()<<" radius="<<radius<<" handle="<<int(handle)<<" angle="<<angle<<" rotation="<<rotated.rotation.x<<','<<rotated.rotation.y<<','<<rotated.rotation.z<<" error="<<ui.GetError()<<std::endl;Check(angle>1&&std::abs(angle/step-std::round(angle/step))<.015,"actual rotation delta is selected step multiple");Check(ui.UndoCount()==history+1,"rotation is one undo transaction");
    ImGui::SetWindowFocus("Viewport");SyntheticEditorInput::Key(ImGuiMod_Ctrl,true);frame(1);SyntheticEditorInput::Key(ImGuiKey_Z,true);frame(1);SyntheticEditorInput::Key(ImGuiKey_Z,false);SyntheticEditorInput::Key(ImGuiMod_Ctrl,false);frame();Check(Same(app.GetWorld().GetComponent<TransformComponent>(entity)->rotation,original.rotation),"native Undo rotation");
    SyntheticEditorInput::Key(ImGuiMod_Ctrl,true);frame(1);SyntheticEditorInput::Key(ImGuiKey_Y,true);frame(1);SyntheticEditorInput::Key(ImGuiKey_Y,false);SyntheticEditorInput::Key(ImGuiMod_Ctrl,false);frame();Check(Same(app.GetWorld().GetComponent<TransformComponent>(entity)->rotation,rotated.rotation),"native Redo rotation");ui.UndoEdit(app);frame();
  };
  // The direct pointer interaction, not SetProperty/UpdateGroupTransform, changes rotation.
  for(int mode:{0,1})for(float step:{5.f,10.f,15.f,30.f,45.f,90.f})rotate(cube,step,mode);
  choose(15);ui.SelectEntity(cube);Check(ui.CreatePrefab(app,"prefabs/Rotation.prefab.json"),ui.GetError());Check(ui.OpenDocument(app,"prefabs/Rotation.prefab.json"),ui.GetError());frame();rotate(ui.GetSelectedEntity(),15,0);Check(ui.ClosePrefabEditor(app,true),ui.GetError());frame();
  // Layout cases are isolated, with controls still reachable through overflow.
  const int decorated=glfwGetWindowAttrib(window,GLFW_DECORATED);glfwSetWindowAttrib(window,GLFW_DECORATED,GLFW_FALSE);
  std::ofstream layouts(output/"layouts.csv");layouts<<"requested_width,requested_height,actual_width,actual_height,exact,dpi,viewport_width,inspector_width\n";
  for(auto resolution:{std::pair{1280,720},std::pair{1920,1080},std::pair{2560,1440}})for(float dpi:{1.f,1.5f,2.f}){
    glfwSetWindowSize(window,resolution.first,resolution.second);ApplyEditorTheme(dpi);ImGui::SetWindowSize(viewport,{310*dpi,400*dpi},ImGuiCond_Always);frame(8);int actualWidth=0,actualHeight=0;glfwGetFramebufferSize(window,&actualWidth,&actualHeight);std::cout<<"layout requested="<<resolution.first<<'x'<<resolution.second<<" actual="<<actualWidth<<'x'<<actualHeight<<" dpi="<<dpi<<std::endl;Check(gpu->ReadBackFramePixels().size()==size_t(actualWidth*actualHeight*4),"GPU readback matches actual native framebuffer");
    Check(std::abs(ImGui::GetStyle().FontScaleDpi-dpi)<.01f,"requested ImGui DPI remains active");
    press(viewport,"Transform",scope());auto* overflow=GImGui->OpenPopupStack.back().Window;press(overflow,"Rotate");Check(EditorDocumentTestAccess::Tool(ui)==1,"narrow viewport transform overflow");ImGui::ClosePopupToLevel(0,true);frame();
    press(viewport,"Rotate Snap",scope());Check(!ui.IsRotationSnapping(),"narrow viewport snap OFF");press(viewport,"Rotate Snap",scope());Check(ui.IsRotationSnapping(),"narrow viewport snap ON");
    auto* inspector=ImGui::FindWindowByName("Inspector");Check(inspector,"Inspector window");ImGui::SetWindowDock(inspector,0,ImGuiCond_Always);ImGui::SetWindowPos(inspector,{actualWidth-290*dpi,100},ImGuiCond_Always);ImGui::SetWindowSize(inspector,{280*dpi,400*dpi},ImGuiCond_Always);frame();Check(inspector->Active,"narrow Inspector remains available");layouts<<resolution.first<<','<<resolution.second<<','<<actualWidth<<','<<actualHeight<<','<<(actualWidth==resolution.first&&actualHeight==resolution.second)<<','<<dpi<<','<<viewport->Size.x<<','<<inspector->Size.x<<'\n';
    if(resolution.first==1280&&dpi==1){frame();Check(gpu->SaveFrameImage((output/"narrow-toolbar.ppm").string()),"narrow controls GPU screenshot");}
  }
  glfwSetWindowAttrib(window,GLFW_DECORATED,decorated);glfwSetWindowSize(window,1280,720);ApplyEditorTheme(1);ImGui::SetWindowSize(viewport,{1120,580},ImGuiCond_Always);frame();choose(30);Check(ui.PersistPreferences(app),ui.GetError());
  frame();Check(gpu->SaveFrameImage((output/"wide-toolbar.ppm").string()),"wide controls GPU screenshot");
  EditorPreferences onDisk;Check(LoadPreferences(PreferencesPath(),onDisk,&error),error);Check(onDisk.rotationSnapping&&onDisk.rotationSnap==30,"existing preferences persist snap state");
  Check(app.OpenProject(project->descriptorPath,Application::ProjectDecision::Discard,&error),error);frame();Check(ui.IsRotationSnapping()&&EditorDocumentTestAccess::Step(ui)==30,"project reopen preserves editor snap");
 }
 Check(gpu->GetValidationErrorCount()==0,"DX12 validation zero");app.Shutdown();ProjectManager::Activate({});
 if(!before){Application restarted;Check(restarted.Initialize(),"fresh editor initialization");glfwHideWindow(dynamic_cast<GlfwWindow*>(restarted.GetWindow())->GetGlfwHandle());restarted.SetFrameLimit(3);Check(restarted.Run()==0,"fresh editor frames");Check(restarted.GetEditorLayer().IsRotationSnapping()&&EditorDocumentTestAccess::Step(restarted.GetEditorLayer())==30,"actual restart restores snap preferences");Check(dynamic_cast<Dx12RenderAdapter*>(restarted.GetRenderAdapter())->GetValidationErrorCount()==0,"restart validation zero");restarted.Shutdown();}
 std::cout<<"EditorViewportWorkflow: "<<checks<<" checks PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<"VIEWPORT32 FAILED: "<<e.what()<<'\n';return 1;}}
