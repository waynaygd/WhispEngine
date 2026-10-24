#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "core/RuntimeLevel.h"
#include "ecs/CameraAttachments.h"
#include "ecs/ComponentRegistry.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "ecs/components/FogComponent.h"
#include "ecs/components/UICanvasComponent.h"
#include "scripting/EntityReference.h"
#include <fstream>
#include <iostream>
#if defined(ENABLE_DX12)
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <GLFW/glfw3.h>
#endif
using namespace ecs;
static unsigned checks=0;
static void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);++checks;}
static Entity Node(World& w,Vec3 p={}){auto e=w.CreateEntity();w.AddComponent<TransformComponent>(e).position=p;return e;}
static Entity Tagged(const World& w,const std::string& tag){for(auto e:w.Entities())if(auto* t=w.GetComponent<TagComponent>(e);t&&t->name==tag)return e;return {};}
static std::string Bytes(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
struct Files {
    std::filesystem::path directory;std::shared_ptr<const ProjectContext> project,previous=ProjectManager::Active();
    Files(){
        directory=AssetPaths::GetExecutableDirectory()/("travel_"+NewSceneIdentity());
        Check(std::filesystem::create_directory(directory),"owned test directory");
        auto defaultAssets=AssetPaths::ResolveAssetRoot();project=ProjectManager::Create(directory,"Travel");
        for(auto folder:{"models/primitives","materials"}){
            auto source=defaultAssets/folder;if(std::filesystem::exists(source))std::filesystem::copy(source,project->ResolveAsset(folder),std::filesystem::copy_options::recursive|std::filesystem::copy_options::overwrite_existing);
        }
        std::ofstream(project->ResolveAsset("scripts/player.lua"))<<"return {fields={camera={type='entity'}},on_start=function(self) assert(World.is_alive(self.camera));Camera.set_active(self.camera);local o=World.find_by_tag('Ortho');if #o>0 then Camera.set_active(o[1]) end;Camera.get_forward(self.camera) end}";
        std::ofstream(project->ResolveAsset("scripts/travel.lua"))<<R"(return {
          on_start=function(self) self.started=true;Timer.after(100,'expired') end,
          on_update=function(self)
            local phase=World.get_position(self.entity).x
            if phase==0 then return end
            World.set_position(self.entity,Vec3.new(0,0,0))
            if phase==1 then assert(World.load_scene('Scenes/B.json'))
            elseif phase==2 then assert(World.load_scene('Scenes/A.json'))
            elseif phase==3 then World.load_scene('Scenes/Missing.json')
            elseif phase==4 then World.load_scene('Scenes/Corrupt.json')
            elseif phase==5 then World.load_scene('Scenes/BadPlayer.json')
            elseif phase==6 then World.load_scene('../outside.json')
            elseif phase==7 then assert(World.load_scene('Scenes/B.json'));assert(not World.load_scene('Scenes/A.json'))
            elseif phase==8 then World.load_scene('Scenes/B.json');World.destroy(self.entity)
            elseif phase==9 then Timer.after(.01,'travel')
            elseif phase==10 then World.send_event(self.entity,'travel',0)
            elseif phase==11 then World.load_scene('Scenes/Legacy.json')
            elseif phase==12 then World.load_scene('Scenes/Empty.json') end
          end,
          on_event=function(self,event) if event.type=='travel' then World.load_scene('Scenes/B.json') elseif event.type=='expired' then error('old timer survived') end end,
          on_stop=function(self) if World.is_alive(self.entity) then World.set_position(self.entity,Vec3.new(0,17,0)) end end,
          on_destroy=function(self) if World.is_alive(self.entity) then assert(World.get_position(self.entity).y==17);World.set_position(self.entity,Vec3.new(0,18,0)) end end
        })";
        std::ofstream(project->ResolveAsset("scripts/stop-request.lua"))<<"return {on_stop=function(self) World.load_scene('Scenes/A.json') end,on_destroy=function(self) World.set_position(self.entity,Vec3.new(0,18,0)) end}";
        World prefab;auto root=Node(prefab);prefab.AddComponent<RigidbodyComponent>(root).useGravity=false;prefab.AddComponent<ColliderComponent>(root).autoFitFromMesh=false;prefab.AddComponent<CharacterMovementComponent>(root);
        auto camera=Node(prefab,{0,0,-7});prefab.AddComponent<CameraComponent>(camera);Check(prefab.SetParent(camera,root,false),"prefab camera parent");prefab.AddComponent<SceneIdentityComponent>(camera);
        auto& playerScript=prefab.AddComponent<LuaScriptComponent>(root);playerScript.scriptPath="scripts/player.lua";playerScript.cameraEnabled=true;playerScript.properties={{"camera",{{"$entity",prefab.GetComponent<SceneIdentityComponent>(camera)->value}}}};
        std::string error;Check(SceneSerializer::WriteAtomic(project->ResolveAsset("prefabs/player.prefab.json"),scripting::CapturePrefab(prefab,root),&error,false),error);
        auto settings=project->Settings();settings.maps.defaultGameMode="Default";settings.maps.modes["Default"]={"prefabs/player.prefab.json",true};project=ProjectManager::WithSettings(*project,settings);
        std::filesystem::create_directory(project->ResolveAsset("ui"));std::ofstream(project->ResolveAsset("ui/travel.ui.json"))<<nlohmann::json(ui::EmptyDocument()).dump();
        for(auto name:{"A","B","BadPlayer","Legacy","Empty"}){
            World scene;auto actor=Node(scene);scene.AddComponent<TagComponent>(actor).name="Travel";scene.AddComponent<LuaScriptComponent>(actor).scriptPath="scripts/travel.lua";
            auto cube=Node(scene,{std::string(name)=="B"?4.f:0.f,0,0});scene.AddComponent<MeshRendererComponent>(cube).meshPath="models/primitives/cube.obj";scene.AddComponent<MaterialComponent>(cube).materialPath="materials/default_primitive.material.json";scene.AddComponent<ColliderComponent>(cube).autoFitFromMesh=false;
            auto attachment=Node(scene);scene.AddComponent<MeshRendererComponent>(attachment).meshPath="models/primitives/cube.obj";scene.AddComponent<MaterialComponent>(attachment).materialPath="materials/default_primitive.material.json";scene.AddComponent<CameraAttachmentComponent>(attachment)={true,{-.8f,-.5f,3},{}};scene.GetComponent<TransformComponent>(attachment)->scale={.1f,.1f,.1f};scene.AddComponent<TagComponent>(attachment).name="Attachment";
            if(std::string(name)!="Empty"){
                auto start=Node(scene,{std::string(name)=="B"?2.f:0.f,0,0});auto& marker=scene.AddComponent<PlayerStartComponent>(start);marker.spawnMode=3;
                if(std::string(name)=="BadPlayer"){marker.spawnMode=2;marker.playerPrefab="prefabs/missing.prefab.json";}
                if(std::string(name)=="Legacy"){marker.spawnMode=1;marker.controllerScript.clear();}
            }
            // B also has an authored orthographic source with higher priority.
            if(std::string(name)=="B"){auto camera=Node(scene,{4,0,-7});auto& c=scene.AddComponent<CameraComponent>(camera);c.projection=CameraProjection::Orthographic;c.priority=10;scene.AddComponent<TagComponent>(camera).name="Ortho";}
            if(std::string(name)=="A"){auto canvas=Node(scene);scene.AddComponent<UICanvasComponent>(canvas).document="ui/travel.ui.json";scene.AddComponent<TagComponent>(canvas).name="Canvas";}
            if(std::string(name)=="B"){auto fog=Node(scene);scene.AddComponent<FogComponent>(fog).density=.01f;}
            SceneSettings visual;visual.environment.enabled=false;visual.shadows.enabled=false;visual.antiAliasing=AntiAliasingMode::FxaaHigh;
            Check(SceneSerializer::SaveWorld(project->ResolveScene("Scenes/"+std::string(name)+".json"),scene,&error,&visual),error);
        }
        std::ofstream(project->ResolveScene("Scenes/Corrupt.json"))<<"{broken";
    }
    ~Files(){ProjectManager::Activate(previous);std::error_code error;std::filesystem::remove_all(directory,error);}
};
static void Cpu(Files& f){
    ProjectManager::Activate(f.project);World authored;auto original=Node(authored,{9,8,7});auto before=SceneSerializer::CaptureWorld(authored);
    auto a=PrepareRuntimeLevel(authored,f.project.get(),"Scenes/A.json");Check(a.world->IsAlive(a.player),"mode spawns player before Lua");auto old=a.player;
    std::string pending;unsigned requests=0;scripting::GameplayCamera camera;scripting::GameplayServices host;host.camera=&camera;
    host.loadScene=[&](const std::string& key){f.project->ResolveScene(key);++requests;if(!pending.empty())return false;pending=key;return true;};
    scripting::LuaRuntime lua;lua.Start(*a.world,{},host);lua.Update(0);Check(lua.Error("scripts/player.lua").empty(),lua.Error("scripts/player.lua"));
    auto actor=Tagged(*a.world,"Travel");a.world->GetComponent<TransformComponent>(actor)->position.x=7;lua.Update(0);Check(pending=="Scenes/B.json"&&requests==2&&a.world->IsAlive(old),"Lua request deferred, first wins");
    auto b=PrepareRuntimeLevel(authored,f.project.get(),pending);Check(!b.world->IsAlive(old)&&b.player.world!=old.world,"new world rejects old handles");
    a.world->GetComponent<TransformComponent>(actor)->position.x=10;lua.Update(0); // Leave an event queued in the old runtime.
    lua.Stop();Check(a.world->WorldTransform(actor).position.y==18&&requests==2,"old on_stop then on_destroy before replacement, travel forbidden during retirement");pending.clear();requests=0;lua.Start(*b.world,{},host);lua.Update(101);Check(pending.empty()&&lua.Error("scripts/travel.lua").empty(),"old timers and events cleared");
    Check(b.world->WorldTransform(b.player).position.x==2,"new PlayerStart used");
    lua.Stop();auto again=PrepareRuntimeLevel(authored,f.project.get(),"Scenes/A.json");Check(again.player.world!=b.player.world,"B to A fresh generation context");
    for(const auto* key:{"Scenes/Missing.json","Scenes/Corrupt.json","../outside.json","C:/outside.json","Assets/scene.json","Scenes/BadPlayer.json"}){
        bool failed=false;try{auto invalid=PrepareRuntimeLevel(authored,f.project.get(),key);}catch(const std::exception&){failed=true;}Check(failed&&again.world->IsAlive(again.player),"preparation failure retains active level");
    }
    auto disabled=std::make_shared<ProjectContext>(*f.project);disabled->maps.modes["Default"].autoSpawnPlayer=false;auto off=PrepareRuntimeLevel(authored,disabled.get(),"Scenes/A.json");Check(!off.player.IsValid(),"Auto Spawn disabled");
    auto empty=PrepareRuntimeLevel(authored,f.project.get(),"Scenes/Empty.json");Check(!empty.player.IsValid(),"no marker means no player");
    auto legacy=PrepareRuntimeLevel(authored,f.project.get(),"Scenes/Legacy.json");Check(legacy.world->HasComponent<PlayerComponent>(legacy.player)&&!legacy.prefabSpawn,"legacy spawn unchanged");
    disabled->maps.defaultGameMode="Missing";bool failed=false;try{auto bad=PrepareRuntimeLevel(authored,disabled.get(),"Scenes/A.json");}catch(const std::exception&){failed=true;}Check(failed,"missing mode rejected");
    PhysicsSystem physics(nullptr,9.81f,.985f,3,.05f,.85f,12);physics.Update(*again.world,1.f/60);Check(physics.GetStatistics().bodies>0,"old physics populated");physics.ResetState();Check(physics.GetStatistics().bodies==0&&physics.GetStatistics().contacts==0,"physics transition reset");physics.Update(*empty.world,0);Check(physics.GetStatistics().activeBodies==0,"no old moving bodies in markerless level");
    Check(authored.IsAlive(original)&&SceneSerializer::CaptureWorld(authored)==before,"authoring untouched by travel preparation");
    auto clone=PrepareRuntimeLevel(authored,f.project.get());Check(clone.mapping.contains(original),"ordinary Play still clones authored World");
    World retired;auto retiring=Node(retired);retired.AddComponent<LuaScriptComponent>(retiring).scriptPath="scripts/stop-request.lua";bool stopReachedHost=false;scripting::GameplayServices stopHost;stopHost.loadScene=[&](const std::string&){stopReachedHost=true;return true;};lua.Start(retired,{},stopHost);lua.Update(0);lua.Stop();Check(!stopReachedHost&&retired.WorldTransform(retiring).position.y==18,"Stop travel rejected before host; destroy still runs in old World");
    World noHost;auto e=Node(noHost);noHost.AddComponent<LuaScriptComponent>(e).scriptPath="scripts/travel.lua";noHost.GetComponent<TransformComponent>(e)->position.x=1;lua.Start(noHost);lua.Update(0);Check(lua.Error("scripts/travel.lua").find("active Play")!=std::string::npos,"travel unavailable without Play host");lua.Stop();
}
#if defined(ENABLE_DX12)
static void Native(Files& f){
    Application app;Check(app.Initialize(),"DX12 initialize");SyntheticEditorInput input;glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());std::string error;
    Check(app.OpenProject(f.project->descriptorPath,Application::ProjectDecision::Discard,&error),error);Check(app.OpenScene("Scenes/A.json",&error),error);
    auto& editor=app.GetEditorLayer();editor.SetFixedDiagnosticLayout(true);editor.SetDiagnosticViewport(360,240);app.SetViewportUnlit(true);
    auto frame=[&](unsigned n=4){app.SetFrameLimit(n);Check(app.Run()==0,"native frames");};frame(12);
    auto authoredActor=Tagged(app.GetWorld(),"Travel");Check(editor.SetProperty(app,authoredActor,ComponentId("Transform"),"position",Vec3{0,1,0}),editor.GetError());
    auto authored=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());auto sceneBytes=Bytes(f.project->ResolveScene("Scenes/A.json")),prefabBytes=Bytes(f.project->ResolveAsset("prefabs/player.prefab.json"));
    app.SetEditorPlayMode(true);app.SetEditorPaused(true);app.GetLuaRuntime().Update(0);frame(18);
    auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(gpu!=nullptr,"DX12 adapter");auto imageA=gpu->ReadBackHdrPixels();
    auto trigger=[&](int phase){auto actor=Tagged(app.GetWorld(),"Travel");Check(actor.IsValid(),"travel behaviour alive");auto id=app.GetWorld().GetIdentity();app.GetWorld().GetComponent<TransformComponent>(actor)->position.x=float(phase);app.GetLuaRuntime().Update(0);Check(app.GetWorld().GetIdentity()==id,"request never replaces World in callback");auto diagnostic=app.GetLuaRuntime().Error("scripts/travel.lua");Check(phase==6?!diagnostic.empty():diagnostic.empty(),"expected Lua diagnostic state: "+diagnostic);return id;};
    auto checkB=[&](std::uint64_t old){Check(app.IsEditorPlayMode()&&app.IsEditorPaused()&&app.GetWorld().GetIdentity()!=old,"travel keeps Play and pause");Check(app.GetLevelTravelError().empty(),app.GetLevelTravelError());Check(app.GetRenderCameraState().entity.world==app.GetWorld().GetIdentity(),"no old camera state");auto attachment=Tagged(app.GetWorld(),"Attachment");Check(CameraAttachmentPose(app.GetWorld(),attachment,app.GetRenderCameraState()).has_value(),"new attachment uses new render camera");};
    auto canvas=app.GetRuntimeUI().Get(Tagged(app.GetWorld(),"Canvas"));Check(app.GetRuntimeUI().Valid(canvas),"old Runtime UI live");auto oldCamera=app.GetRenderCameraState().entity;bool oldMutationRan=false;app.GetMutationQueue().Enqueue(app.GetWorld().GetIdentity(),[&](World&){oldMutationRan=true;});auto id=trigger(1);frame(18);Check(!oldMutationRan,"pending old-world application mutation discarded");checkB(id);Check(!app.GetRuntimeUI().Valid(canvas)&&app.GetRuntimeUI().Instances().empty(),"old UI handles and instances cleared");Check(app.GetRenderCameraState().projection[15]==1,"new orthographic camera");Check(!app.GetWorld().IsAlive(oldCamera),"old camera handle invalid");auto imageB=gpu->ReadBackHdrPixels();Check(imageA.size()==imageB.size()&&!imageA.empty(),"HDR readback");double difference=0;for(size_t i=0;i<imageA.size();i+=4)difference+=std::abs(imageA[i]-imageB[i]);Check(difference>1,"real image changed on travel");
    id=trigger(2);frame(8);Check(app.GetWorld().GetIdentity()!=id,"B to A");
    for(int phase:{3,4,5}){id=trigger(phase);auto camera=app.GetRenderCameraState().entity;frame();Check(app.GetWorld().GetIdentity()==id&&app.IsEditorPlayMode()&&!app.GetLevelTravelError().empty()&&app.GetRenderCameraState().entity==camera,"failed load retains level and camera with diagnostic");}
    id=trigger(6);frame();Check(app.GetWorld().GetIdentity()==id,"unsafe path rejected without publication");app.SetEditorPlayMode(false);app.SetEditorPlayMode(true);app.SetEditorPaused(true);app.GetLuaRuntime().Update(0);frame();
    id=trigger(7);frame();checkB(id);id=trigger(2);frame();
    id=trigger(8);frame();checkB(id);id=trigger(2);frame();
    id=trigger(9);app.GetLuaRuntime().Update(.02f);frame();checkB(id);id=trigger(2);frame();
    id=trigger(10);app.GetLuaRuntime().Update(0);frame();checkB(id);
    id=trigger(11);frame();Check(app.GetWorld().GetIdentity()!=id&&!app.GetRenderCameraState().entity.IsValid(),"travel to legacy camera");
    id=trigger(12);frame();Check(app.GetWorld().GetIdentity()!=id&&!app.GetRenderCameraState().entity.IsValid(),"travel without PlayerStart");Check(app.GetPhysicsSystem()->GetStatistics().activeBodies==0&&app.GetActiveCollisionCount()==0,"markerless level has no old dynamic bodies or contacts");
    Check(!app.SaveSceneAs("Scenes/A.json",true,&error),"saving Runtime World prohibited");
    app.SetEditorPlayMode(false);frame();Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==authored,"Stop restores exact authoring scene and settings");editor.UndoEdit(app);Check(app.GetWorld().GetComponent<TransformComponent>(authoredActor)->position.y==0,"authoring Undo preserved");editor.RedoEdit(app);
    app.SetEditorPlayMode(true);app.SetEditorPaused(true);app.GetLuaRuntime().Update(0);id=trigger(1);app.SetEditorPlayMode(false);app.SetEditorPlayMode(true);app.SetEditorPaused(true);app.GetLuaRuntime().Update(0);id=app.GetWorld().GetIdentity();frame();Check(app.GetWorld().GetIdentity()==id,"Stop clears pending travel on repeated Play");app.SetEditorPaused(false);id=trigger(1);frame();Check(app.IsEditorPlayMode()&&!app.IsEditorPaused()&&app.GetWorld().GetIdentity()!=id,"travel during running simulation");app.SetEditorPlayMode(false);
    Check(Bytes(f.project->ResolveScene("Scenes/A.json"))==sceneBytes&&Bytes(f.project->ResolveAsset("prefabs/player.prefab.json"))==prefabBytes,"source scene and prefab bytes intact");
    Check(gpu->GetValidationErrorCount()==0,"zero DX12 validation errors");app.SetEditorPlayMode(true);app.SetEditorPaused(true);app.GetLuaRuntime().Update(0);trigger(1);Check(app.CloseProject(Application::ProjectDecision::Discard,&error),error);Check(app.OpenProject(f.project->descriptorPath,Application::ProjectDecision::Discard,&error),error);frame();Check(!app.IsEditorPlayMode()&&!app.GetActiveCamera().IsValid()&&app.GetLevelTravelError().empty(),"project switch clears pending travel and handles");Check(app.CloseProject(Application::ProjectDecision::Discard,&error),error);app.Shutdown();
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{Files files;
#if defined(ENABLE_DX12)
    if(argc>1&&std::string(argv[1])=="--dx12")Native(files);else
#endif
    Cpu(files);std::cout<<"Level Travel: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Level Travel failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
