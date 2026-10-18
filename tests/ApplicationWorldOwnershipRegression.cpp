#define NOMINMAX
#include "EditorTestPreferences.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "ecs/ComponentRegistry.h"
#include "ecs/Hierarchy.h"
#include "ecs/TransformAccess.h"
#include "ecs/PhysicsQueries.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "ecs/components/TagComponent.h"
#include "ecs/components/LuaScriptComponent.h"
#include "ecs/components/LightComponent.h"
#include "ecs/PlayerSpawn.h"
#include "scene/SceneSerializer.h"
#include "resources/ResourceManager.h"
#include "resources/MeshResource.h"
#include <fstream>
#include <iostream>
#include <limits>
#include <source_location>
#ifdef ENABLE_DX12
#include "SyntheticEditorInput.h"
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <GLFW/glfw3.h>
#endif

using namespace ecs;
using Json=nlohmann::json;
namespace fs=std::filesystem;
static unsigned checks=0;
static void Check(bool ok,const std::string& text,std::source_location at=std::source_location::current()){
    if(!ok)throw std::runtime_error(text+" at "+std::to_string(at.line()));++checks;
}
struct ApplicationTestAccess {
    static auto Prepare(Application& app,const fs::path& path,std::string& error){return app.PrepareAuthoringScene(path,&error);}
    static auto Empty(Application& app,World::DataMode mode,std::string& error){return app.PrepareEmptyAuthoring(mode,SceneSettings{},&error);}
    static bool Publish(Application& app,auto& candidate,std::string& error){return app.PublishAuthoringWorld(candidate,&error);}
    static auto Runtime(Application& app,std::string& error){return app.PrepareRuntimeWorld(&error);}
    static World& Systems(Application& app){return app.m_SystemWorld;}
    static RenderSystem* Render(Application& app){return app.m_RenderSystem;}
    static void QueueDestroy(Application& app,Entity entity){app.m_Mutations.Destroy(entity);}
    static void FrameSystems(Application& app){app.UpdateWorldSystems(false,0);}
    static void FixedSystems(Application& app){app.UpdateWorldSystems(true,float(FixedStepClock::TickSeconds));}
};
static Entity Object(World& world,const std::string& id,Vec3 position={}){
    auto e=world.CreateEntity();world.AddComponent<SceneIdentityComponent>(e).value=id;
    world.AddComponent<TransformComponent>(e).position=position;world.AddComponent<TagComponent>(e).name=id;return e;
}
static Entity Find(const World& world,const std::string& id){for(auto e:world.Entities())if(SceneSerializer::EntitySceneId(world,e)==id)return e;return {};}
static Json Raw(const World& world){Json value={{"identity",world.GetIdentity()},{"capacity",world.GetCapacity()},{"entities",Json::array()}};
    for(auto e:world.Entities())value["entities"].push_back({{"index",e.index},{"generation",e.generation},{"components",ComponentRegistry::Builtins().Capture(world,e)}});return value;}
