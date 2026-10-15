#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "scripting/LuaRuntime.h"
#include "scripting/Prefab.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "ecs/PlayerSpawn.h"
#include "scene/SceneSerializer.h"
#include "ecs/components/SceneIdentityComponent.h"
#include <fstream>
#include <set>
#include <iostream>
#if defined(ENABLE_DX12)
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <GLFW/glfw3.h>
#endif
using namespace ecs;
static int checks=0;
static void Check(bool v,const char* text){if(!v)throw std::runtime_error(text);++checks;}
static void Near(float a,float b,float tolerance,const char* text){Check(std::abs(a-b)<=tolerance,text);}
struct Assets {
 std::string prefix="scripts/controller-test-"+NewSceneIdentity()+"/";std::filesystem::path directory=AssetPaths::ResolveAssetRoot()/std::filesystem::u8path(prefix).parent_path();
 Assets(){Check(std::filesystem::create_directories(directory),"owned fixture directory");}
 ~Assets(){std::error_code error;std::filesystem::remove_all(directory,error);}
 std::string Write(const std::string& name,const std::string& source){std::ofstream f(directory/name,std::ios::binary);f<<source;Check(bool(f),"fixture write");return prefix+name;}
};
struct Host {
 InputManager input;std::set<int> keys,buttons;bool allowed=true,captured=false;float mx=0,my=0;scripting::GameplayCamera camera;
 void Sample(){input.SampleFrame([&](int k){return keys.contains(k);},[&](int b){return buttons.contains(b);},mx,my);}
 scripting::GameplayServices Services(){scripting::GameplayServices s;s.camera=&camera;s.inputAllowed=[&]{return allowed;};s.pressed=[&](const std::string& a){return allowed&&input.FramePressed(a);};s.released=[&](const std::string& a){return allowed&&input.FrameReleased(a);};s.mouseDelta=[&]{return allowed&&captured?input.MouseDelta():Vec3{};};s.cursorCaptured=[&]{return captured;};s.captureCursor=[&](bool capture){if(capture&&!allowed)return false;captured=capture;return true;};s.bindAction=[&](const std::string& a,const std::string& k){return input.BindNamedAction(a,k);};s.actions=[&]{return input.Actions();};return s;}
};
static Entity Actor(World& w,const std::string& path){auto e=w.CreateEntity();w.AddComponent<TransformComponent>(e);auto& s=w.AddComponent<LuaScriptComponent>(e);s.scriptPath=path;s.inputEnabled=true;s.cameraEnabled=true;return e;}
static void InputFrames(){
 Host h;Check(h.input.BindNamedAction("Jump","Space")&&h.input.BindNamedAction("Click","MouseLeft"),"named keys and buttons");Check(!h.input.BindNamedAction("Bad","unknown")&&!h.input.BindNamedAction("","W"),"invalid actions rejected");
 h.Sample();h.keys.insert(GLFW_KEY_SPACE);h.buttons.insert(GLFW_MOUSE_BUTTON_LEFT);h.mx=7;h.my=-3;h.Sample();Check(h.input.FrameDown("Jump")&&h.input.FramePressed("Jump")&&h.input.FramePressed("Click"),"frame down/press snapshots");Near(h.input.MouseDelta().x,7,0,"relative mouse X");h.Sample();Check(!h.input.FramePressed("Jump"),"no repeated physical edge");h.keys.clear();h.Sample();Check(h.input.FrameReleased("Jump"),"release snapshot");h.input.ResetMouseDelta();h.mx=1000;h.Sample();Near(h.input.MouseDelta().x,0,0,"capture transition suppresses cursor warp");
}
static void Fps(){
 World w;Host host;scripting::LuaRuntime lua;EventBus events;PhysicsSystem physics(&events,9.81f,.985f,3,.05f,.85f,12);
 auto floor=w.CreateEntity();w.AddComponent<TransformComponent>(floor).position={0,-.025f,0};w.AddComponent<ColliderComponent>(floor).halfExtents={64,.025f,64};w.AddComponent<RigidbodyComponent>(floor).isStatic=true;
 auto start=w.CreateEntity();w.AddComponent<TransformComponent>(start).position={0,2,0};w.AddComponent<PlayerStartComponent>(start);auto player=SpawnPlayer(w);
 auto& script=*w.GetComponent<LuaScriptComponent>(player);script.properties={{"move_speed",6},{"jump_speed",5.2},{"sensitivity",.004}};
 events.SubscribeCollision([&](const CollisionEvent& e){GroundCharacter(w,player,e);});
 lua.Start(w,[&](const std::string& a){return host.allowed&&host.input.FrameDown(a);},host.Services());
 auto frame=[&]{host.Sample();lua.Update(1.f/60);};auto tick=[&]{lua.FixedUpdate(1.f/60);ApplyCharacterMovement(w,player);physics.Update(w,1.f/60);};
 frame();Check(lua.Error(script.scriptPath).empty()&&lua.InstanceCount()==1,"FPS script starts on spawned player");Check(host.camera.following&&host.camera.target==player,"explicit camera follow");
 for(int i=0;i<180;++i)tick();auto& m=*w.GetComponent<CharacterMovementComponent>(player);auto& rb=*w.GetComponent<RigidbodyComponent>(player);auto& t=*w.GetComponent<TransformComponent>(player);Check(m.grounded,"FPS lands");
 host.buttons.insert(GLFW_MOUSE_BUTTON_LEFT);frame();Check(host.captured,"script requests viewport capture");host.buttons.clear();host.keys={GLFW_KEY_W,GLFW_KEY_D};host.mx+=10;host.my-=5;frame();
 Near(host.camera.yaw,.04f,1e-6f,"mouse yaw defined by Lua sensitivity");Near(host.camera.pitch,.02f,1e-6f,"mouse pitch defined by Lua");Near(std::hypot(m.desiredDirection.x,m.desiredDirection.z),1,1e-6f,"diagonal normalized in character API");
 auto before=t.position;for(int i=0;i<8;++i)tick();Check(t.position.x>before.x&&t.position.z>before.z,"one frame command survives multiple physics ticks");Check(std::hypot(rb.velocity.x,rb.velocity.z)<=6.001f,"no diagonal acceleration");Near(t.position.y,.85f,.002f,"no movement dip");
 host.keys.clear();frame();tick();Near(std::hypot(rb.velocity.x,rb.velocity.z),0,.0001f,"Lua stops movement");
 host.keys.insert(GLFW_KEY_SPACE);frame();Check(m.jumpRequested,"one-frame jump queued before physics");frame();Check(m.jumpRequested,"zero-tick frame does not lose jump");tick();Check(rb.velocity.y>4,"Lua jump applied by C++ solver");
 for(int i=0;i<180;++i){frame();tick();}Check(m.grounded,"held Space lands without retrigger");Near(t.position.y,.85f,.002f,"jump landing");host.keys.clear();frame();host.keys.insert(GLFW_KEY_SPACE);frame();tick();Check(rb.velocity.y>4,"release then jump again");
 host.allowed=false;host.keys={GLFW_KEY_W};host.mx+=100;auto yaw=host.camera.yaw;frame();tick();Near(std::hypot(rb.velocity.x,rb.velocity.z),0,.0001f,"text editing blocks commands");Near(host.camera.yaw,yaw,0,"text editing blocks mouse look");
 host.allowed=true;script.enabled=false;frame();Check(lua.InstanceCount()==0&&!host.captured&&!host.camera.following,"disable retires input and camera lease");Check(!m.jumpRequested&&m.desiredDirection.x==0&&m.desiredDirection.z==0,"disable clears stale commands");
 auto vertical=rb.velocity.y;for(int i=0;i<4;++i)tick();Check(rb.velocity.y<vertical,"gravity continues with script disabled");
 w.RemoveComponent<LuaScriptComponent>(player);frame();for(int i=0;i<3;++i)tick();Near(std::hypot(rb.velocity.x,rb.velocity.z),0,.0001f,"no Lua component means no fallback WASD");
 auto& alternate=w.AddComponent<LuaScriptComponent>(player);alternate.scriptPath="scripts/KeyboardTurnController.lua";alternate.inputEnabled=alternate.cameraEnabled=true;host.keys={GLFW_KEY_E};host.mx+=500;frame();Check(host.camera.yaw>yaw&&host.camera.yaw<yaw+.1f,"replacement script turns with E and ignores mouse");
 lua.SuspendInput();Check(!host.captured&&!m.jumpRequested,"pause clears pending input/cursor");lua.Stop();Check(lua.InstanceCount()==0&&!host.camera.following,"Stop retires scripts");
 auto saved=host.input.CaptureBindings();host.input.BindNamedAction("Jump","Q");host.input.RestoreBindings(saved);host.keys={GLFW_KEY_SPACE};host.Sample();Check(host.input.FrameDown("Jump"),"editor action bindings restore after runtime remap");
}
static void FixedEdgesAndErrors(Assets& assets){
 auto path=assets.Write("fixed.lua",R"(return {on_start=function(self) Input.bind("Jump","Space");self.up=0;self.fixed=0;self.release=0 end,on_update=function(self) if Input.was_pressed("Jump") then self.up=self.up+1 end end,on_fixed_update=function(self) if Input.was_pressed("Jump") then self.fixed=self.fixed+1 end;if Input.was_released("Jump") then self.release=self.release+1 end;World.set_position(self.entity,Vec3.new(self.up,self.fixed,self.release)) end,on_stop=function(self) if World.is_alive(self.entity) then World.set_position(self.entity,Vec3.new(99,0,0)) end end})");
 World w;Host h;auto e=Actor(w,path);scripting::LuaRuntime lua;lua.Start(w,[&](const std::string& a){return h.input.FrameDown(a);},h.Services());auto frame=[&]{h.Sample();lua.Update(.016f);};frame();h.keys.insert(GLFW_KEY_SPACE);frame();frame();h.keys.clear();frame();
 for(int i=0;i<8;++i)lua.FixedUpdate(1.f/60);auto p=w.GetComponent<TransformComponent>(e)->position;Check(p.x==1&&p.y==1&&p.z==1,"edges buffered across zero ticks and delivered once to fixed phase");lua.Stop();Near(w.GetComponent<TransformComponent>(e)->position.x,99,0,"on_stop callback");
 auto denied=assets.Write("denied.lua",R"(return {on_update=function(self) assert(not Input.is_down("Jump"));Camera.rotate(1,0) end})");auto noPermission=Actor(w,denied);w.GetComponent<LuaScriptComponent>(noPermission)->inputEnabled=false;w.GetComponent<LuaScriptComponent>(noPermission)->cameraEnabled=false;lua.Start(w,[](const std::string&){return true;},h.Services());lua.Update(.01f);Check(lua.Error(denied).find("permission")!=std::string::npos,"camera permissions enforced safely");lua.Stop();
 auto invalid=assets.Write("invalid.lua",R"(return {on_update=function(self) Character.set_move_speed(self.entity,0/0) end})");World actors;auto actor=Actor(actors,invalid);actors.AddComponent<CharacterMovementComponent>(actor);actors.AddComponent<RigidbodyComponent>(actor);actors.AddComponent<ColliderComponent>(actor);lua.Start(actors);lua.Update(.01f);Check(lua.Error(invalid).find("valid range")!=std::string::npos,"nonfinite physical parameters rejected");lua.Stop();
 auto dead=assets.Write("dead.lua",R"(return {on_update=function(self) World.destroy(self.entity);Character.jump(self.entity) end})");actors.GetComponent<LuaScriptComponent>(actor)->scriptPath=dead;lua.Start(actors);lua.Update(.01f);Check(!actors.IsAlive(actor)&&lua.Error(dead).find("stale/foreign")!=std::string::npos,"destroyed entity commands rejected");lua.Stop();
 auto wrong=assets.Write("wrong.lua",R"(return {on_update=function(self) Character.jump(World.find_by_tag("Other")[1]) end})");World shared;auto owner=Actor(shared,wrong);auto other=Actor(shared,"");shared.AddComponent<TagComponent>(other).name="Other";shared.AddComponent<CharacterMovementComponent>(other);shared.AddComponent<RigidbodyComponent>(other);shared.AddComponent<ColliderComponent>(other);lua.Start(shared);lua.Update(.01f);Check(lua.Error(wrong).find("own entity")!=std::string::npos,"script cannot drive another character");lua.Stop();
 auto cameraPath=assets.Write("camera.lua",R"(return {on_start=function(self) if Camera.acquire() then Camera.follow(self.entity,Vec3.new(0,1,0)) end end})");World cameras;auto first=Actor(cameras,cameraPath),second=Actor(cameras,cameraPath);lua.Start(cameras,{},h.Services());lua.Update(.01f);Check(h.camera.target==first,"explicit exclusive camera claim");cameras.DestroyEntity(first);lua.Update(.01f);cameras.GetComponent<LuaScriptComponent>(second)->properties["restart"]=1;lua.Update(.01f);Check(h.camera.target==second,"camera lease released on destruction");lua.Stop();
 auto fresh=assets.Write("fresh.lua",R"(return {on_update=function(self) World.set_position(self.entity,Vec3.new(1,0,0)) end})");World sessions;auto target=Actor(sessions,fresh);lua.Start(sessions);lua.Update(.01f);lua.Stop();assets.Write("fresh.lua",R"(return {on_update=function(self) World.set_position(self.entity,Vec3.new(2,0,0)) end})");lua.Start(sessions);lua.Update(.01f);Near(sessions.GetComponent<TransformComponent>(target)->position.x,2,0,"new Play session reads edited active asset");lua.Stop();
 auto remap=assets.Write("remap.lua",R"(return {on_start=function(self) Input.bind("Jump","Q") end,on_update=function(self) assert(not Input.is_down("Jump"));assert(not Input.was_pressed("Jump")) end})");World rebinding;Actor(rebinding,remap);Host bindingHost;bindingHost.input.BindNamedAction("Jump","Space");bindingHost.keys={GLFW_KEY_SPACE};bindingHost.Sample();lua.Start(rebinding,[&](const std::string& a){return bindingHost.input.FrameDown(a);},bindingHost.Services());lua.Update(.01f);Check(lua.Error(remap).empty(),"same-frame remap cannot read old key state/edge");lua.Stop();
}
#if defined(ENABLE_DX12)
static void Native(Assets& assets){
 auto path=assets.Write("auto.lua",R"(return {on_start=function(self) Camera.acquire();Camera.follow(self.entity,Character.eye_offset(self.entity)) end,on_update=function(self) Character.move_world(self.entity,Vec3.new(1,0,0)) end})");
 Application app;Check(app.Initialize(),"native initialization");glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());SyntheticEditorInput input;app.NewScene();auto& ui=app.GetEditorLayer();ui.SetFixedDiagnosticLayout(true);auto start=ui.CreatePlayerStart(app);app.GetWorld().GetComponent<TransformComponent>(start)->position={0,2,5};app.GetWorld().GetComponent<PlayerStartComponent>(start)->controllerScript=path;
 auto floor=ui.CreatePrimitive(app,"plane");app.GetWorld().GetComponent<TransformComponent>(floor)->scale={64,1,64};auto& c=app.GetWorld().AddComponent<ColliderComponent>(floor);c.halfExtents={32,.025f,32};c.autoFitFromMesh=false;app.GetWorld().AddComponent<RigidbodyComponent>(floor).isStatic=true;
 auto before=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());app.SetEditorPlayMode(true);Entity player;for(auto e:app.GetWorld().Entities())if(app.GetWorld().HasComponent<PlayerComponent>(e))player=e;Check(app.GetWorld().IsAlive(player),"native spawn with selected script");app.GetWorld().GetComponent<LuaScriptComponent>(player)->inputEnabled=false;
 app.SetFrameLimit(240);Check(app.Run()==0,"native Lua controller scene");auto* t=app.GetWorld().GetComponent<TransformComponent>(player);Check(t->position.x>1&&std::abs(t->position.y-.875f)<.004f,"real Lua command/physics landing");auto pose=app.GetRenderInterpolation().Resolve(player,*t);Near(app.GetCameraPosition().x,pose.position.x,1e-5f,"camera follows interpolated character");
 app.SetEditorPaused(true);auto paused=t->position;app.SetFrameLimit(5);app.Run();Near(t->position.x,paused.x,0,"Pause stops physics");app.SetEditorPlayMode(false);Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==before&&app.GetLuaRuntime().InstanceCount()==0,"Stop preserves editor and destroys Lua runtime instances");Check(dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter())->GetValidationErrorCount()==0,"DX12 validation zero");
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{Assets assets;
#if defined(ENABLE_DX12)
 if(argc>1&&std::string(argv[1])=="--dx12")Native(assets);else
#endif
 {InputFrames();Fps();FixedEdgesAndErrors(assets);}std::cout<<"Lua gameplay controller: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
