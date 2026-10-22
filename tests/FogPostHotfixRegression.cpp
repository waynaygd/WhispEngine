#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "core/Application.h"
#include "ecs/components/FogComponent.h"
#include "ecs/components/ColliderComponent.h"
#include "ecs/components/PlayerStartComponent.h"
#include "ecs/components/MeshRendererComponent.h"
#include "scene/SceneSerializer.h"
#include "editor/LightGizmoMath.h"
#include "editor/EditorTheme.h"
#include <fstream>
#include <iostream>
#include <chrono>
#if defined(ENABLE_DX12)
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#include <imgui_internal.h>
#include <ImGuizmo.h>
#endif
using namespace ecs;
static unsigned checks=0;
static void Check(bool v,const char* message){if(!v)throw std::runtime_error(message);++checks;}
static void Cpu(){
    Application app;app.NewScene();auto& ui=app.GetEditorLayer();ui.MarkSaved();auto history=ui.UndoCount();auto e=ui.CreateFog(app);
    Check(app.GetWorld().HasComponent<FogComponent>(e)&&ui.GetSelectedEntity()==e,"Create Fog selects entity");Check(ui.IsSceneDirty()&&ui.UndoCount()==history+1,"Create Fog one dirty transaction");ui.UndoEdit(app);Check(!app.GetWorld().IsAlive(e)&&!ui.IsSceneDirty(),"Undo Fog creation");ui.RedoEdit(app);Check(app.GetWorld().HasComponent<FogComponent>(ui.GetSelectedEntity()),"Redo Fog creation");
    auto count=app.GetWorld().GetAliveCount();app.SetEditorPlayMode(true);Check(!ui.CreateFog(app).IsValid()&&app.GetWorld().GetAliveCount()==count,"Play blocks authored Fog creation");app.SetEditorPlayMode(false);
    // Independent fine distance quadrature checks angular importance sampling
    // against the unchanged inverse-square attenuation (including near radius).
    for(double impact:{.02,.1,.5,2.,8.}){
        const double center=5,limit=20,radius=12,nearRadius=.12,sigma=.04,g=.3;
        auto integrand=[&](double s){double r2=impact*impact+(s-center)*(s-center),r=std::sqrt(r2);if(r>=radius)return 0.;double w=1-std::pow(r/radius,4);return sigma*std::exp(-sigma*s)*w*w*FogPhase((center-s)/r,g)/std::max(r2,nearRadius*nearRadius);};
        double reference=0;for(int i=0;i<200000;++i)reference+=integrand((i+.5)*limit/200000)*limit/200000;
        for(int samples:{8,16,32}){
            double extent=std::sqrt(radius*radius-impact*impact),a=std::max(0.,center-extent),b=std::min(limit,center+extent),h=std::max(impact,nearRadius),t0=std::atan((a-center)/h),dt=(std::atan((b-center)/h)-t0)/samples,sum=0;
            for(int i=0;i<samples;++i){double s=center+h*std::tan(t0+(i+.5)*dt);sum+=integrand(s)*(h*h+(s-center)*(s-center))*dt/h;}
            Check(std::abs(sum-reference)<reference*.035,"importance quadrature resolves near-light peak");
        }
    }
}
#if defined(ENABLE_DX12)
static void Ui(){
    Application app;Check(app.Initialize(),"UI init");SyntheticEditorInput input;auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle();glfwHideWindow(window);SyntheticEditorInput::Queue([](ImGuiIO& io){io.AddFocusEvent(true);});app.NewScene();auto& ui=app.GetEditorLayer();ui.SetGameView(false);ui.SetGridVisible(false);
    // Reserve the fixture's 100/150/200% glyphs in one atlas. The current DX12
    // backend reserves one ImGui descriptor; atlas growth is outside this layout test.
    ImGui::GetIO().Fonts->TexMinWidth=1024;ImGui::GetIO().Fonts->TexMinHeight=1024;
    std::vector<std::string> imguiErrors;
    auto* context=ImGui::GetCurrentContext();
    struct RestoreErrorCallback {
        ImGuiContext* context;ImGuiErrorCallback callback;void* data;
        ~RestoreErrorCallback(){context->ErrorCallback=callback;context->ErrorCallbackUserData=data;}
    } restore{context,context->ErrorCallback,context->ErrorCallbackUserData};
    context->ErrorCallback=[](ImGuiContext*,void* data,const char* message){static_cast<std::vector<std::string>*>(data)->emplace_back(message);};
    context->ErrorCallbackUserData=&imguiErrors;
    // Keep ImGui recovery/assert settings intact. Release builds must also fail
    // on diagnostic errors that otherwise only appear as an on-screen tooltip.
    auto frame=[&](unsigned n=3){app.SetFrameLimit(n);Check(app.Run()==0,"UI frames");if(!imguiErrors.empty())throw std::runtime_error("ImGui diagnostic: "+imguiErrors.front());};frame(60);
    auto point=[&](float x,float y){SyntheticEditorInput::Position(x,y);frame();};
    auto click=[&](float x,float y){point(x,y);SyntheticEditorInput::Button(0,true);frame(1);SyntheticEditorInput::Button(0,false);frame();};
    auto key=[&](ImGuiKey k){SyntheticEditorInput::Key(k,true);frame();SyntheticEditorInput::Key(k,false);frame();};
    auto log=[&]{auto* context=ImGui::GetCurrentContext();auto* previous=context->CurrentWindow;context->CurrentWindow=ImGui::FindWindowByName("Viewport");ImGui::LogToBuffer();context->LogWindow=nullptr;context->CurrentWindow=previous;frame(1);std::string value=ImGui::GetCurrentContext()->LogBuffer.c_str();ImGui::LogFinish();return value;};
    auto text=log();std::filesystem::create_directories("fog-post-captures");std::ofstream("fog-post-captures/ui-log.txt")<<text;auto first=text.find("File"),last=text.find("Help",first);Check(first!=std::string::npos&&last!=std::string::npos&&text.substr(first,last-first).find("Create")==std::string::npos,"main Menu Bar has no Create");
    auto create=[&](int category,int child){
        auto* viewport=ImGui::FindWindowByName(ui.IsViewportMaximized()?"Viewport##Maximized":"Viewport");ImGui::FocusWindow(viewport);frame();
        click(ui.IsViewportMaximized()?28:viewport->WorkRect.Min.x+25,ui.IsViewportMaximized()?8+ImGui::GetFrameHeight()*.5f:viewport->WorkRect.Min.y+ImGui::GetFrameHeight()*.5f);
        auto& popups=ImGui::GetCurrentContext()->OpenPopupStack;Check(!popups.empty()&&popups[0].Window,"Viewport Create popup opens");auto* root=popups[0].Window;
        auto content=log();for(const char* label:{"3D Object","Empty Entity","Camera","Player","Light","Environment"})Check(content.find(label)!=std::string::npos,"Viewport contains all creation categories");
        // Query the submitted menu geometry: GetFontSize() outside the popup
        // frame may still refer to the fallback window's unscaled font.
        // Camera is a new leaf after Empty Entity; UI precedes Environment.
        const float rowStep=(root->DC.CursorPosPrevLine.y-root->DC.CursorStartPos.y)/6;
        const int row=category==4?6:category>=2?category+1:category;
        click(root->DC.CursorStartPos.x+40,root->DC.CursorStartPos.y+row*rowStep+root->DC.PrevLineSize.y*.5f);Check(popups.Size>=2&&popups[1].Window,"creation submenu opens");auto* menu=popups[1].Window;
        const int children=category==0?5:(category==3||category==4)?3:1;
        const float childStep=children>1?(menu->DC.CursorPosPrevLine.y-menu->DC.CursorStartPos.y)/(children-1):0;
        click(menu->DC.CursorStartPos.x+25,menu->DC.CursorStartPos.y+child*childStep+menu->DC.PrevLineSize.y*.5f);
    };
    ui.MarkSaved();create(4,0);auto fog=ui.GetSelectedEntity();Check(app.GetWorld().HasComponent<FogComponent>(fog)&&ui.IsSceneDirty(),"Viewport Environment Fog selects and dirties");ui.UndoEdit(app);Check(!app.GetWorld().IsAlive(fog),"UI Fog Undo");ui.RedoEdit(app);fog=ui.GetSelectedEntity();Check(app.GetWorld().HasComponent<FogComponent>(fog)&&!app.GetWorld().HasComponent<ColliderComponent>(fog),"UI Fog Redo without collider");app.GetWorld().GetComponent<FogComponent>(fog)->enabled=false;
    create(0,0);auto cube=ui.GetSelectedEntity();Check(app.GetWorld().HasComponent<MeshRendererComponent>(cube),"Viewport Cube retained");app.GetWorld().GetComponent<TransformComponent>(cube)->position={3,1,5};
    create(3,1);Check(app.GetWorld().HasComponent<PointLightComponent>(ui.GetSelectedEntity()),"Viewport Point light retained");app.GetWorld().GetComponent<TransformComponent>(ui.GetSelectedEntity())->position={-3,3,5};
    create(2,0);auto start=ui.GetSelectedEntity();Check(app.GetWorld().HasComponent<PlayerStartComponent>(start),"Viewport PlayerStart retained");app.GetWorld().GetComponent<TransformComponent>(start)->position={-3,0,5};
    app.GetWorld().GetComponent<TransformComponent>(fog)->position={0,1,5};ui.ClearSelection();app.SetEditorCameraPose({0,1,-6},0,0);frame(20);auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());
    auto project=[&](Vec3 p){auto b=ui.GetViewportBounds();auto camera=editor::GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),(b[2]-b[0])/(b[3]-b[1]),app.GetCameraNearPlane(),app.GetCameraFarPlane());auto q=editor::ProjectGizmo(p,camera);Check(bool(q),"helper projects");return ImVec2(b[0]+(*q)[0]*(b[2]-b[0]),b[1]+(*q)[1]*(b[3]-b[1]));};
    auto p=project({0,1,5});auto helpers=gpu->ReadBackFramePixels();ui.SetGameView(true);frame();auto game=gpu->ReadBackFramePixels();int w,h;glfwGetFramebufferSize(window,&w,&h);unsigned changed=0;for(int y=int(p.y)-16;y<=int(p.y)+18;++y)for(int x=int(p.x)-18;x<=int(p.x)+18;++x)if(x>=0&&x<w&&y>=0&&y<h){auto at=(y*w+x)*4;changed+=helpers[at]!=game[at]||helpers[at+1]!=game[at+1]||helpers[at+2]!=game[at+2];}Check(changed>80,"cloud icon visible and Game View hides helper");ui.SetGameView(false);frame();click(p.x-10,p.y-6);Check(ui.GetSelectedEntity()==fog,"Fog icon picking");
    auto other=project({3,1,5});click(other.x,other.y);Check(ui.GetSelectedEntity()==cube,"mesh picking after helper");
    const auto normalBounds=ui.GetViewportBounds();
    const auto dock=ImGui::FindWindowByName("Viewport")->DockId;
    int originalWidth,originalHeight;glfwGetWindowSize(window,&originalWidth,&originalHeight);
    const float originalDpi=ImGui::GetStyle().FontScaleDpi;
    auto fullscreenLayout=[&]{
        frame(8);auto* viewport=ImGui::FindWindowByName("Viewport##Maximized");auto* main=ImGui::GetMainViewport();auto b=ui.GetViewportBounds();
        Check(viewport&&viewport->Active&&!viewport->ScrollbarX&&!viewport->ScrollbarY,"F11 no layout scrollbars");
        Check(viewport->ContentSize.x<=viewport->Size.x+.5f&&viewport->ContentSize.y<=viewport->Size.y+.5f,"overlay does not extend F11 content bounds");
        Check(std::abs(b[0]-main->Pos.x)<.5f&&std::abs(b[1]-main->Pos.y)<.5f&&std::abs(b[2]-b[0]-main->Size.x)<.5f&&std::abs(b[3]-b[1]-main->Size.y)<.5f,"F11 bounds fill main viewport");
        auto scale=ImGui::GetIO().DisplayFramebufferScale;
        Check(ui.GetViewportPixelWidth()==std::lround((b[2]-b[0])*scale.x)&&ui.GetViewportPixelHeight()==std::lround((b[3]-b[1])*scale.y),"F11 framebuffer dimensions match bounds and scale");
        Check(gpu->ReadBackViewportPixels().size()==std::size_t(ui.GetViewportPixelWidth())*ui.GetViewportPixelHeight()*4,"F11 actual render target follows bounds");
    };
    ImGui::FocusWindow(ImGui::FindWindowByName("Viewport"));frame();key(ImGuiKey_F11);Check(ui.IsViewportMaximized(),"F11 Viewport");ui.ClearSelection();frame();p=project({0,1,5});click(p.x-10,p.y-6);Check(ui.GetSelectedEntity()==fog,"F11 cloud picking");create(4,0);Check(app.GetWorld().HasComponent<FogComponent>(ui.GetSelectedEntity())&&ui.GetSelectedEntity()!=fog,"F11 overlay Create Fog");key(ImGuiKey_F11);Check(!ui.IsViewportMaximized(),"F11 layout return");
    for(float dpi:{1.f,1.5f,2.f}){
        key(ImGuiKey_F11);Check(ui.IsViewportMaximized(),"repeat F11 enters");editor::ApplyEditorTheme(dpi);
        glfwSetWindowSize(window,1024,768);fullscreenLayout();create(4,0);Check(app.GetWorld().HasComponent<FogComponent>(ui.GetSelectedEntity()),"scaled F11 Create remains interactive");fullscreenLayout();
        glfwSetWindowSize(window,1152,648);fullscreenLayout();
        key(ImGuiKey_F11);Check(!ui.IsViewportMaximized(),"repeat F11 exits");
        Check(ImGui::FindWindowByName("Viewport")->DockId==dock,"F11 preserves authored docking node");
    }
    editor::ApplyEditorTheme(originalDpi);glfwSetWindowSize(window,originalWidth,originalHeight);frame(10);
    auto restoredBounds=ui.GetViewportBounds();for(int i=0;i<4;++i)Check(std::abs(restoredBounds[i]-normalBounds[i])<1.f,"F11 restores normal viewport layout after resize/DPI");
    app.NewScene();frame();app.SetEditorPlayMode(true);frame();auto* viewport=ImGui::FindWindowByName("Viewport");click(viewport->WorkRect.Min.x+25,viewport->WorkRect.Min.y+ImGui::GetFrameHeight()*.5f);Check(ImGui::GetCurrentContext()->OpenPopupStack.empty()&&app.GetWorld().GetAliveCount()==0,"Play disables Create");app.SetEditorPlayMode(false);frame();Check(gpu->GetValidationErrorCount()==0,"UI DX12 validation zero");
}
static void Capture(Dx12RenderAdapter& gpu,const std::filesystem::path& prefix,unsigned w,unsigned h){
    auto bytes=gpu.ReadBackViewportPixels();Check(bytes.size()==std::size_t(w)*h*4,"capture dimensions");std::ofstream ppm(prefix.string()+".ppm",std::ios::binary);ppm<<"P6\n"<<w<<' '<<h<<"\n255\n";for(std::size_t i=0;i<bytes.size();i+=4)ppm.write(reinterpret_cast<const char*>(bytes.data()+i),3);
    auto hdr=gpu.ReadBackHdrPixels();std::ofstream raw(prefix.string()+".f32",std::ios::binary);raw.write(reinterpret_cast<const char*>(hdr.data()),hdr.size()*sizeof(float));Check(ppm.good()&&raw.good(),"captures saved");
}
static void Edges(){
    Application app;Check(app.Initialize(),"edge scene init");SyntheticEditorInput input;glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());app.NewScene();auto& ui=app.GetEditorLayer();ui.SetGridVisible(false);ui.SetBackgroundOverride(false);ui.SetDiagnosticViewport(400,300);
    auto settings=app.GetSceneSettings();settings.environment.enabled=false;settings.shadows.enabled=false;settings.lighting.ambientIntensity=0;settings.antiAliasing=AntiAliasingMode::Off;settings.background.background=EditorBackground::Solid;settings.background.solid={0,0,0};Check(app.SetSceneSettings(settings),"edge scene settings");
    auto cube=[&](Vec3 p,Vec3 scale){auto e=ui.CreatePrimitive(app,"cube");auto& t=*app.GetWorld().GetComponent<TransformComponent>(e);t.position=p;t.scale=scale;};cube({0,2,18},{40,20,1});cube({2,2,3},{1.5f,4,1});
    auto sun=ui.CreateLight(app,LightType::Directional);app.GetWorld().GetComponent<DirectionalLightComponent>(sun)->intensity=0;
    auto point=ui.CreateLight(app,LightType::Point);auto& l=*app.GetWorld().GetComponent<PointLightComponent>(point);l.intensity=50;l.range=3;l.attenuation=.12f;app.GetWorld().GetComponent<TransformComponent>(point)->position={2,2,10};
    auto entity=ui.CreateFog(app);auto& f=*app.GetWorld().GetComponent<FogComponent>(entity);f.mode=FogMode::Volumetric;f.heightFalloff=0;f.extinction=.08f;f.maxDistance=40;f.scatteringColor={1,1,1,1};ui.ClearSelection();ui.SetGameView(true);app.SetEditorCameraPose({0,2,-6},0,0);
    auto frame=[&](unsigned n=6){app.SetFrameLimit(n);Check(app.Run()==0,"edge scene frames");};frame(100);auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());
    for(auto q:{FogQuality::Low,FogQuality::Medium,FogQuality::High}){
        f.quality=q;frame();auto hdr=gpu->ReadBackHdrPixels();unsigned foreground=0;float brightest=0;
        for(int y=80;y<220;++y)for(int x=210;x<330;++x){float dx=(2*(x+.5f)/400-1)*(400.f/300)*std::tan(app.GetCameraVerticalFovRadians()*.5f),dy=(1-2*(y+.5f)/300)*std::tan(app.GetCameraVerticalFovRadians()*.5f);float hitX=dx*8.5f,hitY=2+dy*8.5f;auto at=(y*400+x)*4;brightest=std::max(brightest,hdr[at]);
            if(hitX>1.252f&&hitX<2.748f&&hitY>.002f&&hitY<3.998f){++foreground;Check(hdr[at]<.0001f,"background scattering cannot leak across foreground silhouette");}
        }Check(foreground>100&&brightest>.002f,"opaque foreground and lit background both present");
    }
    ui.SetDiagnosticViewport(401,301);frame(12);Check(gpu->ReadBackHdrPixels().size()==401*301*4,"odd-sized half-resolution resize");Check(gpu->GetValidationErrorCount()==0,"edge scene DX12 validation zero");
}
static void Benchmark(const std::string& label){
    auto output=std::filesystem::path("fog-post-captures")/label;std::filesystem::create_directories(output);std::ofstream csv(output/"timings.csv");csv<<"scene,variant,quality,width,height,steps,cpu_ms,fog_gpu_ms,frame_gpu_ms,frame_cpu_ms,bytes\n";
    for(bool nearLight:{false,true}){
        Application app;Check(app.Initialize(),"benchmark init");SyntheticEditorInput input;glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());app.NewScene();auto& ui=app.GetEditorLayer();ui.SetFixedDiagnosticLayout(true);ui.SetGridVisible(false);ui.SetLightGizmosVisible(false);ui.SetBackgroundOverride(false);
        auto scene=app.GetSceneSettings();scene.environment.enabled=false;scene.antiAliasing=AntiAliasingMode::Off;scene.background.background=EditorBackground::Solid;scene.background.solid={.05f,.05f,.05f};scene.shadows.enabled=!nearLight;scene.shadows.coverage=100;scene.shadows.depthRange=200;scene.lighting.ambientIntensity=.02f;Check(app.SetSceneSettings(scene),"benchmark settings");
        auto cube=[&](Vec3 position,Vec3 scale){auto e=ui.CreatePrimitive(app,"cube");auto& t=*app.GetWorld().GetComponent<TransformComponent>(e);t.position=position;t.scale=scale;return e;};
        if(nearLight){cube({0,2,18},{40,20,1});cube({2,2,3},{1.5f,4,1});}
        else{cube({-3,1,6},{2,2,2});cube({0,3,18},{2,2,2});cube({4,6,32},{2,2,2});auto e=ui.CreatePrimitive(app,"plane");app.GetWorld().GetComponent<TransformComponent>(e)->scale={100,1,100};}
        auto sun=ui.CreateLight(app,LightType::Directional);auto& sunlight=*app.GetWorld().GetComponent<DirectionalLightComponent>(sun);sunlight.primary=true;sunlight.intensity=4;sunlight.castShadows=true;app.GetWorld().GetComponent<TransformComponent>(sun)->rotation={.35f,3.14159265f,0};
        auto point=ui.CreateLight(app,LightType::Point);auto& pl=*app.GetWorld().GetComponent<PointLightComponent>(point);pl.intensity=nearLight?20.f:80.f;pl.range=nearLight?12.f:25.f;pl.attenuation=.12f;pl.color={1,.25f,.12f};app.GetWorld().GetComponent<TransformComponent>(point)->position=nearLight?Vec3{.3f,2,-1}:Vec3{0,4,3};
        auto spot=ui.CreateLight(app,LightType::Spot);auto& sl=*app.GetWorld().GetComponent<SpotLightComponent>(spot);sl.intensity=70;sl.range=40;sl.attenuation=.12f;sl.innerAngle=25;sl.outerAngle=55;sl.color={.15f,.3f,1};auto& st=*app.GetWorld().GetComponent<TransformComponent>(spot);st.position={-2,3,-1};st.rotation={-.2f,.2f,0};
        auto fog=ui.CreateFog(app);auto& f=*app.GetWorld().GetComponent<FogComponent>(fog);f.mode=FogMode::Volumetric;f.extinction=.04f;f.heightFalloff=.12f;f.baseHeight=2;f.maxDistance=70;f.anisotropy=.3f;f.scatteringColor={.7f,.8f,.9f,1};ui.ClearSelection();ui.SetGameView(true);app.SetEditorCameraPose({0,2,-6},0,0);
        auto frame=[&](unsigned n){app.SetFrameLimit(n);Check(app.Run()==0,"benchmark frames");};ui.SetDiagnosticViewport(400,300);frame(100);auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());
        auto measure=[&](const char* variant,unsigned w,unsigned h,FogQuality q){
            f.quality=q;ui.SetDiagnosticViewport(w,h);frame(120);double cpu=0,fogMs=0,frameMs=0,wall=0;
            for(int i=0;i<60;++i){auto start=std::chrono::steady_clock::now();frame(1);wall+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();auto d=gpu->GetFogDiagnostics();Check(d.active&&d.error.empty(),"benchmark fog healthy");cpu+=d.cpuMs;fogMs+=d.gpuMs;frameMs+=gpu->GetGpuTimings().frameMs;}
            csv<<(nearLight?"near":"scene")<<','<<variant<<','<<int(q)<<','<<w<<','<<h<<','<<FogSamples(q)<<','<<cpu/60<<','<<fogMs/60<<','<<frameMs/60<<','<<wall/60<<','<<gpu->GetFogDiagnostics().gpuBytes<<'\n';csv.flush();
            if(std::string(variant)=="all"&&(w==400||q==FogQuality::Medium))Capture(*gpu,output/((nearLight?"near-":"scene-")+std::to_string(w)+"-"+std::to_string(int(q))),w,h);
        };
        for(auto size:std::array<std::array<unsigned,2>,2>{{{400,300},{1280,720}}})for(auto q:{FogQuality::Low,FogQuality::Medium,FogQuality::High})measure("all",size[0],size[1],q);
        if(!nearLight){float pointIntensity=pl.intensity,spotIntensity=sl.intensity;pl.intensity=sl.intensity=0;measure("no_local",1280,720,FogQuality::Medium);pl.intensity=pointIntensity;sl.intensity=spotIntensity;scene.shadows.enabled=false;Check(app.SetSceneSettings(scene),"ablation shadows");measure("no_shadow",1280,720,FogQuality::Medium);f.sceneLighting=false;measure("constant",1280,720,FogQuality::Medium);}
        Check(gpu->GetValidationErrorCount()==0,"benchmark DX12 validation zero");
    }
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{std::filesystem::remove("editor-preferences.json");std::filesystem::remove("imgui.ini");
#if defined(ENABLE_DX12)
    if(argc>2&&std::string(argv[1])=="--bench")Benchmark(argv[2]);else if(argc>1&&std::string(argv[1])=="--dx12"){Ui();Edges();}else
#endif
    Cpu();std::cout<<"Fog post hotfix: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
