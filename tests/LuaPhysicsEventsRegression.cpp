#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "ApplicationTestAccess.h"
#include "scripting/LuaRuntime.h"
#include "scripting/Prefab.h"
#include "scripting/EntityReference.h"
#include "ecs/PhysicsQueries.h"
#include "ecs/systems/PhysicsSystem.h"
#include "ecs/components/ColliderComponent.h"
#include "ecs/components/RigidbodyComponent.h"
#include "ecs/components/LuaScriptComponent.h"
#include "scene/SceneSerializer.h"
#include <fstream>
#include <iostream>
#include <thread>
#if defined(ENABLE_DX12)
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#endif
using namespace ecs;
static unsigned checks=0;
static void Check(bool v,const char* text){if(!v)throw std::runtime_error(text);++checks;}
static void Near(float a,float b,const char* text){Check(std::abs(a-b)<.001f,text);}
struct Files {
    std::string key="scripts/physics-"+NewSceneIdentity()+"/test.lua";
    std::filesystem::path path=scripting::SafeAsset(key,".lua");
    Files(){Check(std::filesystem::create_directory(path.parent_path()),"owned fixture directory");}
    ~Files(){std::error_code ec;std::filesystem::remove_all(path.parent_path(),ec);}
    void Write(const std::string& source){std::ofstream out(path);out<<source;Check(out.good(),"write Lua fixture");}
};
static Entity Object(World& w,Vec3 p={}){auto e=w.CreateEntity();w.AddComponent<TransformComponent>(e).position=p;return e;}
static Entity Shape(World& w,Vec3 p,ColliderType type=ColliderType::Box){auto e=Object(w,p);w.AddComponent<ColliderComponent>(e).type=type;return e;}
static Entity Actor(World& w,const std::string& path){auto e=Object(w);w.AddComponent<LuaScriptComponent>(e).scriptPath=path;return e;}
static void Queries(){
    World world;auto boxEntity=Shape(world,{0,0,4}),sphereEntity=Shape(world,{0,0,8},ColliderType::Sphere);
    auto h=Raycast(world,{0,0,0},{0,0,2},20);Check(h&&h->entity==boxEntity,"ray returns nearest Collider");Near(h->distance,3.5f,"direction normalized, distance world units");Near(h->normal.z,-1,"box outward normal");Near(h->position.z,3.5f,"hit position");
    h=Raycast(world,{0,0,0},{0,0,1},20,boxEntity);Check(h&&h->entity==sphereEntity,"ignore own Collider");Near(h->distance,7.5f,"sphere intersection");Check(!Raycast(world,{0,2,0},{0,0,1},20),"ray miss");Check(!Raycast(world,{0,0,0},{0,0,1},3),"finite ray segment");
    h=Raycast(world,{0,0,0},{0,0,.000001f},20);Check(h&&h->entity==boxEntity,"small valid direction normalizes without fallback axis");
    {World distant;Shape(distant,{0,0,100000},ColliderType::Sphere);auto remote=Raycast(distant,{},{0,0,1},200000);Check(bool(remote),"distant sphere hit");Near(remote->distance,99999.5f,"sphere discriminant retains small radius at large coordinates");Near(remote->normal.z,-1,"distant sphere normal");}
    auto& t=*world.GetComponent<TransformComponent>(boxEntity);auto& c=*world.GetComponent<ColliderComponent>(boxEntity);c.halfExtents={2,.5f,.25f};t.rotation.y=1.57079632679f;c.offset={0,0,1};t.scale={100,100,100};h=Raycast(world,{0,0,0},{0,0,1},20);Near(h->distance,3,"rotated OBB with world-space offset and already scaled dimensions");Near(h->normal.z,-1,"rotated OBB normal");
    h=Raycast(world,{0,0,5},{0,0,1},20);Near(h->distance,0,"ray starts inside returns zero");
    world.DestroyEntity(boxEntity);h=Raycast(world,{0,0,0},{0,0,1},20);Check(h&&h->entity==sphereEntity,"query omits destroyed entity");
    auto plain=Object(world,{0,0,2});auto list=FindInRadius(world,{0,0,2},0);Check(list.size()==1&&list[0]==plain,"proximity includes Transform without Collider, zero radius");
    list=FindInRadius(world,{0,0,2},6);Check(list.size()==2&&list[0].index<list[1].index,"radius inclusive, deterministic index ordering");
    World foreign;auto f=Shape(foreign,{0,0,1});bool rejected=false;try{Raycast(world,{},{0,0,1},20,f);}catch(...){rejected=true;}Check(rejected,"foreign ignore handle rejected");
    World::CloneMap map;auto clone=world.CloneData(&map);h=Raycast(*clone,{},{0,0,1},20);Check(h&&h->entity==map.at(sphereEntity)&&h->entity.world==clone->GetIdentity(),"query handles belong to queried Runtime World");
}
static const char* triggerScript=R"(return {
 on_start=function(self) self.enter=0;self.exit=0;self.dead=0 end,
 on_event=function(self,event)
   if event.type=='trigger_enter' then self.enter=self.enter+1
   elseif event.type=='trigger_exit' then self.exit=self.exit+1;if not World.is_alive(event.other) then self.dead=self.dead+1 end
   elseif event.type=='collision' then error('trigger must not generate contact event') end
   World.set_rotation(self.entity,Vec3.new(self.enter,self.exit,self.dead))
 end})";
