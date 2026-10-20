#define NOMINMAX
#include "core/AssetPaths.h"
#include "core/Application.h"
#include "editor/MeshOcclusion.h"
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include <imgui_internal.h>
#include "core/ProjectContext.h"
#include "scene/SceneSerializer.h"
#include "ecs/systems/RenderSystem.h"
#include "ecs/components/LightComponent.h"
#include "ecs/components/MeshRendererComponent.h"
#include "resources/ResourceManager.h"
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <GLFW/glfw3.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
using Clock=std::chrono::steady_clock;
static double Ms(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
static void Check(bool v,const std::string& message){if(!v)throw std::runtime_error(message);}
static double Median(std::vector<double> v){std::sort(v.begin(),v.end());return v[v.size()/2];}
static bool BruteHit(const MeshData& mesh,ecs::Vec3 p,ecs::Vec3 d,float limit){
 auto sub=[](auto a,auto b){return ecs::Vec3{a.x-b.x,a.y-b.y,a.z-b.z};};auto cross=[](auto a,auto b){return ecs::Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};auto dot=[](auto a,auto b){return a.x*b.x+a.y*b.y+a.z*b.z;};
 auto vertex=[&](unsigned i){auto& v=mesh.vertices[mesh.indices[i]];return ecs::Vec3{v.position[0],v.position[1],v.position[2]};};
 for(unsigned i=0;i+2<mesh.indices.size();i+=3){auto a=vertex(i),e1=sub(vertex(i+1),a),e2=sub(vertex(i+2),a),h=cross(d,e2);float determinant=dot(e1,h);if(std::abs(determinant)<1e-10f)continue;float inverse=1/determinant;auto delta=sub(p,a);float u=inverse*dot(delta,h);if(u<0||u>1)continue;auto q=cross(delta,e1);float v=inverse*dot(d,q);if(v<0||u+v>1)continue;float t=inverse*dot(e2,q);if(t>=0&&t<limit)return true;}return false;
}
static int OcclusionRegression(){
 MeshData mesh;for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){unsigned first=unsigned(mesh.vertices.size());for(auto offset:{std::array<float,2>{-.5f,-.5f},std::array<float,2>{.5f,-.5f},std::array<float,2>{0,.5f}}){MeshVertex v;v.position[0]=x*2.f+offset[0];v.position[1]=y*2.f+offset[1];v.position[2]=5;mesh.vertices.push_back(v);}mesh.indices.insert(mesh.indices.end(),{first,first+1,first+2});}
 editor::MeshOcclusion tree(mesh);unsigned comparisons=0;
 for(float x=-1;x<=15;x+=.25f)for(float y=-1;y<=15;y+=.5f)for(float distance:{4.9f,5.f,5.1f,100.f}){ecs::Vec3 p{x,y,0},d{0,0,1};Check(tree.AnyHit(mesh,p,d,distance)==BruteHit(mesh,p,d,distance),"accelerated occlusion differs from original triangle oracle");++comparisons;}
 for(auto p:{ecs::Vec3{0,0,0},ecs::Vec3{1,1,0},ecs::Vec3{14,14,10}})for(auto d:{ecs::Vec3{0,0,.5f},ecs::Vec3{0,0,-1},ecs::Vec3{.1f,-.2f,1}})Check(tree.AnyHit(mesh,p,d,20)==BruteHit(mesh,p,d,20),"backface / nonuniform transform distance semantics");
 std::size_t tested=0;Check(tree.AnyHit(mesh,{0,0,0},{0,0,1},6,&tested),"triangle hit");Check(tested<=8,"one overlay ray must not scan all mesh triangles");tested=0;Check(!tree.AnyHit(mesh,{-100,-100,0},{0,0,1},100,&tested)&&tested==0,"root rejection performs no triangle tests");Check(!tree.AnyHit(mesh,{0,0,0},{0,0,.5f},9.9f)&&tree.AnyHit(mesh,{0,0,0},{0,0,.5f},10.1f),"inverse-transformed ray retains world-distance cutoff");
 auto moved=mesh;for(auto& v:moved.vertices)v.position[0]+=100;editor::MeshOcclusion rebuilt(moved);Check(!rebuilt.AnyHit(moved,{0,0,0},{0,0,1},6)&&rebuilt.AnyHit(moved,{100,0,0},{0,0,1},6),"rebuilt geometry uses new mesh positions");MeshData empty;Check(!editor::MeshOcclusion(empty).AnyHit(empty,{},{0,0,1},10),"empty mesh");
 std::cout<<"Spot gizmo occlusion: "<<comparisons<<" exact oracle comparisons, leaf <=8 triangles, transforms and rebuild passed\n";return 0;
}
static int EditorMeasurement(int argc,char** argv){
 Check(argc>=3,"--editor project output.json");ScopedEditorPreferenceRestore preferences;Application app;Check(app.Initialize(),"editor initialization");SyntheticEditorInput input;std::string error;
 Check(app.OpenProject(std::filesystem::u8path(argv[1]),Application::ProjectDecision::Discard,&error),error);auto& e=app.GetEditorLayer();e.SetFixedDiagnosticLayout(true);e.SetDiagnosticViewport(1280,720);e.SetLightGizmosVisible(true);e.SetGameView(false);e.SetGridVisible(false);Check(app.SetEditorCameraPose({0,1.7f,0},0,0),"camera pose");
 auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());auto frame=[&](){auto begin=Clock::now();app.SetFrameLimit(1);Check(app.Run()==0,"editor frame");return Ms(begin);};
 for(int i=0;i<24;++i)frame();
 if(argc>3&&std::string(argv[3])=="--qa"){
  auto spot=e.CreateLight(app,LightType::Spot);e.SetFixedDiagnosticLayout(false);e.SwitchWorkspace(editor::EditorWorkspace::LevelEditing);for(int i=0;i<4;++i)frame();
  const auto authored=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());auto picking=e.PickViewportMesh(app,{0,1.7f,0},{0,0,1},2000);Check(picking.IsValid(),"city picking");
  auto baseline=gpu->ReadBackViewportPixels();auto mesh=app.GetResourceManager()->Get<MeshResource>("models/TEST/street_city_7_for_games_free/street_city_7_for_games_free.glb");Check(bool(mesh),"city resource");mesh->ReplaceData(mesh->GetData(),ResourceLoadState::Loaded,false);for(int i=0;i<4;++i)frame();
  Check(gpu->ReadBackViewportPixels()==baseline,"resource version rebuild changes city image");Check(e.PickViewportMesh(app,{0,1.7f,0},{0,0,1},2000)==picking,"picking after occlusion cache rebuild");
  app.SetGpuInstancingEnabled(false);for(int i=0;i<3;++i)frame();Check(gpu->ReadBackViewportPixels()==baseline,"instancing switch image parity");app.SetGpuInstancingEnabled(true);
  app.SetEditorPlayMode(true);for(int i=0;i<3;++i)frame();Check(app.IsEditorPlayMode(),"Play with Spot");app.SetEditorPaused(true);frame();app.SetEditorPaused(false);frame();app.SetEditorPlayMode(false);for(int i=0;i<3;++i)frame();Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==authored,"Play Stop preserves authored scene");
  e.SelectEntity(spot);ImGui::FocusWindow(ImGui::FindWindowByName("Viewport"));SyntheticEditorInput::Key(ImGuiKey_F11,true);frame();SyntheticEditorInput::Key(ImGuiKey_F11,false);frame();Check(e.IsViewportMaximized(),"Spot F11");
  Check(gpu->SaveFrameImage(std::string(argv[2])+".ppm"),"QA screenshot");auto pixels=gpu->ReadBackViewportPixels();{std::ofstream image(std::string(argv[2])+"-scene.ppm",std::ios::binary);image<<"P6\n1280 720\n255\n";Check(pixels.size()==1280*720*4,"QA viewport size");for(std::size_t i=0;i<pixels.size();i+=4)image.write(reinterpret_cast<const char*>(pixels.data()+i),3);}
  Check(gpu->GetValidationErrorCount()==0,"QA DX12 validation");std::cout<<"City image parity, resource-version cache rebuild, picking, instancing, Play/Pause/Stop, F11 and DX12 validation passed\n";app.Shutdown();return 0;
 }
 nlohmann::json output;output["kind"]="native_editor";output["resolution"]={1280,720};output["camera"]={0,1.7,0};
 auto measure=[&](const char* name){for(int i=0;i<2;++i)frame();std::vector<double> total,cpu,device,shadow,main,cull,fog,present,fence,gizmo,ui,material;
  for(int i=0;i<8;++i){double wall=frame();auto waits=gpu->GetCpuWaitMs();auto times=gpu->GetGpuTimings();Check(times.valid,"GPU timestamps unavailable");total.push_back(wall);cpu.push_back(wall-waits[0]-waits[1]-waits[2]);device.push_back(times.frameMs);shadow.push_back(times.shadowMs);main.push_back(times.mainMs);cull.push_back(gpu->GetSceneLightDiagnostics().cullingGpuMs);fog.push_back(gpu->GetFogDiagnostics().gpuMs);present.push_back(waits[1]);fence.push_back(waits[2]);gizmo.push_back(e.GetStatistics().lightGizmoMs);ui.push_back(e.GetStatistics().uiMs);material.push_back(app.GetRenderStatistics().materialResolveMs);}
  auto stats=gpu->GetSubmissionStatistics();nlohmann::json row={{"cpuActiveMs",Median(cpu)},{"wallFrameMs",Median(total)},{"fps",1000/Median(total)},{"gpuFrameMs",Median(device)},{"gpuShadowMs",Median(shadow)},{"gpuMainMs",Median(main)},{"gpuCullMs",Median(cull)},{"gpuFogMs",Median(fog)},{"cpuPresentMs",Median(present)},{"cpuFenceWaitMs",Median(fence)},{"cpuLightGizmoMs",Median(gizmo)},{"cpuUiMs",Median(ui)},{"cpuMaterialMs",Median(material)},{"draws",stats.drawCalls},{"shadowDraws",stats.shadowDrawCalls},{"shadowResolution",stats.shadowResolution}};
  output["phases"][name]=row;std::cout<<name<<": "<<row.dump()<<std::endl;std::ofstream(argv[2])<<output.dump(2);Check(gpu->GetValidationErrorCount()==0,"editor DX12 validation");
 };
 e.ClearSelection();measure("no_spot");auto spot=e.CreateLight(app,LightType::Spot);measure("spot_no_shadows");app.GetWorld().GetComponent<ecs::SpotLightComponent>(spot)->castShadows=true;measure("spot_cast_flag");app.GetWorld().GetComponent<ecs::SpotLightComponent>(spot)->enabled=false;measure("disabled_selected");app.GetWorld().GetComponent<ecs::SpotLightComponent>(spot)->enabled=true;e.ClearSelection();measure("spot_unselected");e.SelectEntity(spot);e.SetLightGizmosVisible(false);measure("spot_gizmos_hidden_diagnostic");e.SetLightGizmosVisible(true);e.ClearSelection();app.GetWorld().GetComponent<ecs::SpotLightComponent>(spot)->enabled=false;measure("disabled_unselected_warm");e.SelectEntity(spot);app.GetWorld().GetComponent<ecs::SpotLightComponent>(spot)->enabled=true;frame();Check(gpu->SaveFrameImage(std::string(argv[2])+".ppm"),"editor capture");app.Shutdown();return 0;
}
int main(int argc,char** argv){try{
 if(argc==1)return OcclusionRegression();
 if(argc>1&&std::string(argv[1])=="--editor")return EditorMeasurement(argc-1,argv+1);
 Check(argc>=3,"Usage: SpotLightRegression project.whispproject output.json");
 ProjectManager::Activate(ProjectManager::Prepare(std::filesystem::u8path(argv[1])));
 ecs::World world;SceneSettings settings;std::string error;
 Check(SceneSerializer::LoadWorld(ProjectManager::Active()->ResolveScene("Scenes/Main.json"),world,settings,&error),error);
 GlfwWindow window;Check(window.Create(1280,720,"Spot Light targeted diagnostic"),"window creation");glfwHideWindow(window.GetGlfwHandle());
 Dx12RenderAdapter gpu;Check(gpu.Initialize(&window),"DX12 initialization");JobSystem jobs;Check(jobs.Initialize(),"jobs initialization");ResourceManager resources(&jobs);
 Check(resources.Load<ShaderResource>("dx12/textured.hlsl")->IsLoaded(),"main HLSL preload");
 ecs::RenderSystem render;render.SetResourceManager(&resources);render.SetRenderAdapter(&gpu);render.SetLightingSettings(&settings.lighting);render.SetShadowSettings(&settings.shadows);render.SetEnvironmentSettings(&settings.environment);render.SetAtmosphereSettings(&settings.background);
 render.SetInstancingEnabled(true);render.SetCameraTransform({0,1.7f,0},0,0);render.SetCameraProjection(1.04719755f,1280.f/720,.1f,2000);
 gpu.SetAntiAliasing(settings.antiAliasing);
 EditorVisualFrame visual;visual.active=true;visual.backgroundEnabled=true;visual.showGrid=false;visual.camera={0,1.7f,0};static_cast<SceneBackground&>(visual.settings)=settings.background;gpu.SetEditorVisuals(visual);
 // Load only assets referenced by this scene. Original project files are never saved.
 world.ForEach<ecs::MeshRendererComponent>([&](auto,const auto& component){auto asset=resources.Load<MeshResource>(component.meshPath);Check(asset->IsLoaded(),asset->GetErrorMessage());for(auto& material:asset->GetData().meshData.materials)for(auto* path:{&material.texturePath,&material.normalTexturePath,&material.metallicRoughnessTexturePath,&material.occlusionTexturePath})if(!path->empty())resources.Load<TextureResource>(*path);});
 struct Sample {double cpu=0,total=0,presentCall=0;RenderGpuTimings gpu;std::array<double,3> waits{};};
 auto frame=[&](){auto start=Clock::now();gpu.BeginFrame();gpu.Clear(.008f,.012f,.02f,1);render.Update(world,0);gpu.EndFrame();Sample result;result.cpu=Ms(start);start=Clock::now();gpu.Present();result.presentCall=Ms(start);result.total=result.cpu+result.presentCall;result.gpu=gpu.GetGpuTimings();result.waits=gpu.GetCpuWaitMs();return result;};
 for(int i=0;i<40;++i){resources.PollAsyncLoads();resources.PumpFinalization(8);frame();}Check(gpu.GetSubmissionStatistics().drawCalls>0,"city must actually be rendered");
 auto spot=world.CreateEntity();auto& transform=world.AddComponent<ecs::TransformComponent>(spot);transform.position={0,1.7f,3};world.AddComponent<ecs::SpotLightComponent>(spot).enabled=false;
 nlohmann::json output;output["resolution"]={1280,720};output["camera"]={0,1.7,0};output["nearFar"]={.1,2000};output["scene"]=AssetPaths::ToUtf8String(ProjectManager::Active()->ResolveScene("Scenes/Main.json"));
 auto measure=[&](const char* name){
  for(int i=0;i<4;++i)frame();std::vector<double> cpu,total,gpuMs,shadow,main,post,cull,present,fence,slot,shadowCpu,submitCpu,fog;bool valid=true;
  for(int i=0;i<16;++i){auto s=frame();cpu.push_back(s.cpu);total.push_back(s.total);valid&=s.gpu.valid;gpuMs.push_back(s.gpu.frameMs);shadow.push_back(s.gpu.shadowMs);main.push_back(s.gpu.mainMs);post.push_back(s.gpu.outputMs);cull.push_back(gpu.GetSceneLightDiagnostics().cullingGpuMs);fog.push_back(gpu.GetFogDiagnostics().gpuMs);slot.push_back(s.waits[0]);present.push_back(s.waits[1]);fence.push_back(s.waits[2]);shadowCpu.push_back(render.GetStatistics().shadowMs);submitCpu.push_back(render.GetStatistics().submitMs);}
  auto stats=gpu.GetSubmissionStatistics();nlohmann::json row={{"cpuRecordMs",Median(cpu)},{"wallFrameMs",Median(total)},{"fps",1000/Median(total)},{"gpuValid",valid},{"gpuFrameMs",Median(gpuMs)},{"gpuShadowMs",Median(shadow)},{"gpuMainMs",Median(main)},{"gpuOutputMs",Median(post)},{"gpuLightCullMs",Median(cull)},{"gpuFogMs",Median(fog)},{"cpuPresentMs",Median(present)},{"cpuFenceWaitMs",Median(fence)},{"cpuSlotWaitMs",Median(slot)},{"cpuShadowRecordMs",Median(shadowCpu)},{"cpuSubmitMs",Median(submitCpu)},{"draws",stats.drawCalls},{"shadowDraws",stats.shadowDrawCalls},{"shadowCasters",stats.shadowCasters},{"shadowResolution",stats.shadowResolution},{"optimized",gpu.GetSceneLightDiagnostics().optimized},{"lightIndices",gpu.GetSceneLightDiagnostics().indices},{"lightError",gpu.GetSceneLightDiagnostics().error}};
  output["phases"][name]=row;std::cout<<name<<": "<<row.dump()<<std::endl;std::ofstream(argv[2])<<output.dump(2);Check(gpu.GetValidationErrorCount()==0,"DX12 validation errors");
 };
 measure("disabled_entity");world.RemoveComponent<ecs::SpotLightComponent>(spot);measure("no_spot");auto& light=world.AddComponent<ecs::SpotLightComponent>(spot);light.enabled=true;light.castShadows=false;measure("spot_no_shadows");light.castShadows=true;measure("spot_cast_flag");render.SetLightRenderingMode(LightRenderingMode::Forward);measure("spot_forward_diagnostic");
 Check(gpu.SaveFrameImage(std::string(argv[2])+".ppm"),"scene capture");render.ReleaseGpuResources();gpu.Shutdown();ProjectManager::Activate({});return 0;
 }catch(const std::exception& e){std::cerr<<"SPOT DIAGNOSTIC FAILED: "<<e.what()<<'\n';return 1;}}
