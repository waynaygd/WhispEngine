#define NOMINMAX
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#include "ecs/systems/RenderSystem.h"
#include "ecs/components/MeshRendererComponent.h"
#include "ecs/components/MaterialComponent.h"
#include "ecs/components/LightComponent.h"
#include "ecs/components/FogComponent.h"
#include "ecs/systems/ParticleSystem.h"
#include "ecs/ComponentRegistry.h"
#include "ecs/LightGather.h"
#include "scene/SceneSerializer.h"
#include "scene/LightSerialization.h"
#include "resources/ResourceManager.h"
#include "ui/RuntimeUI.h"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
using namespace ecs;
namespace {
unsigned checks=0;
void Check(bool b,const char* message){++checks;if(!b)throw std::runtime_error(message);}
int Encode(double x){x=std::clamp(x,0.,1.);return int(std::lround(255*(x<=.0031308?12.92*x:1.055*std::pow(x,1/2.4)-.055)));}
struct Fixture {
    GlfwWindow window;Dx12RenderAdapter gpu;ResourceManager resources{nullptr};RenderSystem render;World world;
    LightingSettings lighting;ShadowSettings shadows;EnvironmentSettings environment;
    Entity floor,caster;std::vector<Entity> spots;unsigned width=640,height=480;std::filesystem::path output;
    RenderTextureHandle white;bool hud=false;std::ofstream samples,timings;
    Fixture(std::filesystem::path path):output(path){
        std::filesystem::create_directories(output);Check(window.Create(width,height,"Spot shadows native"),"window");glfwHideWindow(window.GetGlfwHandle());
        for(auto key:{"models/validation_cube.obj"}){auto r=resources.Load<MeshResource>(key);Check(r->IsLoaded()&&!r->GetData().placeholder,"mesh asset");}
        auto material=resources.Load<MaterialResource>("materials/white.material.json");Check(material->IsLoaded()&&!material->GetData().placeholder,"material asset");
        auto shader=resources.Load<ShaderResource>("dx12/textured.hlsl");Check(shader->IsLoaded()&&!shader->GetData().placeholder,"shader asset");
        auto particleTexture=resources.Load<TextureResource>("textures/validation_checker.png");Check(particleTexture->IsLoaded()&&!particleTexture->GetData().placeholder,"real particle texture asset");
        floor=Mesh({0,-.1f,0},{14,.2f,12});caster=Mesh({0,1.5f,0},{1,1,1});
        lighting.directional.enabled=false;lighting.ambientIntensity=.08f;environment.toneMapping=false;
        Start();SetSpots(1);Camera();
        samples.open(output/"samples.csv");samples<<"case,probe_r,probe_g,probe_b,active,requested,over_budget,spot_draws,map_bytes\n";
        timings.open(output/"frames.csv");timings<<"spots,shadows,cpu_light_gather_ms,cpu_shadow_prepare_ms,cpu_shadow_record_ms,gpu_shadow_ms,gpu_main_ms,gpu_frame_ms,spot_draws,map_bytes\n";
    }
    Entity Mesh(Vec3 position,Vec3 scale){auto e=world.CreateEntity();auto& t=world.AddComponent<TransformComponent>(e);t.position=position;t.scale=scale;world.AddComponent<MeshRendererComponent>(e).meshPath="models/validation_cube.obj";auto& m=world.AddComponent<MaterialComponent>(e);m.materialPath="materials/white.material.json";m.overrides.shadingModel=ShadingModel::PBR;m.overrides.metallic=0;m.overrides.roughness=.65f;for(float& c:m.tint)c=1;return e;}
    void Start(){Check(gpu.Initialize(&window),"DX12 init");render.SetRenderAdapter(&gpu);render.SetResourceManager(&resources);render.SetLightingSettings(&lighting);render.SetShadowSettings(&shadows);render.SetEnvironmentSettings(&environment);TextureData t;t.width=t.height=1;t.pixels={255,255,255,255};white=gpu.CreateTexture2D(t);}
    void Stop(){Check(gpu.GetValidationErrorCount()==0,"DX12 ERROR/CORRUPTION");render.ReleaseGpuResources();gpu.DestroyTexture(white);gpu.Shutdown();}
    void Camera(){TransformComponent t;t.position={0,12,0};t.rotation={1.57079632679f,0,0};CameraComponent c;c.projection=CameraProjection::Orthographic;c.orthographicSize=12;c.nearClip=.1f;c.farClip=50;render.SetCameraState(BuildCameraState(RenderPose::From(t),c,float(width)/height));}
    void SetSpots(unsigned count){for(auto e:spots)world.DestroyEntity(e);spots.clear();for(unsigned i=0;i<count;++i){auto e=world.CreateEntity();auto& t=world.AddComponent<TransformComponent>(e);t.position={-3,6,0};t.rotation={1.107148718f,1.57079632679f,0};auto& l=world.AddComponent<SpotLightComponent>(e);l.castShadows=true;l.intensity=100.f/count;l.range=20;l.innerAngle=25;l.outerAngle=35;l.shadowResolution=1024;spots.push_back(e);}}
    size_t At(Vec3 p){const auto& m=render.GetCameraState().viewProjection;float c[4];for(int r=0;r<4;++r)c[r]=m[12+r]+m[r]*p.x+m[4+r]*p.y+m[8+r]*p.z;int x=int((c[0]/c[3]*.5f+.5f)*width),y=int((.5f-c[1]/c[3]*.5f)*height);Check(x>=0&&y>=0&&x<int(width)&&y<int(height),"probe in viewport");return (size_t(y)*width+x)*4;}
    std::vector<std::uint8_t> Frame(bool read){window.PollEvents();gpu.BeginFrame();gpu.Clear(.01f,.01f,.01f,1);render.Update(world,1.f/60);
        if(hud){ui::DrawList list;list.width=float(width);list.height=float(height);float x=width*.5f,y=height*.5f;for(auto xy:std::array<std::array<float,2>,6>{{{-8,-8},{8,-8},{8,8},{-8,-8},{8,8},{-8,8}}})list.vertices.push_back({x+xy[0],y+xy[1],.5f,.5f,{1,1,0,1}});list.commands.push_back({0,6,{0,0,float(width),float(height)},white});gpu.DrawRuntimeUI(list);}
        gpu.EndFrame();auto result=read?gpu.ReadBackFramePixels():std::vector<std::uint8_t>{};gpu.Present();return result;
    }
    std::vector<std::uint8_t> Capture(const char* name){for(int i=0;i<5;++i)Frame(false);auto image=Frame(true);Check(image.size()==size_t(width)*height*4,"GPU image size");Check(gpu.GetValidationErrorCount()==0,"capture validation");auto at=At({1,0,0});auto d=gpu.GetSpotShadowDiagnostics();samples<<name<<','<<int(image[at])<<','<<int(image[at+1])<<','<<int(image[at+2])<<','<<d.active<<','<<d.requested<<','<<d.overBudget<<','<<d.drawCalls<<','<<d.mapBytes<<'\n';samples.flush();
        std::ofstream out(output/(std::string(name)+".ppm"),std::ios::binary);out<<"P6\n"<<width<<' '<<height<<"\n255\n";for(size_t i=0;i<image.size();i+=4)out.write(reinterpret_cast<char*>(image.data()+i),3);Check(out.good(),"save GPU image");return image;}
};
void Benchmark(Fixture& f){
    std::ofstream warmup(f.output/"warmup.csv");warmup<<"spots,shadows,frames,seconds\n";
    std::ofstream settings(f.output/"benchmark-settings.txt");settings<<"debug_env_present="<<(std::getenv("WHISP_DX12_DEBUG")!=nullptr)<<"\nsamples_per_series=200\nwidth="<<f.width<<"\nheight="<<f.height<<"\n";
    for(unsigned count:{1u,2u,4u,5u}){f.SetSpots(count);
        for(bool enabled:{true,false}){
            for(auto e:f.spots)f.world.GetComponent<SpotLightComponent>(e)->castShadows=enabled;
            // Resource finalization/PSO compilation can occur after the first frame.
            // Exclude it before starting the time-based GPU warmup interval.
            for(int i=0;i<30;++i)f.Frame(false);
            Check(f.gpu.GetSubmissionStatistics().drawCalls>0,"benchmark scene finalized");
            auto start=std::chrono::steady_clock::now();unsigned frames=0;double seconds=0;
            do{f.Frame(false);++frames;seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();}while(frames<120||seconds<1);
            warmup<<count<<','<<enabled<<','<<frames<<','<<seconds<<'\n';
            for(int i=0;i<200;++i){f.Frame(false);auto s=f.render.GetStatistics();auto g=f.gpu.GetGpuTimings();auto spot=f.gpu.GetSpotShadowDiagnostics();Check(g.valid,"GPU timestamps");Check(spot.active==(enabled?std::min(count,4u):0),"benchmark shadow budget");
                f.timings<<count<<','<<enabled<<','<<f.gpu.GetSceneLightDiagnostics().gatherMs<<','<<s.shadowPrepareMs<<','<<s.shadowMs<<','<<g.shadowMs<<','<<g.mainMs<<','<<g.frameMs<<','<<spot.drawCalls<<','<<spot.mapBytes<<'\n';
            }
        }
    }
}
void Math(){
    for(float angle:{.1f,1.f,35.f,89.f})for(float range:{.01f,1.f,20.f,10000.f}){LightParameters p;p.outerAngle=angle;p.range=range;GpuSceneLight l;l.directionType={0,0,1,2};auto f=BuildSpotShadowFrame(0,l,p);Check(f.has_value(),"finite projection");for(float v:f->shadow.constants.lightVP)Check(std::isfinite(v),"finite matrix entry");float nearPlane=std::clamp(range*.001f,.001f,.05f);const auto& m=f->shadow.projection;auto z=[&](float distance){return (m[10]*distance+m[14])/distance;};Check(std::abs(z(nearPlane))<.0001&&std::abs(z(range)-1)<.0001,"DX perspective near/far convention");Check(std::abs(m[0]*std::tan(angle*.0174532925199433f)-1)<.0001,"outer cone projection");}
    auto old=LightFromJson({{"type","spot"}});Check(!old.parameters.castShadows,"legacy default remains unshadowed");old.parameters.castShadows=true;old.parameters.shadowResolution=512;old.parameters.shadowBias=.003f;old.parameters.shadowStrength=.6f;old.parameters.shadowPcf=false;Check(LightFromJson(LightToJson(old)).parameters==old.parameters,"light settings roundtrip");
    GpuSceneLight invalid;invalid.directionType={NAN,0,1,2};Check(!BuildSpotShadowFrame(0,invalid,{}),"NaN projection rejected");
    World world;for(int i=0;i<5;++i){auto e=world.CreateEntity();world.AddComponent<TransformComponent>(e).position={0,0,5};auto& light=world.AddComponent<SpotLightComponent>(e);light.castShadows=true;light.intensity=float(i+1);light.range=20;}
    LightCamera camera;auto gathered=GatherSceneLights(world,LightingSettings{},camera);Check(gathered.spotShadowRequests==5&&gathered.spotShadows.size()==4,"registry fixed shadow budget");for(unsigned i=0;i<4;++i)Check(gathered.spotShadows[i].lightIndex==4-i,"deterministic importance selection");
    world.ForEach<TransformComponent>([](auto,auto& t){t.rotation.z=.5f;});auto rolled=GatherSceneLights(world,LightingSettings{},camera);Check(std::abs(rolled.spotShadows[0].shadow.view[0]-std::cos(.5f))<1e-5&&std::abs(rolled.spotShadows[0].shadow.view[4]-std::sin(.5f))<1e-5,"light Transform roll determines shadow basis");
    GpuSceneLight axis;axis.directionType={0,0,1,2};auto frame=BuildSpotShadowFrame(0,axis,LightParameters{});Check(frame&&SpotCasterIntersects(*frame,{{-.1f,-.1f,2},{.1f,.1f,3}}),"caster in cone");Check(!SpotCasterIntersects(*frame,{{10,10,2},{11,11,3}})&&!SpotCasterIntersects(*frame,{{0,0,-3},{1,1,-2}}),"caster outside cone/depth clipping");
}
}
int main(int argc,char** argv){try{
    const bool benchmark=argc>1&&std::string(argv[1])=="--benchmark";
    Math();Fixture f(benchmark?(argc>2?argv[2]:"spot-shadow-benchmark"):(argc>1?argv[1]:"spot-shadow-readback"));if(benchmark){Benchmark(f);f.Stop();std::cout<<"Spot benchmark PASS\n";return 0;}
    auto& first=*f.world.GetComponent<SpotLightComponent>(f.spots[0]);first.castShadows=false;auto off=f.Capture("spot-off");first.castShadows=true;auto on=f.Capture("spot-on");auto probe=f.At({1,0,0});int ambient=Encode(.08);std::cout<<"probe off="<<int(off[probe])<<" on="<<int(on[probe])<<" ambient="<<ambient<<'\n';Check(off[probe]>on[probe]+25,"spot caster fails to shadow receiver");Check(std::abs(int(on[probe])-ambient)<=2,"spot shadow changes ambient/IBL-independent contribution");
    auto outside=f.At({6,0,5});Check(std::abs(int(off[outside])-int(on[outside]))<=1,"receiver outside cone changed");
    auto depth=f.gpu.ReadBackSpotShadowDepth(0);Check(depth.size()==1024*1024,"D32 readback size");size_t occupied=0;for(float d:depth){Check(std::isfinite(d)&&d>=0&&d<=1,"D32 readback finite");occupied+=d<1;}Check(occupied>100,"Spot depth is empty");std::ofstream raw(f.output/"depth.f32",std::ios::binary);raw.write(reinterpret_cast<const char*>(depth.data()),depth.size()*4);
    f.world.GetComponent<MaterialComponent>(f.caster)->overrides.castShadows=false;auto noCast=f.Capture("cast-disabled");Check(std::abs(int(noCast[probe])-int(off[probe]))<=1,"mesh cast flag ignored");int selfShadowError=0;for(size_t p=0;p<off.size();++p)selfShadowError=std::max(selfShadowError,std::abs(int(noCast[p])-int(off[p])));Check(selfShadowError<=1,"self-shadow acne with no occluding caster");f.world.GetComponent<MaterialComponent>(f.caster)->overrides.castShadows=true;
    first.shadowStrength=0;auto zeroStrength=f.Capture("strength-zero");Check(zeroStrength==off,"zero shadow strength changes old lighting");first.shadowStrength=.5f;auto halfStrength=f.Capture("strength-half");double encoded=off[probe]/255.;double linear=encoded<=.04045?encoded/12.92:std::pow((encoded+.055)/1.055,2.4);Check(std::abs(int(halfStrength[probe])-Encode(.08+(linear-.08)*.5))<=2,"shadow strength must mix direct radiance in linear space");first.shadowStrength=1;
    f.world.GetComponent<MaterialComponent>(f.floor)->overrides.receiveShadows=false;auto noReceive=f.Capture("receive-disabled");Check(std::abs(int(noReceive[probe])-int(off[probe]))<=1,"mesh receive flag ignored");f.world.GetComponent<MaterialComponent>(f.floor)->overrides.receiveShadows=true;
    f.world.GetComponent<TransformComponent>(f.caster)->position={6,1.5f,5};auto moved=f.Capture("caster-outside-cone");Check(std::abs(int(moved[probe])-int(off[probe]))<=1,"outside caster leaves stale shadow");f.world.GetComponent<TransformComponent>(f.caster)->position={0,1.5f,0};
    f.world.GetComponent<TransformComponent>(f.spots[0])->position.x=3;f.world.GetComponent<TransformComponent>(f.spots[0])->rotation.y=-1.57079632679f;auto lightMoved=f.Capture("light-moved");Check(lightMoved[probe]>on[probe]+20,"moving spot leaves stale shadow");f.world.GetComponent<TransformComponent>(f.spots[0])->position.x=-3;f.world.GetComponent<TransformComponent>(f.spots[0])->rotation.y=1.57079632679f;
    first.range=2;auto clipped=f.Capture("range-depth-clipped");Check(std::abs(int(clipped[probe])-ambient)<=2,"out-of-range receiver lighting");auto clippedDepth=f.gpu.ReadBackSpotShadowDepth(0);Check(!clippedDepth.empty()&&std::all_of(clippedDepth.begin(),clippedDepth.end(),[](float d){return d==1.f;}),"far-clipped casters must leave a clear GPU depth map");first.range=20;
    for(int resolution:{512,1024,2048}){first.shadowResolution=resolution;auto image=f.Capture(("resolution-"+std::to_string(resolution)).c_str());Check(std::abs(int(image[probe])-ambient)<=2,"resolution loses shadow");Check(f.gpu.ReadBackSpotShadowDepth(0).size()==size_t(resolution)*resolution,"resolution depth size");}first.shadowResolution=1024;
    first.shadowPcf=false;auto hard=f.Capture("hard");first.shadowPcf=true;auto soft=f.Capture("pcf");int fractional=0;for(size_t p=0;p<soft.size();p+=4)fractional+=soft[p]>hard[p]+2&&soft[p]+2<off[p];Check(fractional>3,"PCF fractional edge missing");
    for(float bias:{0.f,.002f,.02f}){first.shadowBias=bias;auto image=f.Capture(("bias-"+std::to_string(bias)).c_str());auto lit=f.At({-2,0,-2});Check(std::abs(int(image[lit])-int(off[lit]))<=2,"bias introduces lit plane acne");Check(std::abs(int(image[probe])-ambient)<=2,"bias loses shadow core");}first.shadowBias=.002f;
    for(float angle:{.1f,10.f,60.f,89.f}){first.outerAngle=angle;first.innerAngle=std::min(5.f,angle);f.Capture(("cone-"+std::to_string(angle)).c_str());}first.outerAngle=35;first.innerAngle=25;
    f.lighting.directional.enabled=true;f.lighting.directional.intensity=.4f;first.castShadows=false;auto directionalOnly=f.Capture("directional-spot-unshadowed");first.castShadows=true;auto combined=f.Capture("directional-and-spot");
    Check(f.gpu.GetSubmissionStatistics().shadowDrawCalls>f.gpu.GetSpotShadowDiagnostics().drawCalls,"directional shadow pass missing");Check(directionalOnly[probe]>combined[probe]+20,"Spot visibility missing alongside directional shadow");auto sunProbe=f.At({-.7f,0,.5f});Check(std::abs(int(directionalOnly[sunProbe])-int(combined[sunProbe]))<=1,"Spot pass changed directional shadow visibility");f.lighting.directional.enabled=false;
    f.world.GetComponent<MaterialComponent>(f.floor)->overrides.metallic=1;first.castShadows=false;auto metalOff=f.Capture("metal-unshadowed");first.castShadows=true;auto metalOn=f.Capture("metal-shadowed");Check(metalOff[probe]>metalOn[probe]+10&&metalOn[probe]<=2,"Spot shadow must attenuate metal specular direct contribution");f.world.GetComponent<MaterialComponent>(f.floor)->overrides.metallic=0;
    f.environment.toneMapping=true;auto hdr=f.Capture("hdr-tone");Check(std::abs(int(hdr[probe])-Encode(.08/1.08))<=2,"HDR tonemapping ambient oracle");f.environment.toneMapping=false;
    for(float gap:{0.f,.02f}){
        f.world.GetComponent<TransformComponent>(f.caster)->position.y=.5f+gap;
        auto contact=f.Capture(("contact-gap-"+std::to_string(gap)).c_str());auto contactAt=f.At({.65f,0,0});Check(std::abs(int(contact[contactAt])-ambient)<=2,"Spot contact shadow detaches");
    }
    f.world.GetComponent<TransformComponent>(f.spots[0])->position={-6,2,0};f.world.GetComponent<TransformComponent>(f.spots[0])->rotation={.32175055f,1.57079632679f,0};
    auto grazing=f.Capture("grazing-contact");Check(std::abs(int(grazing[probe])-ambient)<=2,"grazing Spot contact shadow");
    f.world.GetComponent<TransformComponent>(f.spots[0])->position={-3,6,0};f.world.GetComponent<TransformComponent>(f.spots[0])->rotation={1.107148718f,1.57079632679f,0};f.world.GetComponent<TransformComponent>(f.caster)->position.y=1.5f;
    auto fogEntity=f.world.CreateEntity();f.world.AddComponent<TransformComponent>(fogEntity);auto& fog=f.world.AddComponent<FogComponent>(fogEntity);fog.color={0,0,0,1};fog.startDistance=0;fog.endDistance=30;
    auto fogImage=f.Capture("fog-and-spot");Check(fogImage[probe]+5<on[probe],"Fog must compose after Spot shadows");
    ParticleSystem particles;std::vector<Entity> emitters;
    for(auto blend:{ParticleBlend::Alpha,ParticleBlend::Additive}){auto e=f.world.CreateEntity();f.world.AddComponent<TransformComponent>(e).position={blend==ParticleBlend::Alpha?-2.f:2.f,1,-2};auto& p=f.world.AddComponent<ParticleEmitterComponent>(e);p.texture="textures/validation_checker.png";p.burstCount=1;p.speedMin=p.speedMax=0;p.lifetimeMin=p.lifetimeMax=10;p.startSize=p.endSize=.4f;emitters.push_back(e);p.blendMode=blend;}
    particles.Bind(f.world);particles.Update(f.world,1.f/60,f.render.GetCameraState());f.render.SetParticles(&particles);f.Capture("fog-alpha-additive-spot");Check(f.gpu.GetParticleStatistics().drawCalls>=2,"Spot/Fog particle integration draws missing");
    f.render.SetParticles(nullptr);for(auto e:emitters)f.world.DestroyEntity(e);f.world.DestroyEntity(fogEntity);
    f.world.GetComponent<MeshRendererComponent>(f.caster)->renderMode=MeshRenderMode::FirstPerson;auto fp=f.Capture("first-person-caster");Check(std::abs(int(fp[probe])-int(off[probe]))<=1,"FirstPerson casts world Spot shadow");f.world.GetComponent<MeshRendererComponent>(f.caster)->renderMode=MeshRenderMode::World;
    f.hud=true;auto ui=f.Capture("hud");auto center=(size_t(f.height/2)*f.width+f.width/2)*4;Check(ui[center]==255&&ui[center+1]==255&&ui[center+2]==0,"HUD output changed");f.hud=false;
    f.SetSpots(2);f.world.GetComponent<SpotLightComponent>(f.spots[0])->color={1,0,0};auto& blue=*f.world.GetComponent<SpotLightComponent>(f.spots[1]);blue.color={0,0,1};f.world.GetComponent<TransformComponent>(f.spots[1])->position.x=3;f.world.GetComponent<TransformComponent>(f.spots[1])->rotation.y=-1.57079632679f;
    auto asymmetric=f.Mesh({-1,1,2},{.6f,2,.6f});auto independent=f.Capture("two-distinct-spots");auto left=f.At({-1,0,0});Check(std::abs(int(independent[probe])-ambient)<=2&&independent[probe+2]>ambient+20,"red shadow must not shadow blue Spot");Check(independent[left]>ambient+20&&std::abs(int(independent[left+2])-ambient)<=2,"blue shadow must not shadow red Spot");Check(f.gpu.ReadBackSpotShadowDepth(0)!=f.gpu.ReadBackSpotShadowDepth(1),"distinct Spot maps alias");f.world.DestroyEntity(asymmetric);
    for(unsigned count:{1u,2u,4u,5u}){f.SetSpots(count);auto image=f.Capture(("budget-"+std::to_string(count)).c_str());auto d=f.gpu.GetSpotShadowDiagnostics();Check(d.active==std::min(count,4u)&&d.overBudget==(count>4?1u:0u),"shadow budget");if(count<=4)Check(std::abs(int(image[probe])-ambient)<=2,"multiple spot visibility");else Check(image[probe]>ambient+15,"unallocated light must remain unshadowed");
    }
    Benchmark(f);
    f.SetSpots(1);auto scene=SceneSerializer::CaptureWorld(f.world,SceneSettings{});World restored;SceneSettings restoredSettings;std::string error;Check(SceneSerializer::RestoreWorld(scene,restored,restoredSettings,&error),"scene serialization restore");
    glfwSetWindowSize(f.window.GetGlfwHandle(),800,600);f.width=800;f.height=600;f.Camera();f.Capture("resize");f.Stop();f.Start();f.Camera();auto restart=f.Capture("restart");Check(f.gpu.GetSpotShadowDiagnostics().active==1,"restart shadow slot");f.Stop();
    std::cout<<"Spot shadows: "<<checks<<" checks PASS validation=0\n";return 0;
}catch(const std::exception& e){std::cerr<<"Spot shadow FAILED: "<<e.what()<<'\n';return 1;}}
