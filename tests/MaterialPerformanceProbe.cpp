#define NOMINMAX
#include "resources/ResourceManager.h"
#include "resources/MaterialSerialization.h"
#include "ecs/systems/RenderSystem.h"
#include "ecs/PhysicsStressScene.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#include <Windows.h>
#include "renderdoc_app.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
using namespace ecs;
using Clock=std::chrono::steady_clock;
static double Ms(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
int main(int argc,char** argv){try{
 if(argc<4)throw std::runtime_error("MaterialPerformanceProbe materialCount instancing output.csv [capture]");
 const int count=std::stoi(argv[1]);const bool instancing=std::stoi(argv[2])!=0;
 if(count<1||count>500)throw std::runtime_error("count must be 1..500");
 const bool capture=argc>4;RENDERDOC_API_1_6_0* rd=nullptr;
 if(capture){auto module=GetModuleHandleA("renderdoc.dll");if(!module)throw std::runtime_error("RenderDoc injection missing");auto api=(pRENDERDOC_GetAPI)GetProcAddress(module,"RENDERDOC_GetAPI");if(!api||!api(eRENDERDOC_API_Version_1_6_0,(void**)&rd))throw std::runtime_error("RenderDoc API unavailable");}
 auto start=Clock::now();GlfwWindow window;if(!window.Create(1920,1080,"WhispEngine Material Performance"))throw std::runtime_error("window");
 Dx12RenderAdapter gpu;if(!gpu.Initialize(&window))throw std::runtime_error("DX12 initialize");
 ResourceManager resources(nullptr);RenderSystem render;render.SetResourceManager(&resources);render.SetRenderAdapter(&gpu);
 render.SetInstancingEnabled(instancing);render.SetCameraTransform({0,0,0},0,0);render.SetCameraProjection(1.04719755f,1920.f/1080,.1f,100);
 EnvironmentSettings environment;environment.toneMapping=false;render.SetEnvironmentSettings(&environment);
 ShadowSettings shadows;shadows.enabled=false;render.SetShadowSettings(&shadows);
 LightingSettings lighting;lighting.directional.enabled=false;lighting.ambientColor={1,1,1};lighting.ambientIntensity=1;render.SetLightingSettings(&lighting);
 resources.Load<MeshResource>("models/validation_cube.obj");resources.Load<ShaderResource>("dx12/textured.hlsl");resources.Load<TextureResource>("defaults/texture");
 std::vector<std::shared_ptr<Resource<MaterialResource>>> materials;
 for(int i=0;i<count;++i){auto key="materials/perf-"+std::to_string(i)+".json";auto m=resources.Load<MaterialResource>(key);if(!m->IsLoaded())throw std::runtime_error("benchmark material unavailable");materials.push_back(m);}
 World world;for(int i=0;i<500;++i){auto e=world.CreateEntity();auto& t=world.AddComponent<TransformComponent>(e);t.position={(i%25-12)*.65f,(i/25-9.5f)*.65f,20};t.scale={.5f,.5f,.5f};world.AddComponent<MeshRendererComponent>(e).meshPath="models/validation_cube.obj";auto& m=world.AddComponent<MaterialComponent>(e);m.materialPath=materials[i%count]->GetKey();}
 std::ofstream out(std::filesystem::u8path(argv[3]));out<<std::fixed<<std::setprecision(9);
 out<<"frame,phase,frameMs,cpuRenderMs,materialResolveMs,gatherMs,prepareMs,batchMs,submitMs,gpuFrameMs,gpuValid,width,height,objects,drawCalls,instances,materials,instancing,psoHits,psoMisses,psoCreations,psoCached,materialHits,materialMisses\n";
 std::cout<<"setupMs="<<Ms(start,Clock::now())<<" shaderCount=1 meshCount=1 editor=0 HDR=0 AO=0 shadows=0\n";
 for(int i=0;i<340;++i){
  if(i==300){auto data=materials[0]->GetData();data.roughness=.77f;materials[0]->ReplaceData(data,ResourceLoadState::Loaded);}
  if(i==310)resources.Reload<ShaderResource>("dx12/textured.hlsl");
  if(rd&&i==100)rd->TriggerCapture();
  auto a=Clock::now();window.PollEvents();gpu.BeginFrame();gpu.Clear(.03f,.04f,.05f,1);float clear[]={.03f,.04f,.05f,1};if(!gpu.BeginViewportRender(1920,1080,clear))throw std::runtime_error("viewport");auto size=gpu.RenderAreaSize();
  auto b=Clock::now();render.Update(world,0);auto c=Clock::now();gpu.EndViewportRender();gpu.EndFrame();gpu.Present();auto d=Clock::now();
  auto r=render.GetStatistics();auto g=gpu.GetSubmissionStatistics();auto p=gpu.GetPipelineCacheDiagnostics();auto timing=gpu.GetGpuTimings();
  if(size[0]!=1920||size[1]!=1080||(i>=30&&(r.visibleObjects!=500||g.renderedInstances!=500||g.drawCalls!=(instancing?1:500))))throw std::runtime_error("scene invariant failed frame="+std::to_string(i)+" objects="+std::to_string(r.visibleObjects)+" instances="+std::to_string(g.renderedInstances)+" draws="+std::to_string(g.drawCalls));
  out<<i<<','<<(i<30?"warmup":i<300?"measured":i<310?"parameter-edit":"shader-reload")<<','<<Ms(a,d)<<','<<Ms(b,c)<<','<<r.materialResolveMs<<','<<r.gatherMs<<','<<r.prepareMs<<','<<r.batchMs<<','<<r.submitMs<<','<<timing.frameMs<<','<<timing.valid<<','<<size[0]<<','<<size[1]<<','<<r.visibleObjects<<','<<g.drawCalls<<','<<g.renderedInstances<<','<<count<<','<<instancing<<','<<p.hits<<','<<p.misses<<','<<p.creations<<','<<p.cachedCount<<','<<r.materialCacheHits<<','<<r.materialCacheMisses<<'\n';
 }
 render.ReleaseGpuResources();render.SetRenderAdapter(nullptr);std::cout<<"DX12 validation errors="<<gpu.GetValidationErrorCount()<<'\n';if(gpu.GetValidationErrorCount())throw std::runtime_error("validation errors");resources.Shutdown();gpu.Shutdown();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
