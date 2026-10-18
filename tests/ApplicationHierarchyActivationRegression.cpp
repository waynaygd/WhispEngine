#define NOMINMAX
#include "EditorTestPreferences.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "scene/SceneSerializer.h"
#include "ecs/ComponentRegistry.h"
#include "ecs/HierarchyPhysics.h"
#include "ecs/TransformAccess.h"
#include "ecs/PhysicsQueries.h"
#include "ecs/components/LuaScriptComponent.h"
#include "ecs/components/MeshRendererComponent.h"
#include "ecs/components/MaterialComponent.h"
#include "ecs/components/TagComponent.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "editor/SunSynchronization.h"
#include "resources/ResourceManager.h"
#include "resources/MeshResource.h"
#include "resources/MaterialResource.h"
#include "resources/ShaderResource.h"
#include <fstream>
#include <iostream>
#include <source_location>
#include <iterator>
#ifdef ENABLE_DX12
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#endif
using namespace ecs;
using Json=nlohmann::json;
namespace fs=std::filesystem;
static unsigned checks=0;
static void Check(bool ok,const std::string& message,std::source_location at=std::source_location::current()){
    if(!ok)throw std::runtime_error(message+" at "+std::to_string(at.line()));++checks;
}
struct ApplicationTestAccess {
    static bool Open(Application& app,const std::string& key,std::string& error){return app.OpenSceneInternal(key,true,&error);}
    static bool New(Application& app,World::DataMode mode,std::string& error){return app.NewSceneInternal(mode,&error);}
    static bool Project(Application& app,const fs::path& path,std::string& error){return app.OpenProjectInternal(path,Application::ProjectDecision::Discard,true,&error);}
    static bool Startup(Application& app,const std::string& key,std::string& error){return app.SaveProjectSettingsInternal(key,app.GetProject()->settings,true,&error);}
    static auto Upgrade(Application& app,std::string& error){return app.PrepareHierarchyUpgrade(&error);}
    static bool Publish(Application& app,auto& candidate,std::string& error){return app.PublishAuthoringWorld(candidate,&error);}
    static auto Runtime(Application& app,std::string& error){return app.PrepareRuntimeWorld(&error);}
};
static Entity Object(World& world,const std::string& id,Vec3 p={}){
    auto e=world.CreateEntity();world.AddComponent<SceneIdentityComponent>(e).value=id;
    world.AddComponent<TransformComponent>(e).position=p;world.AddComponent<TagComponent>(e).name=id;return e;
}
static Entity Find(const World& world,const std::string& id){for(auto e:world.Entities())if(SceneSerializer::EntitySceneId(world,e)==id)return e;return {};}
static Json Read(const fs::path& path){std::ifstream file(path);Json value;file>>value;return value;}
static void Write(const fs::path& path,const Json& j){std::string error;Check(SceneSerializer::WriteAtomic(path,j,&error),error);}
static Json Session(Application& app){Json selection=Json::array();for(auto e:app.GetEditorLayer().GetSelectedEntities())selection.push_back({e.index,e.generation,e.world});
    return {{"author",SceneSerializer::CaptureWorld(app.GetEditorWorld(),app.GetSceneSettings())},{"authorIdentity",app.GetEditorWorld().GetIdentity()},{"active",SceneSerializer::CaptureWorld(app.GetWorld())},{"activeIdentity",app.GetWorld().GetIdentity()},{"settings",SceneSettingsToJson(app.GetSceneSettings())},{"scene",app.GetSceneKey()},{"project",app.GetProject()?AssetPaths::ToUtf8String(app.GetProject()->root):""},{"selection",selection},{"dirty",app.GetEditorLayer().IsSceneDirty()},{"undo",app.GetEditorLayer().UndoCount()},{"play",app.IsEditorPlayMode()},{"paused",app.IsEditorPaused()},{"lua",app.GetLuaRuntime().InstanceCount()}};
}
struct Fixture {
    fs::path root=fs::current_path()/("hierarchy-activation-"+NewSceneIdentity());
    std::shared_ptr<const ProjectContext> a,b;
    Fixture(){Check(fs::create_directory(root),"unique fixture");a=ProjectManager::Create(root,"Root");b=ProjectManager::Create(root,"Hierarchy");
        World hierarchy(World::DataMode::HierarchyData);auto p=Object(hierarchy,"parent",{0,0,4}),c=Object(hierarchy,"child",{1,0,4});Check(SetParent(hierarchy,c,p),"fixture parent");
        hierarchy.GetComponent<RelationshipComponent>(c)->order=9;std::string error;
        Check(SceneSerializer::SaveWorld(a->ResolveScene("Scenes/Hierarchy.json"),hierarchy,&error),error);
        Check(SceneSerializer::SaveWorld(b->ResolveScene(b->startupScene),hierarchy,&error),error);
        auto old=Read(a->ResolveScene(a->startupScene));old.erase("worldMode");Write(a->ResolveScene("Scenes/Old.json"),old);
        auto invalid=Read(a->ResolveScene("Scenes/Hierarchy.json"));invalid["worldMode"]=false;Write(a->ResolveScene("Scenes/Bad.json"),invalid);
    }
    ~Fixture(){ProjectManager::Activate({});std::error_code error;fs::remove_all(root,error);}
};
static void Scenes(Fixture& f){Application app;std::string error;using D=Application::ProjectDecision;
    Check(app.OpenProject(f.a->descriptorPath,D::Discard,&error),error);
    Check(ApplicationTestAccess::Open(app,"Scenes/Old.json",error)&&!app.GetWorld().IsHierarchyData(),"old missing marker remains RootOnly");
    Check(ApplicationTestAccess::Open(app,f.a->startupScene,error)&&!app.GetWorld().IsHierarchyData(),"explicit RootOnly");
    auto e=app.GetEditorLayer().CreateEntity(app,"old");app.GetEditorLayer().SelectEntity(e);app.GetEditorLayer().MarkDirty();auto before=Session(app);
    Check(!app.OpenScene("Scenes/Hierarchy.json",&error)&&Session(app)==before,"ordinary UI hierarchy opening stays closed");
    Check(ApplicationTestAccess::Open(app,"Scenes/Hierarchy.json",error),error);
    Check(app.GetWorld().IsHierarchyData()&&!app.GetWorld().IsAlive(e)&&app.GetEditorLayer().GetSelectedEntities().empty()&&app.GetEditorLayer().UndoCount()==0,"controlled mode publication resets handles/history");
    auto child=Find(app.GetWorld(),"child");Check(GetParent(app.GetWorld(),child)==Find(app.GetWorld(),"parent")&&app.GetWorld().GetComponent<RelationshipComponent>(child)->order==9,"stable graph retained");
    auto snapshot=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());
    Check(app.SaveSceneAs("Scenes/Roundtrip.json",false,&error),error);Check(Read(f.a->ResolveScene("Scenes/Roundtrip.json"))==snapshot,"Save As hierarchy marker/components/settings");
    Check(app.SaveCurrentScene(&error),error);Check(ApplicationTestAccess::Open(app,"Scenes/Roundtrip.json",error)&&!app.GetWorld().IsAlive(child),"actual Application reload invalidates prior handles");
    before=Session(app);Check(!ApplicationTestAccess::Open(app,"Scenes/Bad.json",error)&&Session(app)==before,"invalid mode preserves complete session");
    Check(ApplicationTestAccess::Startup(app,"Scenes/Roundtrip.json",error),error);Check(!app.SetStartupScene("Scenes/Hierarchy.json",&error),"public startup gate remains closed");
    Check(ApplicationTestAccess::Project(app,f.b->descriptorPath,error)&&app.GetWorld().IsHierarchyData(),error);
    Check(ApplicationTestAccess::Project(app,f.a->descriptorPath,error)&&app.GetWorld().IsHierarchyData(),"controlled hierarchy startup retained");
    Check(ApplicationTestAccess::Startup(app,"Scenes/Main.json",error),error);
    Check(ApplicationTestAccess::Project(app,f.b->descriptorPath,error)&&ApplicationTestAccess::Project(app,f.a->descriptorPath,error)&&!app.GetWorld().IsHierarchyData(),"A RootOnly to B Hierarchy to A RootOnly");
    auto bad=Read(f.b->ResolveScene(f.b->startupScene));bad["entities"][1]["components"]["Relationship"]["parent"]="absent";Write(f.b->ResolveScene(f.b->startupScene),bad);
    before=Session(app);Check(!ApplicationTestAccess::Project(app,f.b->descriptorPath,error)&&Session(app)==before,"invalid project hierarchy startup refuses before active resources/session change");
    Check(ApplicationTestAccess::New(app,World::DataMode::HierarchyData,error)&&app.GetWorld().IsHierarchyData()&&app.GetWorld().GetAliveCount()==0,"empty hierarchy creation production path");
    Check(app.SaveSceneAs("Scenes/Empty.json",false,&error)&&ApplicationTestAccess::Open(app,"Scenes/Empty.json",error)&&app.GetWorld().IsHierarchyData(),"empty hierarchy roundtrip");
    app.NewScene();Check(!app.GetWorld().IsHierarchyData(),"public New remains RootOnly");
    Check(app.CloseProject(D::Discard,&error)&&app.IsProjectBrowserActive(),error);Check(app.OpenLegacyWorkspace(&error)&&!app.GetWorld().IsHierarchyData(),"Legacy remains explicit RootOnly");
}
static void Upgrades(Fixture& f){Application app;std::string error;
    Check(app.OpenProject(f.a->descriptorPath,Application::ProjectDecision::Discard,&error),error);
    auto empty=ApplicationTestAccess::Upgrade(app,error);Check(empty&&empty->world->IsHierarchyData(),"empty upgrade candidate: "+error);
    auto visual=Object(app.GetWorld(),"visual",{3,4,5});auto* t=app.GetWorld().GetComponent<TransformComponent>(visual);t->scale={2,3,4};t->rotation={.2f,.4f,.6f};
    auto before=Session(app);auto candidate=ApplicationTestAccess::Upgrade(app,error);
    Check(candidate&&Session(app)==before,"visual-only nonunit TRS upgrade preparation preserves session: "+error);
    auto next=Find(*candidate->world,"visual");Check(candidate->world->GetComponent<SceneIdentityComponent>(next)->value=="visual"&&ComposeTRS(*candidate->world->GetComponent<TransformComponent>(next))==ComposeTRS(*t),"upgrade preserves stable identity/components/root world Transform");
    Check(ApplicationTestAccess::Publish(app,candidate,error)&&app.GetWorld().IsHierarchyData()&&!app.GetWorld().IsAlive(visual),"explicit publication is separate from upgrade preparation");
    for(int kind=0;kind<4;++kind){app.NewScene();auto e=Object(app.GetWorld(),"supported");auto& c=app.GetWorld().AddComponent<ColliderComponent>(e);c.autoFitFromMesh=false;c.type=kind==1?ColliderType::Sphere:ColliderType::Box;c.isTrigger=kind==3;c.friction=.37f;c.restitution=.21f;
        auto& b=app.GetWorld().AddComponent<RigidbodyComponent>(e);b.isStatic=kind!=2;b.useGravity=false;b.mass=3;
        app.GetWorld().GetComponent<TransformComponent>(e)->rotation={.2f,.3f,.4f};before=Session(app);candidate=ApplicationTestAccess::Upgrade(app,error);
        Check(candidate&&Session(app)==before,"supported rotated static/dynamic/trigger geometry: "+error);
        auto expected=SceneSerializer::CaptureWorld(app.GetWorld());auto actual=SceneSerializer::CaptureWorld(*candidate->world);actual["worldMode"]="rootOnly";
        Check(actual==expected,"all serialized physics flags/material properties preserved");
    }
    const auto originalFile=Read(f.a->ResolveScene(f.a->startupScene));
    for(int kind=0;kind<7;++kind){app.NewScene();auto e=Object(app.GetWorld(),"incompatible");auto& c=app.GetWorld().AddComponent<ColliderComponent>(e);c.autoFitFromMesh=false;
        if(kind!=3)app.GetWorld().AddComponent<RigidbodyComponent>(e).isStatic=kind!=1;
        auto* local=app.GetWorld().GetComponent<TransformComponent>(e);
        if(kind==0)local->scale={2,2,2};if(kind==1)c.offset={1,0,0};if(kind==2){local->scale={2,1,1};c.type=ColliderType::Sphere;}if(kind==4)local->scale={-1,1,1};
        if(kind==5)local->scale={0,1,1};if(kind==6){c.autoFitFromMesh=true;app.GetWorld().AddComponent<MeshRendererComponent>(e).meshPath="@engine/models/primitives/cube.obj";}
        app.GetEditorLayer().SelectEntity(e);app.GetEditorLayer().MarkDirty();before=Session(app);candidate=ApplicationTestAccess::Upgrade(app,error);
        Check(!candidate&&error.find("incompatible")!=std::string::npos&&error.find("Collider")!=std::string::npos&&Session(app)==before,"incompatible Entity/Collider diagnostic before session change: "+error);
    }
    Check(Read(f.a->ResolveScene(f.a->startupScene))==originalFile,"upgrade never rewrites original scene JSON");
}
static void Suns(Fixture& f){Application app;std::string error;Check(app.OpenProject(f.a->descriptorPath,Application::ProjectDecision::Discard,&error),error);
    Check(ApplicationTestAccess::New(app,World::DataMode::HierarchyData,error),error);
    auto parent=Object(app.GetWorld(),"sun-parent"),sun=Object(app.GetWorld(),"sun");Check(SetParent(app.GetWorld(),sun,parent),"sun parent");
    app.GetWorld().AddComponent<DirectionalLightComponent>(sun).primary=true;
    app.GetWorld().GetComponent<TransformComponent>(parent)->rotation={.1f,.4f,.2f};app.GetWorld().GetComponent<TransformComponent>(parent)->scale={2,2,2};
    auto settings=app.GetSceneSettings();settings.background.sunSync=SunSyncMode::SkyDrivesLight;settings.background.sunMetadata=true;settings.background.sunDirection={.2f,.7f,.4f};
    const auto local=*app.GetWorld().GetComponent<TransformComponent>(sun);
    Check(app.SetSceneSettings(settings,&error),"checked sun settings: "+error);
    const auto desired=editor::MetadataSun(settings.background,settings.background.skyRotation);auto gathered=GatherSceneLights(app.GetWorld(),{},{});
    const float towardSun[]={desired.x,desired.y,desired.z};for(int i=0;i<3;++i)Check(std::abs(gathered.lighting.directional.direction[i]+towardSun[i])<2e-5,"sun world orientation agrees with actual Runtime LightGather");
    Check(ComposeTRS(*app.GetWorld().GetComponent<TransformComponent>(sun))!=ComposeTRS(local),"parented sun local orientation updated through checked world write");
    Check(app.SaveSceneAs("Scenes/Sun.json",false,&error)&&ApplicationTestAccess::Open(app,"Scenes/Sun.json",error),error);
    sun=Find(app.GetWorld(),"sun");parent=Find(app.GetWorld(),"sun-parent");
    auto before=Session(app);app.GetWorld().GetComponent<TransformComponent>(parent)->scale={2,1,1};before=Session(app);
    settings.background.background=EditorBackground::DayNight;settings.background.driveLightProperties=true;settings.background.sunDirection={1,1,0};
    Check(!app.SetSceneSettings(settings,&error)&&error.find("unsupported affine")!=std::string::npos&&Session(app)==before,"unsupported parent affine refuses settings/orientation/properties without partial mutation: "+error);
    Check(!ApplicationTestAccess::Runtime(app,error)&&Session(app)==before,"unsupported sun ancestry refuses Play preflight before publication");
    app.GetWorld().GetComponent<TransformComponent>(parent)->scale={2,2,2};
    Check(app.SetSceneSettings(settings,&error),error);gathered=GatherSceneLights(app.GetWorld(),{},{});
    Check(std::isfinite(gathered.lighting.directional.intensity)&&gathered.lighting.directional.intensity>=0,"DayNight light properties coherent");
    Check(ApplicationTestAccess::New(app,World::DataMode::HierarchyData,error),error);
    sun=Object(app.GetWorld(),"range-sun",{999999,0,0});auto collider=Object(app.GetWorld(),"range-collider");app.GetWorld().AddComponent<DirectionalLightComponent>(sun);
    Check(SetParent(app.GetWorld(),collider,sun),"sun range fixture");app.GetWorld().AddComponent<ColliderComponent>(collider).autoFitFromMesh=false;app.GetWorld().AddComponent<RigidbodyComponent>(collider).isStatic=true;
    app.GetWorld().RequireRuntimeWorld();before=Session(app);settings=app.GetSceneSettings();settings.background.sunSync=SunSyncMode::SkyDrivesLight;settings.background.sunMetadata=true;settings.background.sunDirection={0,0,1};
    Check(!app.SetSceneSettings(settings,&error)&&Session(app)==before&&error.find("range")!=std::string::npos,"checked sun write that violates descendant physics rolls back orientation and all settings: "+error);
}
static Json Ref(const std::string& id){return {{"$entity",id}};}
static void RuntimeFixture(Fixture& f){
    const auto file=f.a->ResolveAsset("scripts/activation.lua");fs::create_directories(file.parent_path());
    std::ofstream script(file);script<<R"(return {fields={root={type='entity'},child={type='entity'},observer={type='entity'},code={type='number',default=1}},
on_start=function(self) if self.code==1 then
 World.set_position(self.root,Vec3.new(1,0,4));assert(math.abs(World.get_position(self.child).x-2)<.001)
 World.set_position(self.child,Vec3.new(3,0,4));assert(math.abs(World.get_position(self.child).x-3)<.001) end end,
on_stop=function(self) local p=World.get_position(self.observer);World.set_position(self.observer,Vec3.new(p.x,p.y+1,p.z)) end,
on_destroy=function(self) local p=World.get_position(self.observer);World.set_position(self.observer,Vec3.new(p.x*10+self.code,p.y,p.z)) end})";
    script.close();Check(bool(script),"owned Lua fixture");
    World world(World::DataMode::HierarchyData);auto parent=Object(world,"parent",{0,0,4}),child=Object(world,"child",{1,0,4}),grand=Object(world,"grandchild",{2,0,4});
    Check(SetParent(world,child,parent)&&SetParent(world,grand,child),"runtime graph fixture");Object(world,"observer");
    auto body=Object(world,"body",{8,0,4});world.AddComponent<RigidbodyComponent>(body).useGravity=false;world.GetComponent<RigidbodyComponent>(body)->velocity={1,0,0};world.AddComponent<ColliderComponent>(body).autoFitFromMesh=false;
    auto& collider=world.AddComponent<ColliderComponent>(child);collider.autoFitFromMesh=false;world.AddComponent<RigidbodyComponent>(child).isStatic=true;
    auto& mesh=world.AddComponent<MeshRendererComponent>(child);mesh.meshPath="@engine/models/primitives/cube.obj";mesh.shaderPath="dx12/textured.hlsl";
    world.AddComponent<MaterialComponent>(child).materialPath="@engine/materials/default_primitive.material.json";
    int code=0;for(auto e:{parent,child,grand}){auto& s=world.AddComponent<LuaScriptComponent>(e);s.scriptPath="scripts/activation.lua";s.properties={{"root",Ref("parent")},{"child",Ref("child")},{"observer",Ref("observer")},{"code",++code}};}
    auto marker=Object(world,"start",{0,2,-6});world.AddComponent<PlayerStartComponent>(marker).controllerScript.clear();
    std::string error;Check(SceneSerializer::SaveWorld(f.a->ResolveScene("Scenes/Runtime.json"),world,&error),error);
}
static void Plays(Fixture& f){RuntimeFixture(f);Application app;std::string error;Check(app.OpenProject(f.a->descriptorPath,Application::ProjectDecision::Discard,&error),error);
    Check(ApplicationTestAccess::Open(app,"Scenes/Runtime.json",error),error);const auto author=SceneSerializer::CaptureWorld(app.GetEditorWorld(),app.GetSceneSettings());const auto authorId=app.GetEditorWorld().GetIdentity();
    for(int cycle=0;cycle<3;++cycle){const auto before=Session(app);auto prepared=ApplicationTestAccess::Runtime(app,error);Check(prepared&&prepared->world->IsHierarchyData()&&Session(app)==before,"real runtime candidate has no active session side effects: "+error);
        app.SetEditorPlayMode(true);auto runtimeId=app.GetWorld().GetIdentity();Check(app.IsEditorPlayMode()&&runtimeId!=authorId&&app.GetWorld().IsHierarchyData(),"real controlled hierarchy Application Play");
        auto p=Find(app.GetWorld(),"parent"),c=Find(app.GetWorld(),"child"),g=Find(app.GetWorld(),"grandchild");Check(GetParent(app.GetWorld(),c)==p&&GetParent(app.GetWorld(),g)==c&&!app.GetWorld().IsAlive(Find(app.GetEditorWorld(),"child")),"clone graph and handle isolation");
        app.GetLuaRuntime().Update(0);Check(app.GetLuaRuntime().InstanceCount()==3&&app.GetLuaRuntime().Error("scripts/activation.lua").empty()&&std::abs(ReadWorldPosition(app.GetWorld(),c)->x-3)<1e-5,"real Lua checked world get/set follows parent: "+app.GetLuaRuntime().Error("scripts/activation.lua"));
        auto hit=Raycast(app.GetWorld(),{3,0,0},{0,0,1},6);Check(hit&&hit->entity==c&&std::abs(hit->distance-3.5f)<1e-4,"query uses updated parent/child geometry");
        app.SetEditorPaused(true);const auto paused=Session(app);app.SetEditorPaused(false);Check(app.GetWorld().GetIdentity()==runtimeId&&app.GetLuaRuntime().InstanceCount()==3,"Pause Resume preserve world/instances");app.SetEditorPaused(true);
        Check(!ApplicationTestAccess::Open(app,"Scenes/Bad.json",error)&&Session(app)==paused,"failed load retains active paused hierarchy runtime");
        if(cycle==0){app.GetMutationQueue().Destroy(p);app.ApplyStructuralCommands();auto log=*ReadWorldPosition(app.GetWorld(),Find(app.GetWorld(),"observer"));Check(!app.GetWorld().IsAlive(p)&&!app.GetWorld().IsAlive(c)&&!app.GetWorld().IsAlive(g)&&app.GetLuaRuntime().InstanceCount()==0&&log.x==321&&log.y==3,"real Application subtree cleanup children-first exactly once");}
        app.SetEditorPlayMode(false);Check(app.GetWorld().GetIdentity()==authorId&&!app.GetWorld().IsAlive(c)&&app.GetLuaRuntime().InstanceCount()==0&&SceneSerializer::CaptureWorld(app.GetEditorWorld(),app.GetSceneSettings())==author,"Stop restores unchanged author/settings and retires runtime handles");
    }
    auto parent=Find(app.GetEditorWorld(),"parent"),child=Find(app.GetEditorWorld(),"child");auto& invalid=app.GetEditorWorld().AddComponent<RigidbodyComponent>(parent);invalid.isStatic=false;
    auto before=Session(app);bool refused=false;try{app.SetEditorPlayMode(true);}catch(const std::runtime_error& e){refused=std::string(e.what()).find("preflight")!=std::string::npos;}Check(refused&&Session(app)==before,"invalid dynamic ancestor fails before Play commit");app.GetEditorWorld().RemoveComponent<RigidbodyComponent>(parent);
    auto badStart=Find(app.GetEditorWorld(),"start");Check(SetParent(app.GetEditorWorld(),badStart,parent),"invalid marker fixture");before=Session(app);Check(!ApplicationTestAccess::Runtime(app,error)&&Session(app)==before&&error.find("PlayerStart")!=std::string::npos,"PlayerStart checked before cleanup");Check(ClearParent(app.GetEditorWorld(),badStart),"restore marker");
    auto* s=app.GetEditorWorld().GetComponent<LuaScriptComponent>(child);const auto path=s->scriptPath;s->scriptPath="scripts/missing.lua";before=Session(app);refused=false;try{app.SetEditorPlayMode(true);}catch(const std::runtime_error& e){refused=std::string(e.what()).find("Lua")!=std::string::npos;}Check(refused&&Session(app)==before,"Lua prerequisites failure preserves author/history/settings before commit");s->scriptPath=path;
    app.GetEditorWorld().GetComponent<PlayerStartComponent>(badStart)->controllerScript="scripts/missing-controller.lua";before=Session(app);
    Check(!ApplicationTestAccess::Runtime(app,error)&&error.find("missing-controller")!=std::string::npos&&Session(app)==before,"spawned Player controller prerequisites validated without author marker cleanup");
}
#ifdef ENABLE_DX12
static std::vector<char> Bytes(const fs::path& path){std::ifstream file(path,std::ios::binary);Check(bool(file),"Application frame capture exists");return {std::istreambuf_iterator<char>(file),{}};}
static void Native(Fixture& f){RuntimeFixture(f);Application app;std::string error;Check(app.Initialize(Application::StartupMode::ProjectBrowser),"real DX12 Application Initialize");
    auto* native=dynamic_cast<GlfwWindow*>(app.GetWindow());Check(native!=nullptr,"real GLFW window");glfwHideWindow(native->GetGlfwHandle());
    Check(app.OpenProject(f.a->descriptorPath,Application::ProjectDecision::Discard,&error),error);Check(ApplicationTestAccess::Open(app,"Scenes/Runtime.json",error),error);
    auto& world=app.GetEditorWorld();auto shearParent=Object(world,"affine-parent",{-2,0,4}),shearChild=Object(world,"affine-child",{-2,0,4});
    Check(SetParent(world,shearChild,shearParent),"affine visual fixture");world.GetComponent<TransformComponent>(shearChild)->rotation.z=.6f;world.GetComponent<TransformComponent>(shearParent)->scale={1.7f,1,1};
    const auto meshDescription=*world.GetComponent<MeshRendererComponent>(Find(world,"child"));const auto materialDescription=*world.GetComponent<MaterialComponent>(Find(world,"child"));
    world.AddComponent<MeshRendererComponent>(shearChild)=meshDescription;world.AddComponent<MaterialComponent>(shearChild)=materialDescription;
    Check(!ReadWorldTransform(world,shearChild),"real Application fixture includes full affine shear");
    auto settings=app.GetSceneSettings();settings.shadows.resolution=512;settings.shadows.focusDistance=8;settings.shadows.coverage=20;settings.background.background=EditorBackground::Solid;settings.background.solid={.02f,.03f,.04f};settings.environment.enabled=false;
    Check(app.SetSceneSettings(settings,&error)&&app.SetEditorCameraPose({0,0,-8},0,0),"native deterministic scene/camera settings");
    auto* resources=app.GetResourceManager();auto* physics=app.GetPhysicsSystem();auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(gpu!=nullptr,"actual DX12 adapter");
    auto mesh=resources->Load<MeshResource>("@engine/models/primitives/cube.obj");resources->Load<MaterialResource>("@engine/materials/default_primitive.material.json");resources->Load<ShaderResource>("dx12/textured.hlsl");resources->DrainProjectLoads();Check(mesh&&mesh->IsLoaded(),"native assets finalized through real ResourceManager");
    auto run=[&](unsigned frames){app.SetFrameLimit(frames);Check(app.Run()==0,"real Application Run");};run(16);
    for(const char* name:{"Scene Hierarchy","Inspector","Main Toolbar"}){auto* window=ImGui::FindWindowByName(name);Check(!window||!window->Active,"unadapted public hierarchy controls remain closed");}
    const auto author=SceneSerializer::CaptureWorld(app.GetEditorWorld(),app.GetSceneSettings());const auto authorId=app.GetEditorWorld().GetIdentity();
    for(int cycle=0;cycle<3;++cycle){app.SetEditorPlayMode(true);Check(app.IsEditorPlayMode()&&app.GetWorld().IsHierarchyData(),"actual DX12 hierarchy Play");run(48);
        auto parent=Find(app.GetWorld(),"parent"),child=Find(app.GetWorld(),"child"),body=Find(app.GetWorld(),"body");
        Check(app.GetLuaRuntime().Error("scripts/activation.lua").empty()&&app.GetLuaRuntime().InstanceCount()==3&&std::abs(ReadWorldPosition(app.GetWorld(),child)->x-3)<1e-5,"Application Run executes real Lua world-space APIs");
        Check(ReadWorldPosition(app.GetWorld(),body)->x>8&&app.GetPhysicsSystem()==physics,"Application fixed Physics advances hierarchy dynamic root");
        Check(app.GetRenderStatistics().visibleObjects==2&&app.GetRenderStatistics().submitted.drawCalls>0,"Application affine/current world meshes submitted");
        app.SetEditorPaused(true);const auto paused=SceneSerializer::CaptureWorld(app.GetWorld());const auto runtimeId=app.GetWorld().GetIdentity();run(6);
        Check(SceneSerializer::CaptureWorld(app.GetWorld())==paused&&app.GetWorld().GetIdentity()==runtimeId&&app.GetLuaRuntime().InstanceCount()==3,"native Pause preserves data/identity/Lua instances");
        if(cycle==0){auto capture=[&](const char* name){auto path=f.root/name;app.SetFrameCaptureOutput((f.root.filename()/name).string());run(6);app.SetFrameCaptureOutput({});return Bytes(path);};
            const auto first=capture("before.ppm");const auto shadow=gpu->ReadBackShadowDepth();Check(!shadow.empty()&&std::any_of(shadow.begin(),shadow.end(),[](float value){return value<.999f;}),"actual Application hierarchy shadow depth");
            Check(TrySetWorldPosition(app.GetWorld(),parent,{2,0,4}),"supported runtime parent edit");const auto moved=capture("after.ppm");const auto movedShadow=gpu->ReadBackShadowDepth();
            Check(first.size()>100&&first!=moved&&shadow!=movedShadow,"parent movement changes real Application image and shadow geometry");
            auto hit=Raycast(app.GetWorld(),{4,0,0},{0,0,1},6);Check(hit&&hit->entity==child,"fresh parent movement reflected by physics query");
            const auto before=Session(app);auto bad=Read(f.b->ResolveScene(f.b->startupScene));bad["worldMode"]=17;Write(f.b->ResolveScene(f.b->startupScene),bad);
            Check(!ApplicationTestAccess::Project(app,f.b->descriptorPath,error)&&Session(app)==before&&app.GetResourceManager()==resources&&resources->Load<MeshResource>("@engine/models/primitives/cube.obj")==mesh,"invalid project startup preserves native active runtime/resources");
            app.GetMutationQueue().Destroy(parent);app.ApplyStructuralCommands();auto log=*ReadWorldPosition(app.GetWorld(),Find(app.GetWorld(),"observer"));
            Check(!app.GetWorld().IsAlive(child)&&app.GetLuaRuntime().InstanceCount()==0&&log.x==321&&log.y==3,"native subtree lifecycle children-first through Application mutation queue");run(6);Check(app.GetRenderStatistics().visibleObjects==1,"no stale deleted subtree render snapshot");
        }
        app.SetEditorPaused(false);run(6);Check(app.GetWorld().GetIdentity()==runtimeId,"native Resume retains runtime identity");app.SetEditorPlayMode(false);
        Check(!app.GetWorld().IsAlive(child)&&app.GetWorld().GetIdentity()==authorId&&app.GetLuaRuntime().InstanceCount()==0&&SceneSerializer::CaptureWorld(app.GetEditorWorld(),app.GetSceneSettings())==author,"native repeated Stop preserves authoring data/settings and fences runtime handles");
    }
    Check(app.SaveSceneAs("Scenes/NativeSaved.json",false,&error)&&ApplicationTestAccess::Open(app,"Scenes/NativeSaved.json",error),"real native hierarchy Save/Open lifecycle: "+error);run(6);
    Check(app.CloseProject(Application::ProjectDecision::Discard,&error)&&app.IsProjectBrowserActive(),error);Check(app.OpenProject(f.a->descriptorPath,Application::ProjectDecision::Discard,&error)&&!app.GetWorld().IsHierarchyData(),"native return to RootOnly project");run(6);
    Check(gpu->GetValidationErrorCount()==0,"DX12 Application hierarchy ERROR/CORRUPTION zero");app.Shutdown();
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{Fixture fixture;
#ifdef ENABLE_DX12
    if(argc>1&&std::string(argv[1])=="--dx12")Native(fixture);else
#endif
    {Scenes(fixture);Upgrades(fixture);Suns(fixture);Plays(fixture);}
    std::cout<<"Application hierarchy activation: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
