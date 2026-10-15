#include "scripting/LuaRuntime.h"
#include "scripting/Prefab.h"
#include "core/AssetPaths.h"
#include "core/Application.h"
#include "core/Logger.h"
#include "ecs/components/LuaScriptComponent.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "ecs/components/TransformComponent.h"
#include "ecs/components/TagComponent.h"
#include "scene/SceneSerializer.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
int checks=0;
void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);++checks;}
void Write(const std::string& key,const std::string& contents){auto path=scripting::SafeAsset(key,key.ends_with(".lua")?".lua":".prefab.json");std::filesystem::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);file<<contents;if(!file)throw std::runtime_error("Fixture write failed");}
ecs::Entity Actor(ecs::World& world,const std::string& key){auto e=world.CreateEntity();world.AddComponent<ecs::TransformComponent>(e);world.AddComponent<ecs::LuaScriptComponent>(e).scriptPath=key;return e;}
float X(ecs::World& w,ecs::Entity e){return w.GetComponent<ecs::TransformComponent>(e)->position.x;}
const char* counter=R"(return {
 fields={ speed={type="number",default=1,min=0,max=10} },
 on_start=function(self) self.n=0 end,
 on_update=function(self,dt) self.n=self.n+self.speed; World.set_position(self.entity,Vec3.new(self.n,0,0)) end,
 on_event=function(self,event) if event.type=="collision" then self.speed=3 end end,
 on_destroy=function(self) Log.info("destroyed counter") end
})";
}
int main(){
    auto original=std::filesystem::current_path();
    try{
        const auto assets=AssetPaths::ResolveAssetRoot();
        auto scratch=original/"out"/("lua-regression-"+ecs::NewSceneIdentity());std::filesystem::create_directories(scratch/"assets");
        for(const char* folder:{"scripts","prefabs","scenes"})std::filesystem::copy(assets/folder,scratch/"assets"/folder,std::filesystem::copy_options::recursive);
        std::filesystem::current_path(scratch);
        Write("scripts/counter.lua",counter);
        ecs::World world;auto a=Actor(world,"scripts/counter.lua");auto b=Actor(world,"scripts/counter.lua");world.GetComponent<ecs::LuaScriptComponent>(b)->properties["speed"]=2;
        scripting::LuaRuntime lua;Check(lua.ReloadScript("scripts/counter.lua"),"load counter");lua.Start(world);lua.Update(.02f);
        Check(X(world,a)==1&&X(world,b)==2,"per-entity state and override");lua.Update(.02f);Check(X(world,a)==2&&X(world,b)==4,"persistent independent state");
        ecs::EventBus bus;bus.SubscribeCollision([&](const ecs::CollisionEvent& event){lua.QueueCollision(event);});bus.PublishCollision({a,b});lua.Update(.02f);Check(X(world,a)==5&&X(world,b)==7,"EventBus collision reaches Lua");
        Write("scripts/counter.lua","return { broken = ");Check(!lua.ReloadScript("scripts/counter.lua"),"syntax error rejected");lua.Update(.02f);Check(X(world,a)==8,"old version survives failed reload");
        Write("scripts/counter.lua",counter);for(int i=0;i<20;++i){Check(lua.ReloadScript("scripts/counter.lua"),"reload repetition");lua.Update(.02f);Check(X(world,a)==1,"successful reload resets instances");}
        std::filesystem::remove(scripting::SafeAsset("scripts/counter.lua",".lua"));Check(!lua.ReloadScript("scripts/counter.lua"),"deleted reload rejected");Write("scripts/counter.lua",counter);
        auto scene=SceneSerializer::CaptureWorld(world);ecs::World restored;SceneSettings settings;std::string error;Check(SceneSerializer::RestoreWorld(scene,restored,settings,&error),"scene restore");auto entities=restored.Entities();Check(restored.GetComponent<ecs::LuaScriptComponent>(entities[1])->properties["speed"]==2,"scene properties roundtrip");
        auto clone=world.CloneData();Check(!clone->IsAlive(a),"foreign world rejected");
        auto enemy=scripting::SpawnPrefab(world,"prefabs/wave_enemy.prefab.json",{4,0,0});Check(world.HasComponent<ecs::LuaScriptComponent>(enemy)&&X(world,enemy)==4,"prefab components and override");
        auto other=scripting::SpawnPrefab(world,"prefabs/wave_enemy.prefab.json",{});Check(world.GetComponent<ecs::SceneIdentityComponent>(enemy)->value!=world.GetComponent<ecs::SceneIdentityComponent>(other)->value,"prefab fresh identity");
        auto count=world.GetAliveCount();Write("prefabs/bad.prefab.json",R"({"version":1,"components":{"Unknown":{}}})");
        try{scripting::SpawnPrefab(world,"prefabs/bad.prefab.json",{});Check(false,"invalid prefab accepted");}catch(const std::exception&){}Check(world.GetAliveCount()==count,"invalid prefab rollback");
        for(const char* key:{"../outside.lua","C:/outside.lua","scripts/../../outside.lua"}){bool rejected=false;try{(void)scripting::SafeAsset(key,".lua");}catch(...){rejected=true;}Check(rejected,"path traversal rejected");}
        Write("scripts/stale.lua",R"(return {on_start=function(self) self.saved=self.entity end,on_update=function(self) World.destroy(self.entity);World.destroy(self.entity);assert(not World.is_alive(self.saved)) end})");auto doomed=Actor(world,"scripts/stale.lua");lua.Update(.02f);Check(!world.IsAlive(doomed),"deferred repeated destroy");auto reused=world.CreateEntity();Check(!world.IsAlive(doomed)&&reused!=doomed,"generation reuse rejected");
        Write("scripts/loop.lua","return {on_update=function(self) while true do end end}");auto loop=Actor(world,"scripts/loop.lua");lua.Update(.02f);Check(lua.Error("scripts/loop.lua").find("budget")!=std::string::npos,"infinite loop terminated");
        Write("scripts/unsafe.lua","os.execute('echo unsafe');return {}");Check(!lua.ReloadScript("scripts/unsafe.lua"),"unsafe library blocked");
        Write("scripts/callback.lua","return {on_update=42}");Check(!lua.ReloadScript("scripts/callback.lua"),"invalid callback without fields rejected");
        Write("scripts/schema.lua",R"(return {fields={speed={type="number",default="fast"}}})");Check(!lua.ReloadScript("scripts/schema.lua"),"invalid schema type rejected");
        Write("scripts/mutate_api.lua","Vec3.new=nil;return {}");Check(lua.ReloadScript("scripts/mutate_api.lua"),"isolated validation environment");Check(lua.ReloadScript("scripts/counter.lua"),"validation cannot corrupt bindings");
        Write("scripts/sideeffect.lua","World.spawn_prefab('prefabs/wave_enemy.prefab.json',Vec3.new(0,0,0));return {}");count=world.GetAliveCount();Check(!lua.ReloadScript("scripts/sideeffect.lua")&&world.GetAliveCount()==count,"validation has no gameplay side effects");
        Write("scripts/runtime_error.lua","return {on_update=function(self) error('intentional') end}");Actor(world,"scripts/runtime_error.lua");lua.Update(.02f);Check(lua.Error("scripts/runtime_error.lua").find("intentional")!=std::string::npos,"protected runtime error");
        Write("scripts/runtime_error.lua","return {on_update=function(self) World.set_position(self.entity,Vec3.new(42,0,0)) end}");Check(lua.ReloadScript("scripts/runtime_error.lua"),"error repair reload");lua.Update(.02f);Check(lua.Error("scripts/runtime_error.lua").empty(),"error recovery");
        lua.Stop();Check(lua.InstanceCount()==0,"Stop frees instances");
        {
            Write("scripts/random.lua",R"(return {on_start=function(self) math.randomseed(12345) end,on_update=function(self) World.set_position(self.entity,Vec3.new(math.random(),0,0)) end})");
            ecs::World left,right;auto a=Actor(left,"scripts/random.lua"),b=Actor(right,"scripts/random.lua");scripting::LuaRuntime first,second;first.Start(left);second.Start(right);first.Update(.02f);second.Update(.02f);Check(X(left,a)==X(right,b),"random reference state");
            Write("scripts/random.lua","math.randomseed(999);error('rejected candidate')");Check(!first.ReloadScript("scripts/random.lua"),"candidate with library side effect rejected");first.Update(.02f);second.Update(.02f);Check(X(left,a)==X(right,b),"failed candidate cannot mutate running VM random state");first.Stop();second.Stop();
        }
        {
            ecs::World typed;Write("scripts/types.lua",R"(return {fields={flag={type="bool",default=true},title={type="string",default="hello"},offset={type="vec3",default={1,2,3}},count={type="integer",default=2}},on_update=function(self) assert(self.flag and self.title=="hello" and self.count==2);World.set_position(self.entity,self.offset) end})");
            auto actor=Actor(typed,"scripts/types.lua");typed.GetComponent<ecs::LuaScriptComponent>(actor)->properties["offset"]=nlohmann::json::array({4,5,6});scripting::LuaRuntime runtime;runtime.Start(typed);runtime.Update(.02f);Check(X(typed,actor)==4&&runtime.Error("scripts/types.lua").empty(),"bool/string/integer/Vec3 typed fields");
            typed.GetComponent<ecs::LuaScriptComponent>(actor)->properties["flag"]="wrong";runtime.Update(.02f);Check(!runtime.Error("scripts/types.lua").empty()&&runtime.InstanceCount()==0,"wrong override type rejects instance");auto revision=Logger::Get().Revision();runtime.Update(.02f);Check(Logger::Get().Revision()==revision,"invalid override does not spam each frame");runtime.Stop();
        }
        {
            ecs::World inputWorld;Write("scripts/input.lua",R"(return {on_update=function(self)
              local x=0;if Input.is_action_held("TestAction") then x=x+1 end
              if Input.is_action_pressed("TestAction") then x=x+10 end
              if Input.is_action_released("TestAction") then x=x+100 end
              World.set_position(self.entity,Vec3.new(x,0,0)) end})");
            auto actor=Actor(inputWorld,"scripts/input.lua");bool down=false;scripting::LuaRuntime controls;controls.Start(inputWorld,[&](const std::string& action){return action=="TestAction"&&down;});
            controls.Update(.02f);Check(X(inputWorld,actor)==0,"input neutral");down=true;controls.Update(.02f);Check(X(inputWorld,actor)==11,"pressed and held");controls.Update(.02f);Check(X(inputWorld,actor)==1,"held has no repeated edge");down=false;controls.Update(.02f);Check(X(inputWorld,actor)==100,"released edge");
            inputWorld.RemoveComponent<ecs::LuaScriptComponent>(actor);controls.Update(.02f);Check(controls.InstanceCount()==0,"component removal destroys instance");controls.Stop();
        }
        // Test the actual Application clone/pause/stop integration without a renderer.
        Application app;auto edit=Actor(app.GetEditorWorld(),"scripts/counter.lua");auto before=SceneSerializer::CaptureWorld(app.GetEditorWorld());
        for(int i=0;i<20;++i){app.SetEditorPlayMode(true);auto runtime=app.MapToRuntime(edit);app.GetLuaRuntime().Update(.02f);Check(X(app.GetWorld(),runtime)==1,"Play starts fresh");app.SetEditorPaused(true);Check(!app.IsSimulationRunning(),"Pause gates simulation");app.SetEditorPaused(false);app.GetLuaRuntime().Update(.02f);Check(X(app.GetWorld(),runtime)==2,"Resume retains state");app.SetEditorPlayMode(false);Check(app.GetLuaRuntime().InstanceCount()==0,"Play/Stop frees instances");Check(SceneSerializer::CaptureWorld(app.GetEditorWorld())==before,"Editor World unchanged");}
        auto& editor=app.GetEditorLayer();editor.SelectEntity(edit);editor.RecordEdit(app,"Lua field test");app.GetEditorWorld().GetComponent<ecs::LuaScriptComponent>(edit)->properties["speed"]=4;editor.FinishTransaction(app);editor.UndoEdit(app);Check(app.GetEditorWorld().GetComponent<ecs::LuaScriptComponent>(edit)->properties.empty(),"Lua fields Undo");editor.RedoEdit(app);Check(app.GetEditorWorld().GetComponent<ecs::LuaScriptComponent>(edit)->properties["speed"]==4,"Lua fields Redo");
        auto duplicate=editor.DuplicateEntity(app,edit);Check(app.GetEditorWorld().GetComponent<ecs::LuaScriptComponent>(duplicate)->properties["speed"]==4,"duplicate preserves Lua fields");
        // Run real wave scene to defeat and victory with Lua-only rules.
        for(bool victory:{false,true}){
            ecs::World demo;Check(SceneSerializer::LoadWorld(scripting::SafeAsset("scenes/lua_wave_demo.json",".json"),demo,settings,&error),"demo scene loads");
            ecs::Entity controller;for(auto e:demo.Entities())if(auto* tag=demo.GetComponent<ecs::TagComponent>(e);tag&&tag->name=="WaveController")controller=e;
            auto* c=demo.GetComponent<ecs::LuaScriptComponent>(controller);c->properties={{"wave_size",2},{"wave_count",1},{"spawn_interval",.1},{"lives",1},{"attack_radius",30}};
            scripting::LuaRuntime game;bool attack=false;game.Start(demo,[&](const std::string& action){return action=="LuaAttack"&&attack;});
            auto seq=Logger::Get().Revision();for(int tick=0;tick<200;++tick){attack=victory&&(tick%2==0);game.Update(.1f);}
            bool found=false;for(const auto& entry:Logger::Get().Messages())if(entry.sequence>seq&&entry.message.find(victory?"VICTORY":"DEFEAT")!=std::string::npos)found=true;Check(found,victory?"Lua victory":"Lua defeat");game.Stop();
        }
        std::filesystem::current_path(original);std::cout<<"LuaScriptingRegression: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& error){std::filesystem::current_path(original);std::cerr<<"FAIL after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