static void Triggers(Files& files){
    files.Write(triggerScript);World w;auto sensor=Actor(w,files.key);w.AddComponent<ColliderComponent>(sensor).halfExtents={2,2,2};w.GetComponent<ColliderComponent>(sensor)->isTrigger=true;w.AddComponent<RigidbodyComponent>(sensor).isStatic=true;
    auto target=Shape(w,{0,0,0},ColliderType::Sphere);auto& rb=w.AddComponent<RigidbodyComponent>(target);rb.velocity={1,0,0};
    scripting::LuaRuntime lua;EventBus bus;bus.SubscribeCollision([&](const CollisionEvent& event){lua.QueueCollision(event);});PhysicsSystem physics(&bus,0,1,2);lua.Start(w);lua.Update(0);
    auto tick=[&]{physics.Update(w,1.f/60);lua.Update(0);Check(lua.Error(files.key).empty(),"trigger Lua callback healthy");};tick();Near(w.GetComponent<TransformComponent>(sensor)->rotation.x,1,"Trigger Enter once");for(int i=0;i<8;++i)tick();Near(w.GetComponent<TransformComponent>(sensor)->rotation.x,1,"no repeating enter while overlapping");Near(rb.velocity.x,1,"trigger produces no impulse");
    w.GetComponent<TransformComponent>(target)->position={20,0,0};tick();tick();Near(w.GetComponent<TransformComponent>(sensor)->rotation.y,1,"Trigger Exit once");
    w.GetComponent<TransformComponent>(target)->position={0,0,0};tick();w.DestroyEntity(target);tick();Near(w.GetComponent<TransformComponent>(sensor)->rotation.y,2,"destruction exits overlap");Near(w.GetComponent<TransformComponent>(sensor)->rotation.z,1,"destroyed other is safely invalid");
    auto replacement=Shape(w,{0,0,0});Check(replacement.index==target.index&&replacement.generation!=target.generation,"trigger fixture generation reuse");tick();Near(w.GetComponent<TransformComponent>(sensor)->rotation.x,3,"generation reuse is a new enter");
    physics.ResetState();tick();Near(w.GetComponent<TransformComponent>(sensor)->rotation.x,4,"ResetState drops stale pairs");
    World cloned;auto next=Actor(cloned,files.key);cloned.AddComponent<ColliderComponent>(next).isTrigger=true;Shape(cloned,{});lua.Start(cloned);lua.Update(0);physics.Update(cloned,1.f/60);lua.Update(0);Near(cloned.GetComponent<TransformComponent>(next)->rotation.x,1,"world switch does not reuse overlap pairs");lua.Stop();
    auto saved=SceneSerializer::CaptureWorld(w,SceneSettings{});World loaded;SceneSettings settings;std::string error;Check(SceneSerializer::RestoreWorld(saved,loaded,settings,&error),"trigger save/load");bool flag=false;loaded.ForEach<ColliderComponent>([&](Entity,const auto& collider){flag|=collider.isTrigger;});Check(flag,"isTrigger serialized");for(auto& e:saved["entities"])if(e["components"].contains("Collider"))e["components"]["Collider"].erase("isTrigger");Check(SceneSerializer::RestoreWorld(saved,loaded,settings,&error),"old collider scenes load");loaded.ForEach<ColliderComponent>([&](Entity,const auto& collider){Check(!collider.isTrigger,"old scene default is non-trigger");});
}
static const char* timerScript=R"(return {
 on_start=function(self)
   self.once=0;self.beats=0
   local canceled=Timer.after(0.1,'canceled');assert(Timer.cancel(canceled));assert(not Timer.cancel(canceled))
   Timer.after(0.25,'once');self.repeating=Timer.every(0.1,'beat')
 end,
 on_update=function(self)
   if World.get_position(self.entity).z>0 and self.repeating then assert(Timer.cancel(self.repeating));self.repeating=nil end
 end,
 on_event=function(self,event)
   assert(type(event.timer)=='number')
   if event.type=='once' then self.once=self.once+1 elseif event.type=='beat' then self.beats=self.beats+1 else error('canceled timer delivered') end
   World.set_position(self.entity,Vec3.new(self.once,self.beats,0))
 end})";
