#define NOMINMAX
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#include "ecs/systems/RenderSystem.h"
#include "ecs/components/MeshRendererComponent.h"
#include "ecs/components/MaterialComponent.h"
#include "ecs/components/LightComponent.h"
#include "ecs/components/FogComponent.h"
#include "ecs/systems/ParticleSystem.h"
#include "ui/RuntimeUI.h"
#include "scene/SceneSerializer.h"
#include "resources/ResourceManager.h"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <stdexcept>
using namespace ecs;
namespace {
unsigned checks=0;
void Check(bool b,const char* message){++checks;if(!b)throw std::runtime_error(message);}
struct Fixture {
    GlfwWindow window;Dx12RenderAdapter gpu;ResourceManager resources{nullptr};RenderSystem render;World world;
    LightingSettings lighting;ShadowSettings shadows;EnvironmentSettings environment;
    Entity floor,cube;unsigned width=640,height=360;std::filesystem::path output;double cpuRenderMs=0;bool hud=false,benchmarkViewport=false;RenderTextureHandle white;
    Fixture(std::filesystem::path path):output(path){
        std::filesystem::create_directories(output);Check(window.Create(width,height,"Ambient occlusion native"),"window");glfwHideWindow(window.GetGlfwHandle());
        auto mesh=resources.Load<MeshResource>("models/validation_cube.obj");Check(mesh->IsLoaded()&&!mesh->GetData().placeholder,"real mesh");
        auto material=resources.Load<MaterialResource>("materials/white.material.json");Check(material->IsLoaded()&&!material->GetData().placeholder,"real material");
        auto shader=resources.Load<ShaderResource>("dx12/textured.hlsl");Check(shader->IsLoaded()&&!shader->GetData().placeholder,"real shader");
        floor=Mesh({0,-.1f,0},{12,.2f,12});cube=Mesh({0,.5f,0},{1,1,1});
        lighting.directional.enabled=false;lighting.ambientIntensity=.6f;environment.toneMapping=false;shadows.enabled=false;
        Start();Camera();std::ofstream(output/"camera.json")<<nlohmann::json{{"projection",render.GetCameraState().projection}}.dump(2);
    }
    Entity Mesh(Vec3 p,Vec3 scale){auto e=world.CreateEntity();auto& t=world.AddComponent<TransformComponent>(e);t.position=p;t.scale=scale;world.AddComponent<MeshRendererComponent>(e).meshPath="models/validation_cube.obj";auto& m=world.AddComponent<MaterialComponent>(e);m.materialPath="materials/white.material.json";m.overrides.shadingModel=ShadingModel::PBR;m.overrides.metallic=0;m.overrides.roughness=.6f;for(float& c:m.tint)c=1;return e;}
    void Start(){Check(gpu.Initialize(&window),"DX12 initialize");render.SetRenderAdapter(&gpu);render.SetResourceManager(&resources);render.SetLightingSettings(&lighting);render.SetShadowSettings(&shadows);render.SetEnvironmentSettings(&environment);TextureData t;t.width=t.height=1;t.pixels={255,255,255,255};white=gpu.CreateTexture2D(t);}
    void Stop(){Check(gpu.GetValidationErrorCount()==0,"DX12 ERROR/CORRUPTION");render.ReleaseGpuResources();gpu.DestroyTexture(white);gpu.Shutdown();}
    void Hud(){if(!hud)return;ui::DrawList list;list.width=float(width);list.height=float(height);float x=width*.5f,y=height*.5f;for(auto xy:std::array<std::array<float,2>,6>{{{-8,-8},{8,-8},{8,8},{-8,-8},{8,8},{-8,8}}})list.vertices.push_back({x+xy[0],y+xy[1],.5f,.5f,{1,1,0,1}});list.commands.push_back({0,6,{0,0,float(width),float(height)},white});gpu.DrawRuntimeUI(list);}
    void Camera(bool ortho=false,float x=0){TransformComponent t;t.position={x,4,-7};t.rotation={.45f,0,0};CameraComponent c;c.projection=ortho?CameraProjection::Orthographic:CameraProjection::Perspective;c.orthographicSize=9;c.nearClip=.1f;c.farClip=50;render.SetCameraState(BuildCameraState(RenderPose::From(t),c,float(width)/height));}
    void Frame(){window.PollEvents();gpu.BeginFrame();gpu.Clear(.04f,.06f,.08f,1);if(benchmarkViewport){float clear[]={.04f,.06f,.08f,1};Check(gpu.BeginViewportRender(width,height,clear),"benchmark viewport");auto size=gpu.RenderAreaSize();Check(size[0]==width&&size[1]==height,"benchmark actual render resolution");}auto start=std::chrono::steady_clock::now();render.Update(world,1.f/60);cpuRenderMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();if(benchmarkViewport)gpu.EndViewportRender();gpu.EndFrame();gpu.Present();}
    std::vector<unsigned char> Capture(const std::string& name){for(int i=0;i<8;++i)Frame();window.PollEvents();gpu.BeginFrame();gpu.Clear(.04f,.06f,.08f,1);render.Update(world,1.f/60);Hud();gpu.EndFrame();auto image=gpu.ReadBackFramePixels();gpu.Present();Check(image.size()==size_t(width)*height*4,"GPU capture size");Check(gpu.GetValidationErrorCount()==0,"capture validation");std::ofstream f(output/(name+".ppm"),std::ios::binary);f<<"P6\n"<<width<<' '<<height<<"\n255\n";for(size_t i=0;i<image.size();i+=4)f.write(reinterpret_cast<char*>(image.data()+i),3);Check(f.good(),"capture save");return image;}
    std::vector<float> Read(unsigned input,const std::string& name){auto a=gpu.ReadBackAmbientOcclusion(input);Check(a.size()==size_t(width)*height*(input==1?4:1),"AO input size");for(float v:a)Check(std::isfinite(v),"finite AO/depth/normal");std::ofstream f(output/(name+".f32"),std::ios::binary);f.write(reinterpret_cast<const char*>(a.data()),a.size()*4);return a;}
};
int Difference(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b){Check(a.size()==b.size(),"image comparison size");int max=0;for(size_t p=0;p<a.size();++p)max=std::max(max,std::abs(int(a[p])-int(b[p])));return max;}
void Benchmark(Fixture& f){
    f.benchmarkViewport=true;
    std::ofstream csv(f.output/"benchmark.csv");csv<<"width,height,quality,cpu_frame_ms,cpu_render_ms,cpu_ao_prepare_ms,gpu_depth_ms,gpu_raw_ms,gpu_filter_ms,gpu_main_ms,gpu_frame_ms,ao_bytes,ao_depth_draws,draws\n";
    std::ofstream warmup(f.output/"warmup.csv");warmup<<"width,height,quality,warmup_frames,warmup_seconds\n";
    for(auto resolution:std::array<std::array<unsigned,2>,3>{{{640,360},{1280,720},{1920,1080}}}){
        f.width=resolution[0];f.height=resolution[1];f.Camera();
        for(int q:{-1,0,1,2}){auto& a=f.environment.ambientOcclusion;a.enabled=q>=0;a.quality=std::max(q,0);a.debug=0;
            for(int i=0;i<30;++i)f.Frame();auto warm=std::chrono::steady_clock::now();int count=0;double seconds=0;do{f.Frame();++count;seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-warm).count();}while(count<120||seconds<1);warmup<<f.width<<','<<f.height<<','<<q<<','<<count<<','<<seconds<<'\n';
            for(int i=0;i<200;++i){auto start=std::chrono::steady_clock::now();f.Frame();auto end=std::chrono::steady_clock::now();auto d=f.gpu.GetAmbientOcclusionDiagnostics();auto g=f.gpu.GetGpuTimings();Check(g.valid,"GPU timestamps");auto r=f.gpu.GetSubmissionStatistics();csv<<f.width<<','<<f.height<<','<<q<<','<<std::chrono::duration<double,std::milli>(end-start).count()<<','<<f.cpuRenderMs<<','<<d.cpuMs<<','<<(q>=0?d.depthMs:0)<<','<<(q>=0?d.rawMs:0)<<','<<(q>=0?d.filterMs:0)<<','<<g.mainMs<<','<<g.frameMs<<','<<d.gpuBytes<<','<<(q>=0?d.depthDraws:0)<<','<<r.drawCalls<<'\n';}
        }
    }
}
}
int main(int argc,char** argv){try{
    const bool benchmark=argc>1&&std::string(argv[1])=="--benchmark";
    Fixture f(argc>(benchmark?2:1)?argv[benchmark?2:1]:"ambient-occlusion-readback");
    if(benchmark){Benchmark(f);f.Stop();std::cout<<"AO benchmark PASS\n";return 0;}
    auto& ao=f.environment.ambientOcclusion;
    Check(!SceneSettingsFromJson(nlohmann::json::object()).environment.ambientOcclusion.enabled,"legacy scenes AO off");
    auto off=f.Capture("off");ao.enabled=true;auto on=f.Capture("final");Check(Difference(off,on)>3,"contact AO missing");Check(f.gpu.GetSubmissionStatistics().instancedDrawCalls>0,"AO shader lost main-pass instancing");
    auto depth=f.Read(0,"depth"),raw=f.Read(1,"raw-normal"),filtered=f.Read(2,"filtered");
    float minimum=1;unsigned occupied=0;for(size_t p=0;p<filtered.size();++p){Check(filtered[p]>=0&&filtered[p]<=1,"AO factor range");if(depth[p]<1){++occupied;minimum=std::min(minimum,filtered[p]);float n2=0;for(int k=1;k<4;++k)n2+=raw[p*4+k]*raw[p*4+k];Check(std::abs(n2-1)<.003f,"geometric normal unit length");}}
    Check(occupied>100&&minimum<.98f,"occlusion/depth geometry input");std::cout<<"contact minimum="<<minimum<<" occupied="<<occupied<<'\n';
    unsigned oraclePixels=0;int oracleMaxError=0;for(size_t p=0;p<filtered.size();++p)if(depth[p]<1&&off[p*4]==203&&off[p*4+1]==203&&off[p*4+2]==203){float linear=.6f*filtered[p];int expected=int(std::lround(255*(linear<=.0031308f?12.92f*linear:1.055f*std::pow(linear,1/2.4f)-.055f)));oracleMaxError=std::max(oracleMaxError,std::abs(int(on[p*4])-expected));++oraclePixels;}
    Check(oraclePixels>100&&oracleMaxError<=1,"ambient AO linear-light GPU oracle");std::cout<<"ambient oracle pixels="<<oraclePixels<<" max_error_lsb="<<oracleMaxError<<'\n';
    ao.debug=1;auto rawImage=f.Capture("raw");ao.debug=2;auto filteredImage=f.Capture("filtered");
    for(size_t p=0;p<filtered.size();++p){Check(std::abs(int(rawImage[p*4])-int(std::lround(255*raw[p*4])))<=1,"raw AO debug oracle");Check(std::abs(int(filteredImage[p*4])-int(std::lround(255*filtered[p])))<=1,"filtered AO debug oracle");}
    f.environment.toneMapping=true;f.environment.exposure=2;auto hdrDebug=f.Capture("debug-hdr-exposure");Check(hdrDebug==filteredImage,"AO debug altered by HDR output settings");f.environment.toneMapping=false;f.environment.exposure=0;ao.debug=0;
    ao.intensity=0;auto zero=f.Capture("intensity-zero");Check(Difference(off,zero)<=1,"zero intensity legacy equivalence");ao.intensity=1;
    f.world.GetComponent<MeshRendererComponent>(f.cube)->visible=false;f.Capture("flat-plane");auto flat=f.Read(2,"flat");auto flatDepth=f.Read(0,"flat-depth");float error=0;for(size_t p=0;p<flat.size();++p)if(flatDepth[p]<1)error=std::max(error,1-flat[p]);Check(error<.01f,"flat plane self occlusion");f.world.GetComponent<MeshRendererComponent>(f.cube)->visible=true;
    for(float radius:{.2f,.75f,2.f}){ao.radius=radius;f.Capture("radius-"+std::to_string(radius));}ao.radius=.75f;
    for(int q=0;q<3;++q){ao.quality=q;f.Capture("quality-"+std::to_string(q));}
    ao.quality=1;ao.intensity=2;auto strong=f.Capture("intensity-two");Check(Difference(on,strong)>1,"intensity does not affect AO");ao.intensity=1;
    auto wall=f.Mesh({-1,1,0},{.15f,2,4});f.Capture("room-corner");
    auto thin=f.Mesh({.9f,.5f,0},{.04f,1,2});f.Capture("thin-geometry");
    auto parallel=f.Mesh({-1.5f,1,0},{.15f,2,4});f.Capture("parallel-surfaces");
    for(auto e:{wall,thin,parallel})f.world.DestroyEntity(e);
    auto sphere=f.resources.Load<MeshResource>("models/primitives/sphere.obj");Check(sphere->IsLoaded()&&!sphere->GetData().placeholder,"sphere asset");
    auto ball=f.Mesh({1.3f,.5f,0},{1,1,1});f.world.GetComponent<MeshRendererComponent>(ball)->meshPath="models/primitives/sphere.obj";f.Capture("sphere-contact");f.world.DestroyEntity(ball);
    f.Camera(true);f.Capture("orthographic");auto ortho=f.gpu.ReadBackAmbientOcclusion(2);Check(!ortho.empty()&&*std::min_element(ortho.begin(),ortho.end())<.98f,"orthographic AO");
    f.Camera(false,.1f);f.Capture("camera-moved");f.Camera();
    auto still1=f.Capture("camera-still-a"),still2=f.Capture("camera-still-b");Check(still1==still2,"static AO frame instability");
    f.lighting.ambientIntensity=0;
    auto isolate=[&](const std::string& name){ao.enabled=false;auto a=f.Capture(name+"-off");ao.enabled=true;auto b=f.Capture(name+"-on");int error=Difference(a,b),peak=0;for(size_t p=0;p<a.size();p+=4)peak=std::max(peak,int(a[p]));Check(peak>30,"isolated light contributes no visible radiance");Check(error<=1,"AO modifies isolated direct lighting");std::cout<<"isolation "<<name<<" max_error_lsb="<<error<<" peak="<<peak<<'\n';};
    f.shadows.enabled=true;f.lighting.directional.enabled=true;f.lighting.directional.intensity=1;isolate("directional");f.lighting.directional.enabled=false;
    auto light=f.world.CreateEntity();auto& t=f.world.AddComponent<TransformComponent>(light);t.position={0,3,-2};auto& point=f.world.AddComponent<PointLightComponent>(light);point.intensity=20;point.range=15;isolate("point");f.world.DestroyEntity(light);
    light=f.world.CreateEntity();f.world.AddComponent<TransformComponent>(light).position={0,3,-2};f.world.GetComponent<TransformComponent>(light)->rotation.x=.8f;auto& spot=f.world.AddComponent<SpotLightComponent>(light);spot.intensity=20;spot.range=15;spot.innerAngle=30;spot.outerAngle=50;spot.castShadows=true;isolate("spot");Check(f.gpu.GetSpotShadowDiagnostics().active==1,"AO disrupts Spot shadow pass");f.world.DestroyEntity(light);f.shadows.enabled=false;
    f.lighting.ambientIntensity=.6f;f.environment.toneMapping=true;f.Capture("hdr");
    auto env=f.resources.Load<EnvironmentResource>("environments/studio.hdr");Check(env->IsLoaded()&&!env->GetData().placeholder,"IBL environment asset");f.environment.enabled=true;ao.enabled=false;auto iblOff=f.Capture("ibl-off");auto hdrOff=f.gpu.ReadBackHdrPixels();ao.enabled=true;auto iblOn=f.Capture("ibl-on");auto hdrOn=f.gpu.ReadBackHdrPixels();Check(f.gpu.GetEnvironmentDiagnostics().generation>0,"IBL upload");Check(Difference(iblOff,iblOn)>1,"AO does not affect indirect IBL");Check(hdrOff.size()==hdrOn.size()&&hdrOn.size()==size_t(f.width)*f.height*4,"linear HDR IBL readback");auto iblAo=f.gpu.ReadBackAmbientOcclusion(2);double iblError=0;for(size_t p=0;p<depth.size();++p)if(depth[p]<1)for(int c=0;c<3;++c)iblError=std::max(iblError,std::abs(double(hdrOn[p*4+c])-double(hdrOff[p*4+c])*iblAo[p]));Check(iblError<.00002,"IBL linear-HDR AO multiplication oracle");std::cout<<"IBL HDR oracle max_error="<<iblError<<'\n';
    f.world.GetComponent<MaterialComponent>(f.cube)->overrides.metallic=1;f.Capture("metallic");f.world.GetComponent<MaterialComponent>(f.cube)->overrides.metallic=0;f.environment.enabled=false;
    ao.enabled=false;f.environment.toneMapping=false;auto offAgain=f.Capture("off-restored");Check(offAgain==off,"AO disabled changes previous output");ao.enabled=true;
    f.hud=true;auto hud=f.Capture("runtime-ui");size_t center=(size_t(f.height/2)*f.width+f.width/2)*4;Check(hud[center]==255&&hud[center+1]==255&&hud[center+2]==0,"AO affects runtime UI");f.hud=false;
    f.world.GetComponent<MeshRendererComponent>(f.floor)->visible=false;f.world.GetComponent<MeshRendererComponent>(f.cube)->renderMode=MeshRenderMode::FirstPerson;ao.enabled=false;auto fpOff=f.Capture("viewmodel-off");ao.enabled=true;auto fpOn=f.Capture("viewmodel-on");Check(fpOff==fpOn,"AO affects first-person viewmodel");auto fpDepth=f.gpu.ReadBackAmbientOcclusion(0);Check(std::all_of(fpDepth.begin(),fpDepth.end(),[](float d){return d==1;}),"viewmodel pollutes AO depth");
    f.world.GetComponent<MeshRendererComponent>(f.cube)->visible=false;
    auto texture=f.resources.Load<TextureResource>("textures/validation_checker.png");Check(texture->IsLoaded()&&!texture->GetData().placeholder,"particle texture");ParticleSystem particles;particles.Bind(f.world);std::vector<Entity> emitters;
    for(auto blend:{ParticleBlend::Alpha,ParticleBlend::Additive}){auto e=f.world.CreateEntity();f.world.AddComponent<TransformComponent>(e).position={blend==ParticleBlend::Alpha?-.5f:.5f,1.5f,0};auto& p=f.world.AddComponent<ParticleEmitterComponent>(e);p.texture="textures/validation_checker.png";p.burstCount=1;p.speedMin=p.speedMax=0;p.lifetimeMin=p.lifetimeMax=10;p.startSize=p.endSize=1;p.blendMode=blend;emitters.push_back(e);}
    particles.Update(f.world,1.f/60,f.render.GetCameraState());f.render.SetParticles(&particles);ao.enabled=false;auto particlesOff=f.Capture("particles-off");ao.enabled=true;auto particlesOn=f.Capture("particles-on");Check(particlesOff==particlesOn,"AO modifies isolated alpha/additive particles");Check(f.gpu.GetParticleStatistics().drawCalls==2,"particle draws missing");
    f.render.SetParticles(nullptr);for(auto e:emitters)f.world.DestroyEntity(e);f.world.GetComponent<MeshRendererComponent>(f.floor)->visible=true;f.world.GetComponent<MeshRendererComponent>(f.cube)->visible=true;f.world.GetComponent<MeshRendererComponent>(f.cube)->renderMode=MeshRenderMode::World;
    auto fog=f.world.CreateEntity();f.world.AddComponent<TransformComponent>(fog);auto& fogSettings=f.world.AddComponent<FogComponent>(fog);fogSettings.startDistance=0;fogSettings.endDistance=30;f.Capture("fog");f.world.DestroyEntity(fog);
    SceneSettings settings;settings.environment=f.environment;auto restored=SceneSettingsFromJson(SceneSettingsToJson(settings));Check(restored.environment.ambientOcclusion==ao,"AO settings save/reload");
    glfwSetWindowSize(f.window.GetGlfwHandle(),800,450);f.width=800;f.height=450;f.Camera();f.Capture("resize");f.Stop();f.Start();f.Camera();f.Capture("device-restart");f.Stop();
    std::cout<<"Ambient Occlusion: "<<checks<<" checks PASS validation=0\n";return 0;
}catch(const std::exception& e){std::cerr<<"AO FAILED: "<<e.what()<<'\n';return 1;}}