static Json Session(Application& app){return {{"author",Raw(app.GetEditorWorld())},{"active",Raw(app.GetWorld())},{"settings",SceneSettingsToJson(app.GetSceneSettings())},{"scene",app.GetSceneKey()},{"project",app.GetProject()?AssetPaths::ToUtf8String(app.GetProject()->root):""},{"selection",[&]{Json j=Json::array();for(auto e:app.GetEditorLayer().GetSelectedEntities())j.push_back({e.index,e.generation,e.world});return j;}()},{"undo",app.GetEditorLayer().UndoCount()},{"dirty",app.GetEditorLayer().IsSceneDirty()},{"play",app.IsEditorPlayMode()},{"paused",app.IsEditorPaused()},{"lua",app.GetLuaRuntime().InstanceCount()}};}
static Json Read(const fs::path& path){std::ifstream in(path);Json j;in>>j;return j;}
static void Write(const fs::path& path,const Json& j){std::string error;Check(SceneSerializer::WriteAtomic(path,j,&error),"write owned fixture: "+error);}
struct Fixture {
    fs::path root=fs::current_path()/("application-ownership-"+NewSceneIdentity());
    fs::path flat=root/"root.json",hierarchy=root/"hierarchy.json";
    Fixture(){Check(fs::create_directory(root),"unique fixture");
        World w;auto e=Object(w,"root",{0,0,0});w.GetComponent<TransformComponent>(e)->scale={2,3,4};
        w.AddComponent<ColliderComponent>(e).offset={1,0,0};w.GetComponent<ColliderComponent>(e)->autoFitFromMesh=false;w.AddComponent<RigidbodyComponent>(e).useGravity=false;
        std::string error;Check(SceneSerializer::SaveWorld(flat,w,&error),error);
        World h(World::DataMode::HierarchyData);auto p=Object(h,"parent",{3,0,0}),c=Object(h,"child",{4,0,0});
        Check(SetParent(h,c,p),"fixture relationship");h.GetComponent<RelationshipComponent>(c)->order=7;
        h.GetComponent<TransformComponent>(p)->scale={2,1,1};h.GetComponent<TransformComponent>(c)->rotation.z=.7f;
        auto start=Object(h,"marker",{0,3,0});h.AddComponent<PlayerStartComponent>(start).controllerScript.clear();
        Check(SceneSerializer::SaveWorld(hierarchy,h,&error),error);
    }
    ~Fixture(){ProjectManager::Activate({});std::error_code error;fs::remove_all(root,error);}
};
struct Probe:ISystem {
    unsigned& calls;unsigned& destroyed;std::uint64_t& last;
    Application* reenter=nullptr;bool refused=false,stop=false;
    Probe(unsigned& c,unsigned& d,std::uint64_t& l):calls(c),destroyed(d),last(l){}
    ~Probe(){++destroyed;}
    const char* Name()const override{return "OwnershipProbe";}
    void Update(World& world,float)override{++calls;last=world.GetIdentity();if(reenter){if(stop)reenter->SetEditorPlayMode(false);else try{reenter->NewScene();}catch(const std::runtime_error&){refused=true;}}}
};
static void Ownership(Fixture& fixture){
    unsigned calls=0,destroyed=0;std::uint64_t last=0;
    {
        Application app;std::string error;auto& ui=app.GetEditorLayer();
        auto old=ui.CreateEntity(app,"old");ui.SelectEntity(old);ui.MarkDirty();const auto before=Session(app);
        auto* probe=&ApplicationTestAccess::Systems(app).AddSystem<Probe>(calls,destroyed,last);
        for(int repeat=0;repeat<3;++repeat){
            auto root=ApplicationTestAccess::Prepare(app,fixture.flat,error);auto h=ApplicationTestAccess::Prepare(app,fixture.hierarchy,error);
            Check(root&&h,"prepare both modes: "+error);Check(!root->world->IsHierarchyData()&&h->world->IsHierarchyData(),"immutable mode chosen from persisted marker");
            Check(Session(app)==before,"preparation leaves complete session untouched");
            const auto& relation=*h->world->GetComponent<RelationshipComponent>(Find(*h->world,"child"));Check(relation.parent=="parent"&&relation.order==7,"candidate stable parent/order");
            Check(!ReadWorldTransform(*h->world,Find(*h->world,"child")),"full affine hierarchy candidate retained");
            bool threw=false;try{root->world->ReplaceData(*h->world);}catch(const std::logic_error&){threw=true;}
            Check(threw&&!root->world->IsHierarchyData()&&Session(app)==before,"cross-mode ReplaceData remains guarded");
        }
        auto h=ApplicationTestAccess::Prepare(app,fixture.hierarchy,error);const auto preparedId=h->world->GetIdentity();
        Check(ApplicationTestAccess::Publish(app,h,error)&&!h,"publish validated hierarchy through real internal Application API: "+error);
        Check(app.GetEditorWorld().IsHierarchyData()&&app.GetEditorWorld().GetIdentity()==preparedId&&!app.GetWorld().IsAlive(old),"pointer publication preserves candidate identity and fences old handles");
        Check(ui.GetSelectedEntities().empty()&&ui.UndoCount()==0&&!ui.IsSceneDirty(),"selection active/anchor history drafts reset");
        probe->reenter=&app;ApplicationTestAccess::FrameSystems(app);Check(calls==1&&last==preparedId&&destroyed==0&&probe->refused&&app.GetWorld().GetIdentity()==preparedId,"system survives and reentrant publication outside ForEach is refused");probe->reenter=nullptr;
        const auto author=SceneSerializer::CaptureWorld(app.GetEditorWorld());auto runtime=ApplicationTestAccess::Runtime(app,error);
        Check(runtime&&runtime->world->IsHierarchyData()&&runtime->world->IsAlive(runtime->player),"real runtime preparation supports hierarchy clone/player validation");
        Check(!runtime->world->IsAlive(Find(app.GetEditorWorld(),"parent"))&&SceneSerializer::CaptureWorld(app.GetEditorWorld())==author,"runtime candidate data/handles isolated");
        Check(runtime->world->GetComponent<RelationshipComponent>(Find(*runtime->world,"child"))->parent=="parent","runtime candidate preserves graph");
        const auto session=Session(app);app.SetEditorPlayMode(true);Check(Session(app)==session,"public hierarchy Play remains closed before A3");
        auto invalid=ApplicationTestAccess::Prepare(app,fixture.hierarchy,error);invalid->world->GetComponent<RelationshipComponent>(Find(*invalid->world,"child"))->parent="missing";
        Check(!ApplicationTestAccess::Publish(app,invalid,error)&&error.find("preflight")!=std::string::npos&&Session(app)==session,"candidate revalidation failure before commit preserves session");
        auto noId=ApplicationTestAccess::Prepare(app,fixture.flat,error);noId->world->RemoveComponent<SceneIdentityComponent>(Find(*noId->world,"root"));
        Check(!ApplicationTestAccess::Publish(app,noId,error)&&Session(app)==session,"publication requires persisted identities, not synthesized fallback IDs");
        auto drivenSun=ApplicationTestAccess::Prepare(app,fixture.hierarchy,error);drivenSun->world->AddComponent<DirectionalLightComponent>(Find(*drivenSun->world,"parent"));drivenSun->settings.background.sunSync=SunSyncMode::SkyDrivesLight;drivenSun->settings.background.sunMetadata=true;
        Check(!ApplicationTestAccess::Publish(app,drivenSun,error)&&error.find("A3")!=std::string::npos&&Session(app)==session,"RootOnly sun authoring adapter is not silently applied to hierarchy before A3");
        auto unsupported=Read(fixture.hierarchy);for(auto& node:unsupported["entities"])if(node["id"]=="marker")node["components"]["Relationship"]={{"parent","parent"},{"order",9}};
        Write(fixture.root/"unsupported.json",unsupported);Check(!ApplicationTestAccess::Prepare(app,fixture.root/"unsupported.json",error)&&error.find("PlayerStart")!=std::string::npos&&Session(app)==session,"runtime restrictions checked during authoring preflight before publication");
        app.GetWorld().ForEach<TransformComponent>([&](auto,const auto&){auto empty=ApplicationTestAccess::Empty(app,World::DataMode::RootOnly,error);Check(!empty&&Session(app)==session,"phase-boundary refusal before candidate publication");});
        ApplicationTestAccess::QueueDestroy(app,Find(app.GetWorld(),"parent"));
        app.NewScene();app.ApplyStructuralCommands();Check(!app.GetWorld().IsHierarchyData()&&app.GetWorld().GetAliveCount()==0,"explicit New Scene creates RootOnly; old mutation commands drained");
        auto root=ApplicationTestAccess::Prepare(app,fixture.flat,error);Check(ApplicationTestAccess::Publish(app,root,error),error);
        auto hit=Raycast(app.GetWorld(),{-10,0,0},{1,0,0},30);Check(hit&&std::abs(hit->distance-10.5f)<1e-5f,"flat collider world dimensions/offset unchanged");
        Check(SceneSerializer::CaptureWorld(app.GetWorld())["entities"]==Read(fixture.flat)["entities"],"root components and TRS unchanged");
        ApplicationTestAccess::FrameSystems(app);Check(calls==2&&last==app.GetWorld().GetIdentity()&&destroyed==0,"no re-registration or premature destruction");
        auto oldDocument=Read(fixture.flat);oldDocument.erase("worldMode");Write(fixture.root/"old.json",oldDocument);
        auto legacy=ApplicationTestAccess::Prepare(app,fixture.root/"old.json",error);Check(legacy&&!legacy->world->IsHierarchyData(),"old v4 RootOnly candidate");
        std::vector<EcsDemoEntityConfig> legacyConfigs(1);legacyConfigs[0].position={1,2,3};Check(SceneSerializer::SaveEntityConfigs(fixture.root/"v3.json",legacyConfigs,&error),error);
        auto v3=ApplicationTestAccess::Prepare(app,fixture.root/"v3.json",error);Check(v3&&!v3->world->IsHierarchyData()&&v3->world->GetAliveCount()==1,"legacy candidate without conversion");
    }
    Check(destroyed==1,"Application destroys registered systems exactly once");
}
static void ProjectsAndRuntime(Fixture& fixture){
    unsigned calls=0,destroyed=0;std::uint64_t last=0;
    auto a=ProjectManager::Create(fixture.root,"A"),b=ProjectManager::Create(fixture.root,"B");std::string error;Application app;auto& ui=app.GetEditorLayer();using Decision=Application::ProjectDecision;
    auto old=Read(a->ResolveScene(a->startupScene));old.erase("worldMode");Write(a->ResolveScene(a->startupScene),old);
    Check(app.OpenProject(a->descriptorPath,Decision::Discard,&error),error);Check(!app.GetEditorWorld().IsHierarchyData(),"old project startup remains RootOnly");
    auto actor=ui.CreateEntity(app,"actor");ui.SelectEntity(actor);ui.MarkDirty();auto before=Session(app);
    {std::ofstream script(a->ResolveAsset("scripts/ownership.lua"));script<<"return {on_start=function(self) World.set_position(self.entity,Vec3.new(9,0,0)) end,on_stop=function(self) World.set_position(self.entity,Vec3.new(-7,0,0)) end}";Check(bool(script),"owned lifecycle Lua fixture");}
    app.GetWorld().AddComponent<LuaScriptComponent>(actor).scriptPath="scripts/ownership.lua";before=Session(app);
    Write(a->ResolveScene("Scenes/Hierarchy.json"),Read(fixture.hierarchy));
    Check(!app.OpenScene("Scenes/Hierarchy.json",&error)&&error.find("worldMode mismatch")!=std::string::npos&&Session(app)==before,"public hierarchy OpenScene rejection preserves authoring session");
    Check(!app.SetStartupScene("Scenes/Hierarchy.json",&error)&&Session(app)==before,"hierarchy Startup Scene remains closed");
    auto invalid=Read(fixture.flat);invalid["worldMode"]=42;Write(a->ResolveScene("Scenes/Bad.json"),invalid);
    Check(!app.OpenScene("Scenes/Bad.json",&error)&&error.find("preflight")!=std::string::npos&&Session(app)==before,"invalid marker fails before commit");
    auto marker=ui.CreatePlayerStart(app);app.GetWorld().GetComponent<PlayerStartComponent>(marker)->controllerScript.clear();
    for(int repeat=0;repeat<3;++repeat){
        auto author=SceneSerializer::CaptureWorld(app.GetEditorWorld());auto runtime=ApplicationTestAccess::Runtime(app,error);
        Check(runtime&&runtime->world->IsAlive(runtime->player)&&!app.IsEditorPlayMode()&&SceneSerializer::CaptureWorld(app.GetEditorWorld())==author,"RootOnly runtime preparation does not publish");
        app.SetEditorPlayMode(true);Check(app.IsEditorPlayMode()&&!app.GetWorld().IsAlive(actor)&&app.GetWorld().IsAlive(app.MapToRuntime(actor)),"actual Play isolated runtime");
        app.GetLuaRuntime().Update(0);Check(app.GetLuaRuntime().InstanceCount()==1&&app.GetLuaRuntime().Error("scripts/ownership.lua").empty()&&ReadWorldPosition(app.GetWorld(),app.MapToRuntime(actor))->x==9&&ReadWorldPosition(app.GetEditorWorld(),actor)->x==0,"real Lua ownership/start is isolated to active runtime");
        app.SetEditorPaused(true);const auto paused=Session(app);
        const auto beforeCalls=calls;
        auto* stopping=&ApplicationTestAccess::Systems(app).AddSystem<Probe>(calls,destroyed,last);stopping->stop=true;stopping->reenter=&app;
        ApplicationTestAccess::FrameSystems(app);stopping->reenter=nullptr;
        Check(Session(app)==paused&&calls>beforeCalls,"Stop from an executing system is refused before runtime destruction");
        Check(!app.OpenScene("Scenes/Bad.json",&error)&&Session(app)==paused,"failed load preserves active paused runtime, selection, Lua and dirty state");
        Write(b->ResolveScene(b->startupScene),Read(fixture.hierarchy));Check(!app.OpenProject(b->descriptorPath,Decision::Discard,&error)&&Session(app)==paused,"failed project preflight preserves active project/runtime");
        auto invalidCandidate=ApplicationTestAccess::Prepare(app,fixture.hierarchy,error);invalidCandidate->world->GetComponent<TransformComponent>(Find(*invalidCandidate->world,"parent"))->scale={0,1,1};
        Check(!ApplicationTestAccess::Publish(app,invalidCandidate,error)&&Session(app)==paused,"invalid publication does not stop current Lua/runtime");
        auto* t=app.GetEditorWorld().GetComponent<TransformComponent>(marker);const auto saved=*t;t->position.x=std::numeric_limits<float>::quiet_NaN();
        auto authorMalformed=Raw(app.GetEditorWorld()).dump();auto active=Raw(app.GetWorld());Check(!ApplicationTestAccess::Runtime(app,error)&&std::isnan(t->position.x)&&Raw(app.GetEditorWorld()).dump()==authorMalformed&&Raw(app.GetWorld())==active&&app.IsEditorPaused(),"SpawnPlayer candidate failure leaves current active runtime intact");*t=saved;
        app.SetEditorPaused(false);Check(app.IsSimulationRunning(),"Resume retains runtime state");app.SetEditorPlayMode(false);
        Check(SceneSerializer::CaptureWorld(app.GetEditorWorld())==author&&app.GetLuaRuntime().InstanceCount()==0&&ui.GetSelectedEntity()==marker,"Stop preserves authoring/history selection and retires Lua ownership");
    }
    {auto* t=app.GetEditorWorld().GetComponent<TransformComponent>(marker);const auto saved=*t;t->position.x=std::numeric_limits<float>::quiet_NaN();const auto beforeFailure=Session(app).dump();bool threw=false;try{app.SetEditorPlayMode(true);}catch(const std::runtime_error& e){threw=std::string(e.what()).find("preflight")!=std::string::npos;}Check(threw&&Session(app).dump()==beforeFailure&&!app.IsEditorPlayMode()&&app.GetLuaRuntime().InstanceCount()==0,"public Play preparation failure occurs before history/camera/Lua/session commit");*t=saved;}
    Check(app.SaveCurrentScene(&error),error);auto savedKey=app.GetSceneKey();Check(Read(a->ResolveScene(savedKey))["worldMode"]=="rootOnly","Application Save marker retained");
    Check(app.OpenScene(savedKey,&error)&&!app.GetWorld().IsAlive(actor)&&ui.UndoCount()==0&&ui.GetSelectedEntities().empty(),"OpenScene invalidates handles and editor history");
    World empty;Check(SceneSerializer::SaveWorld(b->ResolveScene(b->startupScene),empty,&error),error);
    for(int repeat=0;repeat<2;++repeat){auto entity=app.CreateEmptyEntity("transition");Check(app.OpenProject(b->descriptorPath,Decision::Discard,&error)&&!app.GetWorld().IsAlive(entity),error);Check(app.OpenProject(a->descriptorPath,Decision::Discard,&error)&&app.GetProject()->root==a->root,error);}
    Check(app.CloseProject(Decision::Discard,&error)&&app.IsProjectBrowserActive()&&!app.GetProject(),"Close returns RootOnly browser");
    Check(app.OpenLegacyWorkspace(&error)&&!app.GetWorld().IsHierarchyData()&&!app.IsProjectBrowserActive(),"explicit Legacy Workspace preserved");
}
#ifdef ENABLE_DX12
static void Native(Fixture& fixture){
    unsigned calls=0,destroyed=0;std::uint64_t last=0;
    Application app;Check(app.Initialize(),"DX12 Application Initialize");SyntheticEditorInput input;
    auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle();glfwHideWindow(window);app.NewScene();
    auto* physics=app.GetPhysicsSystem();auto* render=ApplicationTestAccess::Render(app);auto* manager=app.GetResourceManager();auto* adapter=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());
    ApplicationTestAccess::Systems(app).AddSystem<Probe>(calls,destroyed,last);
    auto mesh=manager->Load<MeshResource>("models/validation_cube.obj");manager->DrainProjectLoads();Check(mesh&&mesh->IsLoaded(),"real ResourceManager cache after async finalization");
    auto e=app.GetEditorLayer().CreatePrimitive(app,"cube");app.GetWorld().GetComponent<TransformComponent>(e)->position={0,0,4};app.SetEditorCameraPose({0,0,-4},0,0);
    auto frame=[&]{app.SetFrameLimit(8);Check(app.Run()==0,"real Application systems/render lifecycle");};frame();
    std::string error;auto before=Session(app);auto bad=Read(fixture.flat);bad["worldMode"]=false;Write(fixture.root/"bad-native.json",bad);
    Check(!ApplicationTestAccess::Prepare(app,fixture.root/"bad-native.json",error)&&Session(app)==before&&app.GetResourceManager()==manager&&manager->Load<MeshResource>("models/validation_cube.obj")==mesh,"failed preflight preserves ResourceManager and cached resource ownership");
    auto h=ApplicationTestAccess::Prepare(app,fixture.hierarchy,error);Check(ApplicationTestAccess::Publish(app,h,error),error);
    Check(app.GetPhysicsSystem()==physics&&ApplicationTestAccess::Render(app)==render&&app.GetResourceManager()==manager,"real systems remain same instances across hierarchy publication");
    // Exercise the registered Application pipeline on authoring data, not a second
    // runtime harness or hierarchy Play. Full hierarchy Play remains A3-only.
    auto* renderer=app.GetRenderAdapter();renderer->BeginFrame();renderer->Clear(0,0,0,1);render->SetRenderAdapter(renderer);ApplicationTestAccess::FrameSystems(app);render->SetRenderAdapter(nullptr);renderer->EndFrame();renderer->Present();
    Check(last==app.GetEditorWorld().GetIdentity()&&destroyed==0&&app.GetRenderStatistics().visibleObjects==0,"pipeline attached to hierarchy authoring data without stale RootOnly mesh snapshots");
    app.NewScene();e=app.GetEditorLayer().CreatePrimitive(app,"cube");app.GetWorld().GetComponent<TransformComponent>(e)->position={0,0,4};frame();
    Check(app.GetPhysicsSystem()==physics&&ApplicationTestAccess::Render(app)==render&&calls>0&&app.GetRenderStatistics().visibleObjects==1&&manager->Load<MeshResource>("models/validation_cube.obj")==mesh,"RootOnly rendering resumes with original systems/resource caches");
    app.GetWorld().AddComponent<ColliderComponent>(e).autoFitFromMesh=false;auto& body=app.GetWorld().AddComponent<RigidbodyComponent>(e);body.useGravity=false;body.velocity={1,0,0};
    const auto author=SceneSerializer::CaptureWorld(app.GetEditorWorld());app.SetEditorPlayMode(true);frame();ApplicationTestAccess::FixedSystems(app);Check(app.GetWorld().GetComponent<TransformComponent>(app.MapToRuntime(e))->position.x>0,"actual Application fixed Physics uses runtime world");app.SetEditorPaused(true);frame();app.SetEditorPaused(false);app.SetEditorPlayMode(false);
    Check(SceneSerializer::CaptureWorld(app.GetEditorWorld())==author,"native Play/Stop author isolation after replacements");
    Check(adapter->GetValidationErrorCount()==0,"DX12 ownership validation zero");app.Shutdown();Check(destroyed==1,"Shutdown destroys additional system once");
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{Fixture fixture;
#ifdef ENABLE_DX12
    if(argc>1&&std::string(argv[1])=="--dx12")Native(fixture);else
#endif
    {Ownership(fixture);ProjectsAndRuntime(fixture);}std::cout<<"Application world ownership: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
