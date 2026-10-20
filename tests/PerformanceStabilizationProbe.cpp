#define NOMINMAX
#include "ApplicationTestAccess.h"
#include "resources/ResourceManager.h"
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "scene/SceneSerializer.h"
#include "ecs/components/UICanvasComponent.h"
#include "ecs/components/FogComponent.h"
#include "ecs/components/PostProcessVolumeComponent.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <imgui_internal.h>
#include <fstream>
#include <iostream>
#include <cstdlib>

namespace {
void Check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
double Percentile(std::vector<double> v,double p){std::sort(v.begin(),v.end());return v[std::min(v.size()-1,std::size_t(std::ceil(p*v.size())-1))];}
}
int main(int argc,char** argv){try{
 Check(argc==3,"PerformanceStabilizationProbe scene.json output.json");
 ScopedEditorPreferenceRestore preferences;
 if(const char* project=std::getenv("WHISP_PERF_PROJECT")) {
  // The supplied descriptor references a missing startup scene. Correct that
  // reference only in this diagnostic context; never rewrite the user's file.
  auto context=std::make_shared<ProjectContext>();context->descriptorPath=std::filesystem::absolute(std::filesystem::u8path(project)).lexically_normal();context->root=context->descriptorPath.parent_path();
  std::ifstream descriptor(context->descriptorPath);descriptor>>context->descriptor;
  context->assetDirectory=context->descriptor.at("directories").at("assets");context->sceneDirectory=context->descriptor.at("directories").at("scenes");context->startupScene="Scenes/InteractiveRoom.json";
  ProjectContext::CheckNoLinks(context->AssetRoot());ProjectManager::Activate(context);
 }
 Application app;Check(app.Initialize(),"initialize");SyntheticEditorInput input;
 ecs::World staged;SceneSettings settings;std::string error;
 Check(SceneSerializer::LoadWorld(std::filesystem::u8path(argv[1]),staged,settings,&error),error);
 app.NewScene();app.GetWorld().ReplaceData(staged);Check(app.SetSceneSettings(settings),"settings");
 const auto authored=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());
 auto& editor=app.GetEditorLayer();editor.SetFixedDiagnosticLayout(true);editor.SetDiagnosticViewport(1280,720);
 editor.SetLightGizmosVisible(true);editor.SetGridVisible(true);editor.SetGameView(false);
 const ecs::Vec3 camera{-1.85f,1.69f,-7.3f};Check(app.SetEditorCameraPose(camera,0,0),"camera");
 ApplicationTestAccess::FrameDelta(app,1.f/60);
 auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(gpu,"DX12 required");
 nlohmann::json output={{"projectMode",bool(ProjectManager::Active())},{"scene",argv[1]},{"resolution",{1280,720}},{"camera",{camera.x,camera.y,camera.z}},
 {"debugLayerRequested",std::getenv("WHISP_DX12_DEBUG")!=nullptr},{"gbvRequested",std::getenv("WHISP_DX12_GBV")!=nullptr},
#ifdef NDEBUG
 {"build","optimized"}
#else
 {"build","Debug"}
#endif
 };
 auto frames=[&](unsigned n){app.SetFrameLimit(n);Check(app.Run()==0,"frames");};
 auto measure=[&](const char* name){
  std::map<std::string,std::vector<double>> samples;unsigned frame=0;
  ApplicationTestAccess::ObserveFrames(app,[&]{
   if(frame++<24)return;
   if(std::string(name)=="H_clean_play")Check(app.IsGameplayMouseCaptured(),"Clean Play sample lost input capture");
   const auto cpu=ApplicationTestAccess::FrameCpuMs(app);const auto waits=gpu->GetCpuWaitMs();const auto t=gpu->GetGpuTimings();Check(t.valid,"GPU timestamps");
   auto add=[&](const char* key,double value){samples[key].push_back(value);};
   add("wall",cpu[6]);add("cpu",cpu[6]-waits[0]-waits[1]-waits[2]);
   add("resource",cpu[0]);add("luaUpdate",cpu[1]);add("fixedSimulation",cpu[2]);add("physics",app.GetPhysicsFrameStatistics().physics.totalMs);
   add("ecs",cpu[3]);add("worldRender",cpu[4]);add("imguiSubmit",cpu[5]);
   add("slotWait",waits[0]);add("present",waits[1]);add("fenceWait",waits[2]);
   const auto& r=app.GetRenderStatistics();add("material",r.materialResolveMs);add("path",r.materialPathMs);
   add("uiLayout",r.runtimeUILayoutMs);add("uiSubmit",r.runtimeUISubmitMs);add("gpuFinalize",r.gpuFinalizationMs);
   add("editorUI",editor.GetStatistics().uiMs);add("gizmo",editor.GetStatistics().lightGizmoMs);
   add("gpu",t.frameMs);add("shadowGpu",t.shadowMs);add("mainGpu",t.mainMs);add("outputGpu",t.outputMs);add("customGpu",t.customPostFxMs);add("uiGpu",t.runtimeUIMs);add("builtinOutputGpu",t.outputMs-t.customPostFxMs);
   add("fogGpu",gpu->GetFogDiagnostics().gpuMs);add("lightCullGpu",gpu->GetSceneLightDiagnostics().cullingGpuMs);
  });
  frames(48);ApplicationTestAccess::ObserveFrames(app,{});
  nlohmann::json row;for(auto& [key,values]:samples)row[key]={{"median",Percentile(values,.5)},{"p95",Percentile(values,.95)}};
  row["fps"]=1000/row["wall"]["median"].get<double>();row["draws"]=gpu->GetSubmissionStatistics().drawCalls;
  row["uploadBytes"]=gpu->GetSubmissionStatistics().uploadBytes;row["instanceGrowth"]=gpu->GetSubmissionStatistics().instanceBufferGrowths;
  row["viewport"]={editor.GetViewportPixelWidth(),editor.GetViewportPixelHeight()};row["renderArea"]=gpu->RenderAreaSize();
  row["instancing"]=app.IsGpuInstancingEnabled();row["shadowResolution"]=gpu->GetSubmissionStatistics().shadowResolution;
  row["aaSamples"]=gpu->GetAntiAliasingDiagnostics().effectiveSamples;row["fogSamples"]=gpu->GetFogDiagnostics().samples;
  row["uiInstances"]=ApplicationTestAccess::RuntimeUIInstances(app);row["captured"]=app.IsGameplayMouseCaptured();row["cameraOwner"]=app.GetLuaRuntime().HasCameraOwner();row["luaError"]=app.GetLuaRuntime().Error("scripts/FirstPersonController.lua");
  row["postFxPasses"]=ApplicationTestAccess::PostProcess(app).customEffects.size();
  output["phases"][name]=row;std::ofstream(argv[2])<<output.dump(2);
  Check(gpu->GetValidationErrorCount()==0,"DX12 validation errors");std::cout<<name<<": "<<row["fps"]<<" FPS\n";
 };
 measure("A_edit");
 app.SetEditorPlayMode(true);
 bool captureForSample=false;auto held=std::make_shared<std::unordered_set<std::string>>();auto host=ApplicationTestAccess::GameplayHost(app);
 scripting::GameplayCamera scriptCamera;host.camera=&scriptCamera;host.inputAllowed=[](){return true;};host.pressed=[&](const std::string& action){return held->contains(action)||(captureForSample&&action=="CaptureCursor");};host.released={};host.mouseDelta=[](){return ecs::Vec3{};};
 host.cursorCaptured=[&](){return app.IsGameplayMouseCaptured();};host.captureCursor=[&](bool v){return ApplicationTestAccess::GameplayCapture(app,v);};
 app.GetLuaRuntime().Start(app.GetWorld(),[held](const std::string& key){return held->contains(key);},host);
 ApplicationTestAccess::FixedGameplayCamera(app,camera);
 frames(2);Check(app.GetLuaRuntime().HasCameraOwner(),"FPS Lua: "+app.GetLuaRuntime().Error("scripts/FirstPersonController.lua"));measure("B_play");
 auto canvas=app.GetWorld().CreateEntity();app.GetWorld().AddComponent<ecs::UICanvasComponent>(canvas).document="ui/PlayerHUD.ui.json";
 measure("C_play_ui");
 measure("D_no_volumetric_fog"); // Authored room has no Fog component.
 measure("E_no_custom_postfx"); // Authored room has no PostProcessVolume.
 app.GetWorld().GetComponent<ecs::UICanvasComponent>(canvas)->enabled=false;measure("F_no_ui");
 app.GetWorld().GetComponent<ecs::UICanvasComponent>(canvas)->enabled=true;
 editor.SetGameView(true);editor.SetLightGizmosVisible(false);editor.SetGridVisible(false);measure("G_no_overlays");
 editor.SetGameView(false);editor.SetLightGizmosVisible(true);editor.SetGridVisible(true);
 captureForSample=true;ImGui::FocusWindow(ImGui::FindWindowByName("Viewport"));held->insert("CaptureCursor");frames(1);held->clear();Check(app.IsGameplayMouseCaptured(),"FPS cursor capture");
 ImGui::FocusWindow(ImGui::FindWindowByName("Viewport"));SyntheticEditorInput::Key(ImGuiKey_F11,true);frames(1);SyntheticEditorInput::Key(ImGuiKey_F11,false);frames(1);
 Check(editor.IsViewportMaximized(),"F11");measure("H_clean_play");
 auto fogEntity=app.GetWorld().CreateEntity();auto& fog=app.GetWorld().AddComponent<ecs::FogComponent>(fogEntity);fog.mode=FogMode::Volumetric;
 measure("diagnostic_volumetric");fog.enabled=false;
 auto ppv=app.GetWorld().CreateEntity();auto& volume=app.GetWorld().AddComponent<ecs::PostProcessVolumeComponent>(ppv);auto effect=app.GetResourceManager()->Load<PostFxResource>("postfx/vhs/VHS.postfx.json");Check(effect->IsUsable()&&bool(effect->GetData().definition),"VHS load");volume.customEffects.push_back({"postfx/vhs/VHS.postfx.json",effect->GetData().definition->id,true,1});
 measure("diagnostic_vhs");
 captureForSample=false;held->insert("ReleaseCursor");frames(2);held->clear();Check(!app.IsGameplayMouseCaptured(),"Tab release");
 app.SetEditorPaused(true);frames(2);app.SetEditorPaused(false);frames(2);app.SetEditorPlayMode(false);frames(2);
 Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==authored,"Stop restores authored room");
 Check(gpu->GetValidationErrorCount()==0,"final validation");app.Shutdown();return 0;
 }catch(const std::exception& e){std::cerr<<"PERFORMANCE PROBE: "<<e.what()<<'\n';return 1;}}