static void Timers(Files& files){
    files.Write(timerScript);World w;auto e=Actor(w,files.key);scripting::LuaRuntime lua;lua.Start(w);lua.Update(0);lua.FixedUpdate(.5f);Near(w.GetComponent<TransformComponent>(e)->position.y,0,"fixed ticks do not advance frame timers");
    lua.Update(.1f);Near(w.GetComponent<TransformComponent>(e)->position.y,1,"repeating timer due");lua.Update(.2f);Near(w.GetComponent<TransformComponent>(e)->position.x,1,"once timer due exactly once");Near(w.GetComponent<TransformComponent>(e)->position.y,2,"late repeat coalesces missed periods");lua.Update(10);Near(w.GetComponent<TransformComponent>(e)->position.x,1,"once remains once");Near(w.GetComponent<TransformComponent>(e)->position.y,3,"long frame bounded repeating events");
    w.GetComponent<TransformComponent>(e)->position.z=1;lua.Update(0);lua.Update(10);Near(w.GetComponent<TransformComponent>(e)->position.y,3,"cancel repeating handle");
    w.GetComponent<TransformComponent>(e)->position={};Check(lua.ReloadScript(files.key),"reload timers");lua.Update(0);lua.Update(.1f);Near(w.GetComponent<TransformComponent>(e)->position.y,1,"reload starts fresh timers");
    files.Write("return {on_start=function(self) World.set_position(self.entity,Vec3.new(7,0,0)) end,on_event=function(self,event) error('retired timer leaked') end}");Check(lua.ReloadScript(files.key),"reload replacement");lua.Update(0);lua.Update(1);Check(lua.Error(files.key).empty(),"reload clears all retired timer events");Near(w.GetComponent<TransformComponent>(e)->position.x,7,"replacement state retained");lua.Stop();
    files.Write(timerScript);lua.Start(w);lua.Update(0);lua.Update(.1f);Near(w.GetComponent<TransformComponent>(e)->position.y,1,"Stop/Start fresh game clock");w.DestroyEntity(e);lua.Update(10);Check(lua.InstanceCount()==0,"destroy timer owner cleanup");lua.Stop();
    files.Write("return {on_start=function(self) Timer.after(0,'boom') end,on_event=function(self,event) error('expected callback error') end}");World errors;Actor(errors,files.key);lua.Start(errors);lua.Update(0);Check(!lua.Error(files.key).empty(),"timer callback error isolated");lua.Stop();
}
static void LuaQueries(Files& files){
    files.Write(R"(return {on_start=function(self)
      local hit=Physics.raycast(Vec3.new(0,0,0),Vec3.new(0,0,3),10,self.entity)
      assert(hit and World.is_alive(hit.entity) and hit.distance==3.5 and hit.position.z==3.5 and hit.normal.z==-1)
      assert(Physics.raycast(Vec3.new(0,9,0),Vec3.new(0,0,1),10)==nil)
      local list=World.find_in_radius(Vec3.new(0,0,4),1);assert(#list==1 and World.is_alive(list[1]))
      World.destroy(hit.entity)
      assert(#World.find_in_radius(Vec3.new(0,0,4),1)==0)
      assert(Physics.raycast(Vec3.new(0,0,0),Vec3.new(0,0,1),10,self.entity)==nil)
    end})");World w;auto e=Actor(w,files.key);w.AddComponent<ColliderComponent>(e);Shape(w,{0,0,4});scripting::LuaRuntime lua;lua.Start(w);lua.Update(0);Check(lua.Error(files.key).empty(),"Lua ray/proximity ignore pending destruction");lua.Stop();
    for(const char* command:{"Physics.raycast(Vec3.new(0,0,0),Vec3.new(0,0,0),3)","Physics.raycast(Vec3.new(0/0,0,0),Vec3.new(0,0,1),3)","Physics.raycast(Vec3.new(0,0,0),Vec3.new(0,0,1),-1)","Physics.raycast(Vec3.new(0,0,0),Vec3.new(0,0,1),math.huge)","World.find_in_radius(Vec3.new(0,0,0),-1)","World.find_in_radius(Vec3.new(math.huge,0,0),3)","Timer.after(-1,'bad')","Timer.after(math.huge,'bad')","Timer.every(0,'bad')","Timer.every(0.1,'')"}){
        files.Write(std::string("return {on_start=function(self) ")+command+" end}");World bad;Actor(bad,files.key);lua.Start(bad);lua.Update(0);Check(!lua.Error(files.key).empty(),"bad Lua arguments rejected without crashing runtime");lua.Stop();
    }
    files.Write("return {on_start=function(self) for i=1,129 do Timer.every(1,'limit') end end}");World budget;Actor(budget,files.key);lua.Start(budget);lua.Update(0);Check(lua.Error(files.key).find("budget")!=std::string::npos,"timer instance budget enforced");lua.Stop();
}
#if defined(ENABLE_DX12)
static void Native(Files& files,float frameDelta){
    files.Write(R"(return {on_start=function(self)
      self.enter=0;self.exit=0;self.count=0
      assert(Camera.acquire());local hit=Physics.raycast(Camera.position(),Camera.forward(),20,self.entity);assert(hit and World.is_alive(hit.entity))
      assert(#World.find_in_radius(Vec3.new(0,0,5),3)==2)
      Timer.every(0.02,'beat')
    end,on_event=function(self,event)
      if event.type=='trigger_enter' then self.enter=self.enter+1 elseif event.type=='trigger_exit' then self.exit=self.exit+1 elseif event.type=='beat' then self.count=self.count+1 end
      World.set_rotation(self.entity,Vec3.new(self.enter,self.exit,self.count))
    end})");
    Application app;Check(app.Initialize(),"DX12 init");ApplicationTestAccess::FrameDelta(app,frameDelta);SyntheticEditorInput input;auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle();glfwHideWindow(window);app.NewScene();auto& ui=app.GetEditorLayer();auto& edit=app.GetWorld();auto box=ui.CreatePrimitive(app,"cube"),sensor=ui.CreateEntity(app,"Sensor");
    ui.AddComponent(app,box,ComponentId("Collider"));ui.AddComponent(app,box,ComponentId("Rigidbody"));edit.GetComponent<RigidbodyComponent>(box)->isStatic=true;edit.GetComponent<TransformComponent>(box)->position={0,0,5};ui.AddComponent(app,sensor,ComponentId("Collider"));edit.GetComponent<ColliderComponent>(sensor)->isTrigger=true;edit.GetComponent<ColliderComponent>(sensor)->halfExtents={2,2,2};edit.GetComponent<TransformComponent>(sensor)->position={0,0,5};ui.AddComponent(app,sensor,ComponentId("LuaScript"));auto* script=edit.GetComponent<LuaScriptComponent>(sensor);script->scriptPath=files.key;script->cameraEnabled=true;ui.ClearSelection();app.SetEditorCameraPose({0,0,-6},0,0);
    auto frame=[&](int n=3){app.SetFrameLimit(n);Check(app.Run()==0,"native frames");};frame(60);auto saved=SceneSerializer::CaptureWorld(edit,app.GetSceneSettings());auto sensorRef=nlohmann::json{{"$entity",edit.GetComponent<SceneIdentityComponent>(sensor)->value}},boxRef=nlohmann::json{{"$entity",edit.GetComponent<SceneIdentityComponent>(box)->value}};
    app.SetEditorPlayMode(true);auto& runtime=app.GetWorld();auto owner=scripting::ResolveEntityReference(runtime,sensorRef),target=scripting::ResolveEntityReference(runtime,boxRef);
    frame(32);Check(app.GetPhysicsFrameStatistics().simulationSeconds>0,"deterministic entry includes fixed ticks");auto* pose=runtime.GetComponent<TransformComponent>(owner);Check(app.GetLuaRuntime().Error(files.key).empty(),"native query/timer/trigger callbacks healthy");Near(pose->rotation.x,1,"native PhysicsSystem trigger routed through Application EventBus");Check(pose->rotation.z>0,"native gameplay timer advances");
    app.SetEditorPaused(true);auto paused=*pose;frame(10);Check(pose->rotation.x==paused.rotation.x&&pose->rotation.y==paused.rotation.y&&pose->rotation.z==paused.rotation.z,"Pause freezes gameplay timers");
    app.SetEditorPaused(false);runtime.GetComponent<TransformComponent>(target)->position={20,0,5};frame(32);Check(app.GetPhysicsFrameStatistics().simulationSeconds>0,"deterministic exit includes fixed ticks");Near(pose->rotation.y,1,"native Trigger Exit once");frame(32);Near(pose->rotation.x,1,"no duplicate enter");Near(pose->rotation.y,1,"no duplicate exit after additional ticks");
    files.Write("return {on_start=function(self) Timer.after(0,'new') end,on_event=function(self,event) assert(event.type=='new'); World.set_scale(self.entity,Vec3.new(2,2,2)) end}");Check(app.GetLuaRuntime().ReloadScript(files.key),"native hot reload");frame(10);Near(pose->scale.x,2,"native new timer callback");Check(app.GetLuaRuntime().Error(files.key).empty(),"native reload clears previous repeating timers");
    app.SetEditorPlayMode(false);frame();Check(SceneSerializer::CaptureWorld(edit,app.GetSceneSettings())==saved,"Stop preserves editor scene and trigger data");Check(app.GetLuaRuntime().InstanceCount()==0,"Stop retires Lua instances/timers");Check(dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter())->GetValidationErrorCount()==0,"DX12 validation zero");
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{Files files;
#if defined(ENABLE_DX12)
    if(argc>1&&std::string(argv[1])=="--dx12"){for(float fps:{30.f,60.f,144.f})Native(files,1.f/fps);}else
#endif
    {Queries();Triggers(files);Timers(files);LuaQueries(files);}std::cout<<"Lua Physics Events: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
