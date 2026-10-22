#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "ecs/CameraState.h"
#include "ecs/ComponentRegistry.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "ecs/components/TagComponent.h"
#include "ecs/components/LuaScriptComponent.h"
#include "ecs/components/FogComponent.h"
#include <imgui_internal.h>
#include "scripting/EntityReference.h"
#include "scripting/Prefab.h"
#include "scene/SceneSerializer.h"
#include "resources/ResourceManager.h"
#include <fstream>
#include <iostream>
#include <limits>
#if defined(ENABLE_DX12)||defined(ENABLE_VULKAN)
#include "platform/GlfwWindow.h"
#include <GLFW/glfw3.h>
#endif
#if defined(ENABLE_DX12)
#include "render/backends/dx12/Dx12RenderAdapter.h"
#endif
using namespace ecs;
static int checks=0;
static void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);++checks;}
static void Near(float a,float b,const std::string& message,float epsilon=1e-4f){Check(std::abs(a-b)<epsilon,message);}
struct Fixture {
    std::string key="camera-system-tests-"+NewSceneIdentity()+"/";
    std::filesystem::path path=AssetPaths::ResolveAssetRoot()/std::filesystem::u8path(key);
    Fixture(){Check(std::filesystem::create_directory(path),"owned fixture folder");}
    ~Fixture(){std::error_code error;std::filesystem::remove_all(path,error);}
    std::string Write(const std::string& name,const std::string& text){std::ofstream file(path/name);file<<text;Check(bool(file),"write fixture");return key+name;}
};
static Entity CameraEntity(World& w,Vec3 position={}){auto e=w.CreateEntity();w.AddComponent<TransformComponent>(e).position=position;w.AddComponent<CameraComponent>(e);return e;}
static Entity Actor(World& w,const std::string& path){auto e=w.CreateEntity();w.AddComponent<TransformComponent>(e);auto& s=w.AddComponent<LuaScriptComponent>(e);s.scriptPath=path;s.cameraEnabled=true;return e;}
static nlohmann::json Ref(World& w,Entity e){if(!w.HasComponent<SceneIdentityComponent>(e))w.AddComponent<SceneIdentityComponent>(e);return nlohmann::json{{"$entity",w.GetComponent<SceneIdentityComponent>(e)->value}};}
static float ClipDepth(const CameraState& s,float z){auto& p=s.projection;return (p[10]*z+p[14])/(p[11]*z+p[15]);}
static void ComponentsAndProjection(Fixture& files){
    World w;auto& r=ComponentRegistry::Builtins();std::string error;auto entity=w.CreateEntity();
    Check(r.Add(w,entity,ComponentId("Camera"),&error),error);Check(w.HasComponent<TransformComponent>(entity),"registry Camera auto adds Transform");
    Check(!r.Remove(w,entity,ComponentId("Transform"),&error),"camera protects Transform dependency");
    auto& c=*w.GetComponent<CameraComponent>(entity);Check(ValidCamera(c),"valid defaults");
    Check(r.Set(w,entity,ComponentId("Camera"),"nearClip",.5f,&error),error);
    Check(!r.Set(w,entity,ComponentId("Camera"),"farClip",.4f,&error),"reject near >= far");Near(c.farClip,2000,"invalid property edit rolls back");
    Check(!r.Set(w,entity,ComponentId("Camera"),"fieldOfView",std::numeric_limits<float>::quiet_NaN(),&error),"reject NaN");
    Check(!r.Set(w,entity,ComponentId("Camera"),"projection",2,&error),"reject projection enum");
    auto snapshot=r.Capture(w,entity);auto bad=snapshot;bad["Camera"]["farClip"]=.1f;World staged;bool rejected=false;
    try{r.Restore(staged,staged.CreateEntity(),bad);}catch(...){rejected=true;}Check(rejected,"deserialize validates cross-field clips");
    c.fieldOfView=.9f;c.orthographicSize=12;c.farClip=140;c.priority=27;
    auto state=ResolveCameraState(w,entity,2);Near(state.projection[5]/state.projection[0],2,"perspective aspect");
    Near(ClipDepth(state,c.nearClip),0,"perspective near depth");Near(ClipDepth(state,c.farClip),1,"perspective far depth");
    c.projection=CameraProjection::Orthographic;state=ResolveCameraState(w,entity,2);
    Near(state.projection[5],1.f/6,"orthographic full vertical span");Near(state.projection[0],1.f/12,"orthographic horizontal span");
    Near(ClipDepth(state,c.nearClip),0,"orthographic near depth");Near(ClipDepth(state,c.farClip),1,"orthographic far depth");
    auto captured=SceneSerializer::CaptureWorld(w);Check(SceneSerializer::SaveWorld(files.path/"camera.json",w,&error),error);
    World loaded;SceneSettings settings;Check(SceneSerializer::LoadWorld(files.path/"camera.json",loaded,settings,&error),error);
    Check(SceneSerializer::CaptureWorld(loaded)==captured,"all Camera properties scene roundtrip");
    World::CloneMap map;auto clone=w.CloneData(&map);Check(r.Capture(*clone,map.at(entity))==r.Capture(w,entity),"Camera runtime clone");
    auto prefab=scripting::CapturePrefab(w,entity);Check(SceneSerializer::WriteAtomic(files.path/"camera.prefab.json",prefab,&error,false),error);
    auto read=scripting::ReadPrefab(files.key+"camera.prefab.json");auto instance=scripting::InstantiatePrefab(loaded,files.key+"camera.prefab.json",read,{3,4,5});
    auto* copied=loaded.GetComponent<CameraComponent>(instance);Check(copied&&copied->projection==c.projection&&copied->priority==27&&copied->nearClip==c.nearClip&&copied->farClip==c.farClip&&copied->orthographicSize==12,"Prefab file roundtrip");
    auto corrupt=c;corrupt.orthographicSize=0;Check(!ValidCamera(corrupt),"zero orthographic size rejected");corrupt=c;corrupt.nearClip=-1;Check(!ValidCamera(corrupt),"negative clip rejected");
}
static void ParentingAndSelection(){
    World w;auto rig=w.CreateEntity();auto& t=w.AddComponent<TransformComponent>(rig);t.position={10,0,0};t.rotation={0,1.570796327f,0};t.scale={2,2,2};
    auto camera=CameraEntity(w,{0,1,-3});Check(w.SetParent(camera,rig,false),"TPS camera parent");auto& local=*w.GetComponent<TransformComponent>(camera);local.rotation={.3f,0,.2f};
    auto state=ResolveCameraState(w,camera,1.5f);Near(state.pose.position.x,4,"rig scale translates offset");Near(state.pose.position.y,2,"rig scaled Y offset");
    auto expected=CameraCompose(RenderRotation(t.rotation),RenderRotation(local.rotation));
    Near(state.pose.rotation.x,expected.x,"parented rotation X");Near(state.pose.rotation.y,expected.y,"parented rotation Y");
    Near(state.forward.y,-std::sin(.3f)*std::cos(.2f),"positive ECS pitch points down");
    Near(state.right.x*state.up.x+state.right.y*state.up.y+state.right.z*state.up.z,0,"view orthogonal despite scale");
    t.scale={-2,-2,-2};auto negative=ResolveCameraState(w,camera,1.5f);Near(negative.forward.y,state.forward.y,"negative scale does not invert view");t.scale={1,1,1};
    // FPS camera child follows the same interpolation as its physical parent.
    w.AddComponent<RigidbodyComponent>(rig);w.AddComponent<ColliderComponent>(rig);w.SetParent(camera,{},false);t.rotation={};t.position={};
    Check(w.SetParent(camera,rig,false),"FPS parent");local.position={0,.8f,0};local.rotation={};
    RenderInterpolation interpolation;interpolation.BeginTick(w);t.position.x=2;interpolation.EndTick(w);interpolation.SetFrame(w,true,.5);
    state=ResolveCameraState(w,camera,1,&interpolation);Near(state.pose.position.x,1,"camera inherits interpolated physics parent");Near(state.pose.position.y,.8f,"FPS eye offset");
    local.rotation={1.570796327f,0,0};state=ResolveCameraState(w,camera,1);Near(state.forward.y,-1,"top-down camera at pole");
    local.rotation={.7f,.8f,0};w.GetComponent<CameraComponent>(camera)->projection=CameraProjection::Orthographic;state=ResolveCameraState(w,camera,1);Check(std::isfinite(state.view[14]),"isometric orthographic rig");
    scripting::GameplayCamera runtime;auto second=CameraEntity(w);Check(runtime.Active(w)==camera,"equal priority stable lower index");w.GetComponent<CameraComponent>(second)->priority=10;Check(runtime.Active(w)==second,"higher priority fallback");
    Check(runtime.Select(w,camera)&&runtime.Active(w)==camera,"explicit selection overrides priority");
    w.GetComponent<CameraComponent>(camera)->enabled=false;Check(runtime.Active(w)==second,"disabled selected camera falls back");
    w.GetComponent<CameraComponent>(camera)->enabled=true;Check(runtime.Active(w)==second,"re-enabled old selection not silently restored");
    Check(runtime.Select(w,camera),"reselect");w.DestroyEntity(camera);Check(runtime.Active(w)==second,"destroyed selection falls back");
    auto reused=w.CreateEntity();Check(!runtime.Select(w,camera)&&!runtime.Select(w,reused),"stale handle and missing Camera rejected");World foreign;auto other=CameraEntity(foreign);Check(!runtime.Select(w,other),"foreign world rejected");
    w.GetComponent<CameraComponent>(second)->enabled=false;Check(!runtime.Active(w).IsValid(),"no eligible camera uses legacy");
}
static void LuaSelection(Fixture& files){
    World w;auto a=CameraEntity(w),b=CameraEntity(w);scripting::GameplayCamera camera;scripting::GameplayServices host;host.camera=&camera;
    auto path=files.Write("selection.lua",R"(return {fields={a={type='entity'},b={type='entity'}},on_start=function(self)
        Camera.set_active(self.a);assert(Camera.get_active()==self.a and Camera.is_active(self.a));assert(Camera.acquire());
        Camera.set_position(Vec3.new(7,8,9));Camera.set_angles(.4,.2);Camera.follow(self.entity,Vec3.new(0,1,0));Camera.rotate(.1,.1);
        assert(Camera.is_active(self.a));assert(Camera.position().x==7);assert(Camera.forward().y>0);Camera.release();assert(Camera.is_active(self.a))
        end,on_update=function(self) if not self.sent and World.get_position(self.entity).x>0 then World.send_event(self.entity,'switch',0);self.sent=true end end,on_event=function(self,event) if event.type=='switch' then Camera.set_active(self.b);assert(Camera.get_active()==self.b) end end})");
    auto actor=Actor(w,path);w.GetComponent<LuaScriptComponent>(actor)->properties={{"a",Ref(w,a)},{"b",Ref(w,b)}};
    scripting::LuaRuntime lua;lua.Start(w,{},host);lua.Update(0);Check(lua.Error(path).empty(),lua.Error(path));Check(camera.Active(w)==a,"Lua selects without input permission");Near(camera.yaw,.5f,"legacy rotate remains compatible");
    w.GetComponent<TransformComponent>(actor)->position.x=1;lua.Update(0);Check(camera.Active(w)==a,"selection event queued");lua.Update(0);Check(camera.Active(w)==b&&lua.Error(path).empty(),"Lua switches at delivery");
    auto denied=files.Write("denied.lua","return {on_start=function(self) Camera.get_active() end}");auto d=Actor(w,denied);w.GetComponent<LuaScriptComponent>(d)->cameraEnabled=false;lua.Update(0);Check(lua.Error(denied).find("permission")!=std::string::npos,"selection permission enforced");
    auto invalid=files.Write("invalid.lua","return {on_start=function(self) Camera.set_active(self.entity) end}");Actor(w,invalid);lua.Update(0);Check(lua.Error(invalid).find("invalid Camera")!=std::string::npos,"invalid entity selection diagnosed");
    lua.Stop();camera={};w.GetComponent<CameraComponent>(a)->enabled=w.GetComponent<CameraComponent>(b)->enabled=false;
    auto empty=files.Write("empty.lua","return {on_start=function(self) assert(Camera.get_active()==nil);assert(not Camera.is_active(self.entity));assert(Camera.acquire());Camera.rotate(0,.1) end}");World legacy;Actor(legacy,empty);lua.Start(legacy,{},host);lua.Update(0);Check(lua.Error(empty).empty()&&!camera.Active(legacy).IsValid(),"legacy APIs and nil selection without Camera Entity");lua.Stop();
}
#if defined(ENABLE_DX12)
static void Native(Fixture& files){
    Application app;Check(app.Initialize(),"initialize native DX12 editor");SyntheticEditorInput input;
    glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());app.NewScene();auto& ui=app.GetEditorLayer();ui.SetFixedDiagnosticLayout(true);ui.SetDiagnosticViewport(360,240);ui.SetGameView(true);
    auto frame=[&](unsigned count=5){app.SetFrameLimit(count);Check(app.Run()==0,"native render frames");};
    Check(app.SetEditorCameraPose({0,0,-7},0,0),"editor camera setup");
    auto a=ui.CreateCamera(app);Check(app.GetWorld().HasComponent<CameraComponent>(a),"editor creates Camera");frame();auto* inspector=ImGui::FindWindowByName("Inspector");Check(inspector&&inspector->Active,"Camera renders in actual Inspector");
    Check(ui.SetProperty(app,a,ComponentId("Camera"),"fieldOfView",.9f),ui.GetError());ui.UndoEdit(app);Near(app.GetWorld().GetComponent<CameraComponent>(a)->fieldOfView,1.04719755f,"Inspector property Undo");ui.RedoEdit(app);
    auto b=ui.DuplicateEntity(app,a);Check(app.GetWorld().HasComponent<CameraComponent>(b),"editor duplicate Camera");
    Check(ui.SetProperty(app,b,ComponentId("Transform"),"position",Vec3{5,0,-7}),ui.GetError());
    Check(ui.SetProperty(app,b,ComponentId("Camera"),"projection",1),ui.GetError());Check(ui.SetProperty(app,b,ComponentId("Camera"),"orthographicSize",6.f),ui.GetError());
    auto cube=ui.CreatePrimitive(app,"cube");app.GetWorld().GetComponent<TransformComponent>(cube)->position={0,0,0};ui.ClearSelection();
    auto settings=app.GetSceneSettings();settings.environment.enabled=false;settings.shadows.enabled=false;settings.antiAliasing=AntiAliasingMode::FxaaHigh;Check(app.SetSceneSettings(settings),"native settings");app.SetViewportUnlit(true);
    auto script=files.Write("native.lua","return {fields={a={type='entity'},b={type='entity'}},on_start=function(self) Camera.set_active(self.a) end,on_update=function(self) if not self.sent and World.get_position(self.entity).x>0 then World.send_event(self.entity,'switch',0);self.sent=true end;if self.sent and not self.moved and World.get_position(self.entity).x>1 then World.set_position(self.b,Vec3.new(0,0,-7));self.moved=true end end,on_event=function(self,event) if event.type=='switch' then Camera.set_active(self.b) end end}");
    auto actor=Actor(app.GetWorld(),script);app.GetWorld().GetComponent<LuaScriptComponent>(actor)->properties={{"a",Ref(app.GetWorld(),a)},{"b",Ref(app.GetWorld(),b)}};
    auto authored=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());auto editorPosition=app.GetCameraPosition();frame();Check(!app.GetRenderCameraState().entity.IsValid(),"Edit uses independent editor camera");
    app.SetEditorPlayMode(true);app.SetEditorPaused(true);app.GetLuaRuntime().Update(0);frame(12);auto ra=app.MapToRuntime(a),rb=app.MapToRuntime(b),ractor=app.MapToRuntime(actor);
    Check(app.GetActiveCamera()==ra&&app.GetRenderCameraState().entity==ra,"Play renders Lua selected Camera Entity");
    auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());auto first=gpu->ReadBackHdrPixels();Check(first.size()==360*240*4,"native HDR camera readback: "+std::to_string(first.size()));
    app.GetWorld().GetComponent<TransformComponent>(ractor)->position.x=1;app.GetLuaRuntime().Update(0);app.GetLuaRuntime().Update(0);frame(12);Check(app.GetRenderCameraState().entity==rb&&app.GetRenderCameraState().projection[15]==1,"runtime switches to Orthographic");
    auto second=gpu->ReadBackHdrPixels();Check(second.size()==first.size(),"same render target after source switch");double difference=0;for(std::size_t i=0;i<first.size();i+=4)difference+=std::abs(first[i]-second[i]);Check(difference>1,"camera switch changes actual image");
    app.GetWorld().GetComponent<TransformComponent>(ractor)->position.x=2;app.GetLuaRuntime().Update(0);frame();auto moved=gpu->ReadBackHdrPixels();difference=0;for(std::size_t i=0;i<moved.size();i+=4)difference+=std::abs(moved[i]-second[i]);Check(difference>1,"Lua World camera Transform changes rendered image");
    // The orthographic path must also reconstruct depth and parallel fog rays.
    auto fogEntity=app.GetWorld().CreateEntity();app.GetWorld().AddComponent<TransformComponent>(fogEntity);
    auto& fog=app.GetWorld().AddComponent<FogComponent>(fogEntity);fog.startDistance=0;fog.endDistance=20;fog.baseHeight=1;fog.sceneLighting=false;fog.maxDistance=30;
    for(auto mode:{FogMode::Distance,FogMode::Height,FogMode::Volumetric}){
        fog.mode=mode;frame(8);const auto& diagnostics=gpu->GetFogDiagnostics();
        Check(diagnostics.active&&diagnostics.error.empty(),"Orthographic fog GPU pipeline healthy");
        auto pixels=gpu->ReadBackHdrPixels();Check(pixels.size()==moved.size(),"orthographic fog readback");
        double change=0;for(std::size_t i=0;i<pixels.size();++i){if(!std::isfinite(pixels[i]))throw std::runtime_error("nonfinite orthographic fog pixel");if(i%4!=3)change+=std::abs(pixels[i]-moved[i]);}
        Check(change>1,"orthographic fog affects actual image");
        if(mode==FogMode::Distance){
            fog.maxDistance=1;frame();auto shortRange=gpu->ReadBackHdrPixels();
            const auto center=(120*360+180)*4;
            Near(shortRange.at(center),pixels.at(center),"Distance Fog maxDistance does not clamp geometry",1e-5f);
            fog.maxDistance=30;
        }
    }
    app.GetWorld().DestroyEntity(fogEntity);
    app.GetWorld().GetComponent<CameraComponent>(rb)->enabled=false;frame();Check(app.GetRenderCameraState().entity==ra,"native disabled selection fallback");
    app.GetWorld().DestroyEntity(ra);frame();Check(!app.GetActiveCamera().IsValid()&&!app.GetRenderCameraState().entity.IsValid(),"native removal falls back to legacy");
    app.SetEditorPlayMode(false);frame();Near(app.GetCameraPosition().z,editorPosition.z,"Stop restores editor camera");Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==authored,"Stop preserves original authored data");
    app.SetEditorPlayMode(true);app.SetEditorPaused(true);app.GetLuaRuntime().Update(0);frame();Check(app.GetActiveCamera()==app.MapToRuntime(a),"repeat Play resets selection handles");app.SetEditorPlayMode(false);
    Check(gpu->GetValidationErrorCount()==0,"zero DX12 validation errors");app.Shutdown();
}
#endif
#if defined(ENABLE_VULKAN)
static void VulkanSmoke(){
    GlfwWindow window;Check(window.Create(320,240,"WhispEngine Camera Vulkan regression"),"Vulkan window");
    auto adapter=RenderFactory::Create(RenderBackend::Vulkan);Check(adapter&&adapter->Initialize(&window),"Vulkan initialization");
    World w;auto camera=CameraEntity(w,{0,0,-5});auto cube=w.CreateEntity();w.AddComponent<TransformComponent>(cube);w.AddComponent<ColliderComponent>(cube);
    RenderSystem render;render.SetRenderAdapter(adapter.get());render.SetDebugCollidersEnabled(true);
    for(auto projection:{CameraProjection::Perspective,CameraProjection::Orthographic}){
        w.GetComponent<CameraComponent>(camera)->projection=projection;
        auto state=ResolveCameraState(w,camera,320.f/240);render.SetCameraState(state);
        for(int frame=0;frame<8;++frame){window.PollEvents();adapter->BeginFrame();adapter->Clear(.02f,.02f,.03f,1);render.Update(w,0);adapter->EndFrame();adapter->Present();}
        Check(render.GetCameraState().projection==state.projection,"Vulkan consumes shared camera state");
    }
    render.ReleaseGpuResources();render.SetRenderAdapter(nullptr);adapter->Shutdown();
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{Fixture files;
#if defined(ENABLE_VULKAN)
    if(argc>1&&std::string(argv[1])=="--vulkan"){VulkanSmoke();std::cout<<"Camera Vulkan: "<<checks<<" checks passed\n";return 0;}
#endif
#if defined(ENABLE_DX12)
    if(argc>1&&std::string(argv[1])=="--dx12")Native(files);else
#endif
    {ComponentsAndProjection(files);ParentingAndSelection();LuaSelection(files);}
    std::cout<<"Camera System: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
