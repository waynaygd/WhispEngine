#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "core/Application.h"
#include "ecs/FogGather.h"
#include "ecs/PlayerSpawn.h"
#include "ecs/components/ColliderComponent.h"
#include "scene/SceneSerializer.h"
#include "editor/LightGizmoMath.h"
#include "render/ColorPipeline.h"
#include <fstream>
#include <iostream>
#if defined(ENABLE_DX12)
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#endif
using namespace ecs;
static int checks=0;
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);++checks;}
static void Near(double a,double b,double tolerance,const char* message){Check(std::abs(a-b)<=tolerance,message);}
static void Cpu(){
    FogSettings settings;Check(ValidFog(settings),"defaults valid");Near(DistanceFogAmount(settings,0),0,0,"near clear");Near(DistanceFogAmount(settings,55),.5,1e-6,"linear world distance");Near(DistanceFogAmount(settings,200),1,0,"far opaque");
    settings.startDistance=settings.endDistance;Check(!ValidFog(settings),"equal distance invalid");settings={};settings.heightDensity=NAN;Check(!ValidFog(settings),"NaN invalid");settings={};settings.anisotropy=1;Check(!ValidFog(settings),"singular phase invalid");
    for(auto ys:std::array<std::array<double,2>,7>{{{-5,-1},{1,5},{-3,7},{7,-3},{3,3},{3,3.000001},{0,0}}}){
        double sum=0;for(int i=0;i<20000;++i){double y=ys[0]+(ys[1]-ys[0])*(i+.5)/20000;sum+=std::exp(-.3*std::max(y,0.))*20/20000;}
        Near(HeightFogIntegral(ys[0],ys[1],20,0,.3),sum,1e-6,"analytic height integral matches numerical quadrature");
    }
    Near(HeightFogIntegral(100,200,50,0,0),50,0,"zero falloff homogeneous");Check(HeightFogIntegral(-100,100,10000,0,10)<=10000,"extreme ray integral bounded");
    Application app;app.NewScene();auto& ui=app.GetEditorLayer();auto& world=app.GetWorld();auto e=ui.CreateFog(app);Check(world.HasComponent<FogComponent>(e)&&!world.HasComponent<ColliderComponent>(e),"Fog creation without collider");auto original=*world.GetComponent<FogComponent>(e);auto id=ComponentId("Fog");
    Check(ui.SetProperty(app,e,id,"endDistance",70.f),"Fog property");ui.UndoEdit(app);Check(*world.GetComponent<FogComponent>(e)==original,"Fog Undo");ui.RedoEdit(app);Near(world.GetComponent<FogComponent>(e)->endDistance,70,0,"Fog Redo");Check(!ui.SetProperty(app,e,id,"startDistance",80.f),"cross field rejection");Near(world.GetComponent<FogComponent>(e)->startDistance,10,0,"failed setter atomic");
    auto second=ui.CreateFog(app);world.GetComponent<FogComponent>(second)->color={1,0,0,1};auto gathered=GatherFog(world);Check(gathered.activeCount==2&&gathered.settings.color==original.color,"deterministic lowest index fog");world.GetComponent<FogComponent>(e)->enabled=false;Check(GatherFog(world).settings.color[0]==1,"disabled primary allows next");world.GetComponent<FogComponent>(e)->enabled=true;
    auto json=SceneSerializer::CaptureWorld(world,app.GetSceneSettings());World loaded;SceneSettings restored;std::string error;Check(SceneSerializer::RestoreWorld(json,loaded,restored,&error),"scene roundtrip");Check(SceneSerializer::CaptureWorld(loaded,restored)==json,"Fog parameters survive Save/Load");
    auto invalid=json;for(auto& item:invalid["entities"])if(item["components"].contains("Fog"))item["components"]["Fog"]["endDistance"]=0;Check(!SceneSerializer::RestoreWorld(invalid,loaded,restored,&error),"invalid serialized fog rejected");
    app.SetEditorPlayMode(true);Check(GatherFog(app.GetWorld()).activeCount==2,"runtime fog cloned");for(auto entity:app.GetWorld().Entities())if(auto* c=app.GetWorld().GetComponent<FogComponent>(entity))c->density=0;app.SetEditorPlayMode(false);Check(SceneSerializer::CaptureWorld(world,app.GetSceneSettings())==json,"Stop retains authored Fog");
    for(double g:{0.,.7,-.7}){double sum=0;for(int i=0;i<100000;++i)sum+=FogPhase(-1+(i+.5)*2/100000,g)*2/100000*6.28318530718;Near(sum,1,1e-6,"HG phase normalized");}Check(FogPhase(1,.7)>FogPhase(-1,.7),"anisotropy changes scattering direction");
    Check(FogSamples(FogQuality::Low)<FogSamples(FogQuality::Medium)&&FogSamples(FogQuality::Medium)<FogSamples(FogQuality::High),"quality changes integration work");
    auto& configured=*world.GetComponent<FogComponent>(e);configured.extinction=.123f;configured.anisotropy=-.4f;configured.maxDistance=321;configured.heightFalloff=.7f;configured.baseHeight=-3;configured.scatteringColor={.2f,.4f,.6f,1};configured.quality=FogQuality::High;configured.sceneLighting=false;
    for(auto mode:{FogMode::Distance,FogMode::Height,FogMode::Volumetric}){configured.mode=mode;auto document=SceneSerializer::CaptureWorld(world,app.GetSceneSettings());World copy;Check(SceneSerializer::RestoreWorld(document,copy,restored,&error),"all mode scene restore");Check(SceneSerializer::CaptureWorld(copy,restored)==document,"all nondefault fog parameters roundtrip");}
    configured.extinction=INFINITY;Check(GatherFog(world).invalidCount==1,"corrupt direct ECS data rejected at render boundary");
    World empty;Check(!GatherFog(empty).active,"no Fog is inactive");
}
#if defined(ENABLE_DX12)
static void Image(const std::string& name,const std::vector<std::uint8_t>& pixels,unsigned w,unsigned h){
    std::filesystem::create_directories("fog-captures");std::ofstream out("fog-captures/"+name+".ppm",std::ios::binary);out<<"P6\n"<<w<<' '<<h<<"\n255\n";for(std::size_t i=0;i<pixels.size();i+=4)out.write(reinterpret_cast<const char*>(pixels.data()+i),3);Check(out.good(),"diagnostic image saved");
}
static void Gpu(){
    Application app;Check(app.Initialize(),"DX12 init");SyntheticEditorInput input;glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());app.NewScene();auto& ui=app.GetEditorLayer();ui.SetFixedDiagnosticLayout(true);ui.SetDiagnosticViewport(400,300);ui.SetGridVisible(false);ui.SetLightGizmosVisible(false);ui.SetGameView(true);ui.SetBackgroundOverride(false);
    auto scene=app.GetSceneSettings();scene.environment.enabled=false;scene.shadows.enabled=false;scene.antiAliasing=AntiAliasingMode::Off;Check(app.SetSceneSettings(scene),"diagnostic scene settings");
    auto sun=ui.CreateLight(app,LightType::Directional);app.GetWorld().GetComponent<DirectionalLightComponent>(sun)->primary=true;
    std::array<Vec3,3> positions{{{-3,1,6},{0,3,18},{4,6,32}}};
    for(auto p:positions){auto e=ui.CreatePrimitive(app,"cube");auto& t=*app.GetWorld().GetComponent<TransformComponent>(e);t.position=p;t.scale={2,2,2};}
    auto floor=ui.CreatePrimitive(app,"plane");app.GetWorld().GetComponent<TransformComponent>(floor)->scale={100,1,100};auto fog=ui.CreateFog(app);auto& f=*app.GetWorld().GetComponent<FogComponent>(fog);f.enabled=false;f.startDistance=10;f.endDistance=50;f.color={0,0,0,1};ui.ClearSelection();app.SetEditorCameraPose({0,3,-8},0,0);app.SetViewportUnlit(true);
    auto frame=[&](unsigned n=6){app.SetFrameLimit(n);Check(app.Run()==0,"native frames");};frame(100);auto* renderer=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());auto baseline=renderer->ReadBackViewportPixels();Check(baseline.size()==400*300*4,"viewport dimensions");Image("disabled",baseline,400,300);
    auto pixel=[&](Vec3 p){auto camera=editor::GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),400.f/300,app.GetCameraNearPlane(),app.GetCameraFarPlane());auto projected=editor::ProjectGizmo(p,camera);Check(bool(projected),"object projects");return (std::size_t((*projected)[1]*300)*400+unsigned((*projected)[0]*400))*4;};
    std::array<std::size_t,3> pixels;for(int i=0;i<3;++i){pixels[i]=pixel(positions[i]);Check(baseline[pixels[i]]>100,"cube visible in baseline");}
    f.enabled=true;frame();auto distance=renderer->ReadBackViewportPixels();Check(renderer->GetFogDiagnostics().active,"Distance pass active");Check(renderer->GetFogDiagnostics().error.empty(),"Distance shader/pipeline healthy");Image("distance",distance,400,300);
    double previous=2;for(auto at:pixels){double ratio=SrgbToLinear(distance[at]/255.f)/SrgbToLinear(baseline[at]/255.f);Check(ratio<previous&&ratio>0,"GPU distance fog strengthens with world distance");previous=ratio;}
    f.enabled=false;frame();Check(renderer->ReadBackViewportPixels()==baseline,"Disabled restores baseline exactly");
    f.enabled=true;f.color={.8f,.1f,.02f,1};frame();Check(renderer->ReadBackViewportPixels()!=distance,"Fog color updates without restart");
    f.color={0,0,0,1};f.mode=FogMode::Height;f.baseHeight=2;f.heightDensity=.1f;f.heightFalloff=1;frame();auto height=renderer->ReadBackViewportPixels();Image("height",height,400,300);
    double nearTrans=SrgbToLinear(height[pixels[0]]/255.f)/SrgbToLinear(baseline[pixels[0]]/255.f),farTrans=SrgbToLinear(height[pixels[2]]/255.f)/SrgbToLinear(baseline[pixels[2]]/255.f);Check(nearTrans<farTrans-.05,"Height is spatial: low near object foggier than elevated far object");Check(height!=distance,"Height differs from Distance");
    for(float pitch:{-.7f,.7f,0.f}){app.SetEditorCameraPose({0,3,-8},0,pitch);frame();auto hdr=renderer->ReadBackHdrPixels();Check(!hdr.empty(),"Height HDR readback");for(float value:hdr)if(!std::isfinite(value))throw std::runtime_error("Height nonfinite ray output");}app.SetEditorCameraPose({0,3,-8},0,0);frame();
    f.mode=FogMode::Volumetric;f.heightFalloff=0;f.extinction=0;f.scatteringColor={0,0,0,1};frame();auto clearHdr=renderer->ReadBackHdrPixels();auto zeroVolume=renderer->ReadBackViewportPixels();Image("volume-zero",zeroVolume,400,300);unsigned maximum=0,changed=0;for(std::size_t i=0;i<baseline.size();++i){maximum=std::max(maximum,unsigned(std::abs(int(zeroVolume[i])-int(baseline[i]))));changed+=zeroVolume[i]!=baseline[i];}std::cout<<"Zero extinction max byte error "<<maximum<<" changed "<<changed<<"\n";Check(maximum<=1,"zero extinction preserves Scene Color within UNORM rounding");
    f.extinction=.07f;f.density=0;frame();auto noIntensity=renderer->ReadBackHdrPixels();Check(noIntensity==clearHdr,"zero global intensity disables volume medium");f.density=1;frame();auto absorption=renderer->ReadBackHdrPixels();for(int i=0;i<3;++i){auto p=positions[i];double z=p.z+8,L=std::sqrt(p.x*p.x+(p.y-3)*(p.y-3)+z*z)*(z-1)/z;Near(absorption[pixels[i]]/clearHdr[pixels[i]],std::exp(-f.extinction*L),.02,"GPU Beer-Lambert transmittance in world units");}
    Image("volumetric-absorption",renderer->ReadBackViewportPixels(),400,300);
    f.scatteringColor={.8f,.85f,.9f,1};f.heightFalloff=.25f;f.extinction=.04f;f.baseHeight=2;f.maxDistance=70;f.anisotropy=.3f;app.SetViewportUnlit(false);scene.lighting.ambientIntensity=.02f;Check(app.SetSceneSettings(scene),"lit fog settings");auto* sunlight=app.GetWorld().GetComponent<DirectionalLightComponent>(sun);sunlight->intensity=5;sunlight->color={1,.8f,.6f};app.GetWorld().GetComponent<TransformComponent>(sun)->rotation={0,3.14159265f,0};frame();auto lit=renderer->ReadBackHdrPixels();Image("volumetric",renderer->ReadBackViewportPixels(),400,300);
    const std::size_t skyPixel=(30*400+350)*4;sunlight->intensity=0;frame();auto dark=renderer->ReadBackHdrPixels();Check(lit[skyPixel]>dark[skyPixel]+.005f,"volume receives Directional light at empty sky ray");Image("volumetric-no-sun",renderer->ReadBackViewportPixels(),400,300);sunlight->intensity=5;
    f.anisotropy=-.5f;frame();auto backward=renderer->ReadBackHdrPixels();f.anisotropy=.7f;frame();Check(std::abs(renderer->ReadBackHdrPixels()[skyPixel]-backward[skyPixel])>.005,"HG anisotropy changes angular scattering");f.anisotropy=.3f;
    f.extinction=.01f;frame();auto thin=renderer->ReadBackHdrPixels();f.extinction=.12f;frame();Check(renderer->ReadBackHdrPixels()!=thin,"extinction changes volume integration");f.extinction=.04f;
    f.sceneLighting=false;f.color={.2f,.4f,.6f,1};frame();auto constant=renderer->ReadBackHdrPixels();sunlight->intensity=0;frame();Near(renderer->ReadBackHdrPixels()[skyPixel],constant[skyPixel],1e-6,"constant radiance option independent of sun");f.sceneLighting=true;sunlight->intensity=5;frame();auto primaryOnly=renderer->ReadBackHdrPixels();
    auto point=ui.CreateLight(app,LightType::Point);auto* pl=app.GetWorld().GetComponent<PointLightComponent>(point);pl->intensity=80;pl->range=25;pl->color={1,0,0};app.GetWorld().GetComponent<TransformComponent>(point)->position={0,6,2};ui.ClearSelection();frame();auto pointHdr=renderer->ReadBackHdrPixels();Check(pointHdr[skyPixel]>primaryOnly[skyPixel]+1e-5,"Point light scatters inside volume along empty sky ray");pl->intensity=0;frame();
    auto spot=ui.CreateLight(app,LightType::Spot);auto* sl=app.GetWorld().GetComponent<SpotLightComponent>(spot);sl->intensity=100;sl->range=40;sl->innerAngle=25;sl->outerAngle=55;sl->color={0,0,1};auto* st=app.GetWorld().GetComponent<TransformComponent>(spot);st->position={0,5,-2};st->rotation={-.5f,0,0};ui.ClearSelection();frame();Check(renderer->ReadBackHdrPixels()[skyPixel+2]>primaryOnly[skyPixel+2]+1e-5,"Spot cone contributes to volume");
    pl=app.GetWorld().GetComponent<PointLightComponent>(point);pl->intensity=80;sunlight=app.GetWorld().GetComponent<DirectionalLightComponent>(sun);sunlight->castShadows=true;frame();auto unshadowedVolume=renderer->ReadBackHdrPixels();scene.shadows.enabled=true;scene.shadows.coverage=100;scene.shadows.depthRange=200;Check(app.SetSceneSettings(scene),"volume directional shadow map enabled");frame(12);Image("volumetric-shadowed",renderer->ReadBackViewportPixels(),400,300);Check(renderer->GetFogDiagnostics().active&&renderer->GetFogDiagnostics().error.empty(),"shadowed volume pipeline healthy");auto shadowedVolume=renderer->ReadBackHdrPixels();unsigned shadowedSky=0;for(unsigned y=0;y<150;++y)for(unsigned x=0;x<400;++x){auto at=(y*400+x)*4;if(baseline[at]<200&&unshadowedVolume[at]-shadowedVolume[at]>.002f)++shadowedSky;}Check(shadowedSky>10,"shadow map attenuates volumetric scattering on empty sky rays");
    std::ofstream timings("fog-captures/timings.csv");timings<<"quality,steps,width,height,cpu_ms,gpu_ms,extra_bytes\n";
    for(auto size:std::array<std::array<unsigned,2>,2>{{{400,300},{1280,720}}}){
        ui.SetDiagnosticViewport(size[0],size[1]);
        for(auto quality:{FogQuality::Low,FogQuality::Medium,FogQuality::High}){
            f.quality=quality;frame(12);double cpu=0,gpu=0;
            for(int i=0;i<8;++i){frame(1);auto stats=renderer->GetFogDiagnostics();cpu+=stats.cpuMs;gpu+=stats.gpuMs;Check(stats.samples==FogSamples(quality),"quality reaches actual GPU pass");}
            auto stats=renderer->GetFogDiagnostics();timings<<int(quality)<<','<<stats.samples<<','<<size[0]<<','<<size[1]<<','<<cpu/8<<','<<gpu/8<<','<<stats.gpuBytes<<'\n';
            std::cout<<"Fog quality "<<int(quality)<<" at "<<size[0]<<'x'<<size[1]<<": CPU "<<cpu/8<<" GPU "<<gpu/8<<" ms\n";
            if(size[0]==400)Image("volumetric-quality-"+std::to_string(int(quality)),renderer->ReadBackViewportPixels(),size[0],size[1]);
        }
    }
    ui.SetDiagnosticViewport(400,300);
    f.quality=FogQuality::Medium;frame();auto stable=renderer->ReadBackHdrPixels();frame();Check(renderer->ReadBackHdrPixels()==stable,"stationary volume stable without temporal jitter");for(float yaw:{.2f,-.2f,0.f}){app.SetEditorCameraPose({0,3,-8},yaw,0);frame();auto hdr=renderer->ReadBackHdrPixels();for(float value:hdr)if(!std::isfinite(value))throw std::runtime_error("moving volume output nonfinite");}
    auto beforePlay=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());app.SetEditorPlayMode(true);frame();Check(renderer->GetFogDiagnostics().active&&GatherFog(app.GetWorld()).active,"volume in Play");app.SetEditorPaused(true);frame();app.SetEditorPlayMode(false);frame();Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==beforePlay,"Play Pause Stop retain authored fog");
    ui.SetDiagnosticViewport(512,288);glfwSetWindowSize(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle(),1100,700);frame(12);Check(renderer->ReadBackViewportPixels().size()==512*288*4,"resized volumetric target");ui.SetDiagnosticViewport(400,300);frame(12);Check(renderer->ReadBackViewportPixels().size()==400*300*4,"volume size restored");
    scene.antiAliasing=AntiAliasingMode::FxaaHigh;Check(app.SetSceneSettings(scene),"volume plus FXAA");frame();Check(renderer->GetAntiAliasingDiagnostics().mode==AntiAliasingMode::FxaaHigh&&renderer->GetFogDiagnostics().active,"FXAA output preserved after fog");
    std::filesystem::create_directories("fog-captures");std::string error;Check(SceneSerializer::WriteAtomic("fog-captures/fog-diagnostic.scene.json",SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings()),&error),"diagnostic scene saved");
    World loaded;SceneSettings loadedSettings;Check(SceneSerializer::LoadWorld("fog-captures/fog-diagnostic.scene.json",loaded,loadedSettings,&error),"disk scene load");Check(SceneSerializer::CaptureWorld(loaded,loadedSettings)==SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings()),"disk fog roundtrip");
    Check(ui.DeleteEntity(app,fog),"delete Fog");frame();Check(!renderer->GetFogDiagnostics().active,"destroyed Fog disables effect immediately");ui.UndoEdit(app);frame();Check(renderer->GetFogDiagnostics().active,"Undo Fog deletion restores render");app.NewScene();frame();Check(!renderer->GetFogDiagnostics().active,"new scene clears Fog state");
    Check(renderer->GetValidationErrorCount()==0,"DX12 validation zero");
}
#endif
// Optional local diagnostic. Only an explicitly copied fixture may be opened:
// loaders may write model caches, so never point this at the user's project.
#if defined(ENABLE_DX12)
static void ProjectDiagnostic(const std::filesystem::path& descriptor){
    Check(std::filesystem::exists(descriptor.parent_path()/"stability-fixture"),"Project diagnostic requires an owned copy marker");
    Application app;Check(app.Initialize(Application::StartupMode::ProjectBrowser),"Project diagnostic init");SyntheticEditorInput input;auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle();glfwHideWindow(window);std::string error;Check(app.OpenProject(descriptor,Application::ProjectDecision::Discard,&error),error.c_str());
    auto& editor=app.GetEditorLayer();editor.SetFixedDiagnosticLayout(true);editor.SetDiagnosticViewport(400,300);editor.SetGameView(true);editor.SetGridVisible(false);editor.SetBackgroundOverride(false);app.SetEditorCameraPose({0,3,-6},0,0);
    auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());auto frame=[&](unsigned n=6){app.SetFrameLimit(n);Check(app.Run()==0,"Project diagnostic native frames");};frame(100);
    Entity fogEntity;for(auto e:app.GetWorld().Entities())if(app.GetWorld().HasComponent<FogComponent>(e))fogEntity=e;Check(fogEntity.IsValid(),"copied Project has Fog");
    const auto original=*app.GetWorld().GetComponent<FogComponent>(fogEntity);const auto settings=app.GetSceneSettings();std::ofstream csv("fog-captures/projectA-comparison.csv");csv<<"scenario,mean_hdr,cpu_ms,gpu_ms,width,height\n";
    auto measure=[&](const char* name){frame(12);double cpu=0,gpuTime=0;for(int i=0;i<8;++i){frame(1);auto stats=gpu->GetFogDiagnostics();cpu+=stats.cpuMs;gpuTime+=stats.gpuMs;}auto pixels=gpu->ReadBackViewportPixels();Image(std::string("projectA-")+name,pixels,editor.GetViewportPixelWidth(),editor.GetViewportPixelHeight());auto hdr=gpu->ReadBackHdrPixels();double total=0;for(std::size_t i=0;i<hdr.size();++i)if(i%4!=3){if(!std::isfinite(hdr[i])||hdr[i]<0)throw std::runtime_error("Project Fog nonfinite/negative radiance");total+=hdr[i];}const double mean=total/(hdr.size()*3/4);csv<<name<<','<<mean<<','<<cpu/8<<','<<gpuTime/8<<','<<editor.GetViewportPixelWidth()<<','<<editor.GetViewportPixelHeight()<<'\n';return mean;};
    auto& fog=*app.GetWorld().GetComponent<FogComponent>(fogEntity);fog.enabled=false;const double off=measure("off");fog.enabled=true;fog.mode=FogMode::Distance;measure("distance");fog.mode=FogMode::Height;measure("height");fog.mode=FogMode::Volumetric;measure("volume-authored");fog.extinction=.04f;measure("volume-control");
    const auto beforePreview=gpu->ReadBackHdrPixels();const auto sceneDocument=editor.ActiveDocument();
    Check(editor.OpenDocument(app,"materials/new.material.json"),"copied Project Material Preview opens");frame(12);Check(editor.ActivateDocument(app,sceneDocument),"return from Material Preview to Fog scene");frame(12);Check(gpu->ReadBackHdrPixels()==beforePreview,"Material Preview does not change Fog/lighting pixel output");
    app.SetViewportUnlit(true);measure("unlit");app.SetViewportUnlit(false);
    std::vector<std::pair<Entity,float>> suns,spots;app.GetWorld().ForEach<DirectionalLightComponent>([&](Entity e,auto& light){suns.emplace_back(e,light.intensity);light.intensity=0;});measure("no-directional");for(auto [e,intensity]:suns)app.GetWorld().GetComponent<DirectionalLightComponent>(e)->intensity=intensity;
    app.GetWorld().ForEach<SpotLightComponent>([&](Entity e,auto& light){spots.emplace_back(e,light.intensity);light.intensity=0;});measure("no-spot");for(auto [e,intensity]:spots)app.GetWorld().GetComponent<SpotLightComponent>(e)->intensity=intensity;
    auto noPost=settings;noPost.antiAliasing=AntiAliasingMode::Off;noPost.environment.toneMapping=false;Check(app.SetSceneSettings(noPost),"Project output controls");measure("no-post");Check(app.SetSceneSettings(settings),"restore Project output controls");
    // Keep the diagnostic camera fixed instead of comparing the editor view to
    // the user's different spawn point/controller camera. Only the copy changes.
    app.GetWorld().ForEach<PlayerStartComponent,TransformComponent>([](Entity,auto& start,auto& t){t.position={0,3-PlayerEyeHeight(start),-6};t.rotation={};});
    app.SetEditorPlayMode(true);app.GetLuaRuntime().Stop();measure("play");app.SetEditorPaused(true);measure("pause");app.SetEditorPlayMode(false);measure("edit-return");
    editor.SetDiagnosticViewport(0,0);editor.SetGameView(false);ImGui::SetWindowFocus("Viewport");SyntheticEditorInput::Key(ImGuiKey_F11,true);frame(1);SyntheticEditorInput::Key(ImGuiKey_F11,false);frame();measure("f11");
    Check(off>0,"Project baseline has visible radiance");Check(gpu->GetValidationErrorCount()==0,"Project comparison DX12 validation zero");app.Shutdown();
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore prefs;try{
#if defined(ENABLE_DX12)
    if(argc>2&&std::string(argv[1])=="--project")ProjectDiagnostic(std::filesystem::u8path(argv[2]));else if(argc>1&&std::string(argv[1])=="--dx12")Gpu();else
#endif
    Cpu();std::cout<<"Fog: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
