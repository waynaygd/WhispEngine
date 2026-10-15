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
#include <imgui_internal.h>
#include <ImGuizmo.h>
#if defined(ENABLE_DX12)
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
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
static void CameraMouseRouting(){
 auto* context=ImGui::CreateContext();
 {
  editor::EditorLayer ui;auto& io=ImGui::GetIO();const auto flags=io.ConfigFlags;
  io.AddMousePosEvent(10,20);io.AddMouseButtonEvent(0,true);io.AddMouseWheelEvent(0,1);io.AddKeyEvent(ImGuiKey_W,true);
  {auto frame=ui.RouteCameraMouse(true,true,ImVec2(30,40));Check((io.ConfigFlags&ImGuiConfigFlags_NoMouse)&&!io.MouseDown[0],"camera frame suppresses UI mouse");Check(context->InputEventsQueue.Size==1&&context->InputEventsQueue[0].Type==ImGuiInputEventType_Key,"camera filters only mouse events, preserves keyboard");}
  Check(io.ConfigFlags==flags,"camera scope restores original ImGui flags");
  {auto frame=ui.RouteCameraMouse(false,true,ImVec2(30,40));Check(io.ConfigFlags&ImGuiConfigFlags_NoMouse,"secondary held button remains owned after camera release");}
  {auto frame=ui.RouteCameraMouse(false,false,ImVec2(30,40));Check(io.ConfigFlags&ImGuiConfigFlags_NoMouse,"release frame is drained");}
  {auto frame=ui.RouteCameraMouse(false,false,ImVec2(123,456));Check(!(io.ConfigFlags&ImGuiConfigFlags_NoMouse),"new gestures return to UI");auto& event=context->InputEventsQueue.back();Check(event.Type==ImGuiInputEventType_MousePos&&event.MousePos.PosX==123&&event.MousePos.PosY==456,"UI resumes at restored native pointer without virtual warp");}
  io.ConfigFlags|=ImGuiConfigFlags_NoMouse;{auto frame=ui.RouteCameraMouse(true,true,ImVec2());}Check(io.ConfigFlags&ImGuiConfigFlags_NoMouse,"preexisting mouse configuration preserved");
 }
 ImGui::DestroyContext(context);
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
 app.SetFrameLimit(480);Check(app.Run()==0,"native Lua controller scene");auto* t=app.GetWorld().GetComponent<TransformComponent>(player);Check(t->position.x>1&&std::abs(t->position.y-.875f)<.004f,"real Lua command/physics landing");auto pose=app.GetRenderInterpolation().Resolve(player,*t);Near(app.GetCameraPosition().x,pose.position.x,1e-5f,"camera follows interpolated character");
 app.SetEditorPaused(true);auto paused=t->position;app.SetFrameLimit(5);app.Run();Near(t->position.x,paused.x,0,"Pause stops physics");app.SetEditorPlayMode(false);Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==before&&app.GetLuaRuntime().InstanceCount()==0,"Stop preserves editor and destroys Lua runtime instances");Check(dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter())->GetValidationErrorCount()==0,"DX12 validation zero");
}
// Messages target this test's own HWND, exercising GLFW polling and Application's
// frame order rather than replacing Lua's host with a fake input service.
static void NativeEditorMouseInput(){
 Application app;Check(app.Initialize(),"editor mouse initialization");SyntheticEditorInput input;
 auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle();auto hwnd=glfwGetWin32Window(window);
 glfwShowWindow(window);glfwFocusWindow(window);SetActiveWindow(hwnd);SetFocus(hwnd);glfwPollEvents();
 app.NewScene();auto& ui=app.GetEditorLayer();ui.SetFixedDiagnosticLayout(true);
 auto frame=[&](unsigned n=4){app.SetFrameLimit(n);Check(app.Run()==0,"editor mouse frames");};
 frame();auto bounds=ui.GetViewportBounds();const float x=(bounds[0]+bounds[2])*.5f,y=(bounds[1]+bounds[3])*.5f;
 auto button=[&](int b,bool down){SendMessageW(hwnd,b==1?(down?WM_RBUTTONDOWN:WM_RBUTTONUP):(down?WM_LBUTTONDOWN:WM_LBUTTONUP),down?(b==1?MK_RBUTTON:MK_LBUTTON):0,MAKELPARAM(int(x),int(y)));SyntheticEditorInput::Button(b,down);};
 auto point=[&]{SetForegroundWindow(hwnd);SetActiveWindow(hwnd);SetFocus(hwnd);glfwSetCursorPos(window,x,y);SyntheticEditorInput::Position(x,y);ImGui::FocusWindow(ImGui::FindWindowByName("Viewport"));};
 button(0,false);button(1,false);point();frame();point();button(1,true);frame(1);
 Check(app.IsCameraControlActive(),"Edit RMB captures free-fly");
 auto* inspector=ImGui::FindWindowByName("Inspector");Check(inspector!=nullptr,"editor mouse Inspector exists");
 SyntheticEditorInput::Position(inspector->WorkRect.Min.x+20,inspector->WorkRect.Min.y+30);frame();
 Check(ImGui::GetCurrentContext()->HoveredWindow==nullptr&&ImGui::GetCurrentContext()->HoveredId==0,"captured virtual cursor cannot hover other UI windows");
 button(0,true);frame();Check(app.IsCameraControlActive()&&!ImGui::IsAnyItemActive()&&!ImGui::GetIO().MouseDown[0],"secondary click stays camera owned and cannot activate UI");
 button(1,false);frame();Check(!app.IsCameraControlActive()&&glfwGetInputMode(window,GLFW_CURSOR)==GLFW_CURSOR_NORMAL,"RMB release ends camera capture");
 Check(!ImGui::IsAnyItemActive()&&!ImGui::GetIO().MouseDown[0],"held secondary button cannot click through after release");
 button(0,false);frame();point();frame();Check(ui.IsViewportHovered(),"normal UI hover returns after all buttons released");
 Check(!(ImGui::GetIO().ConfigFlags&ImGuiConfigFlags_NoMouse),"capture does not leave global ImGui mouse disabled");
 button(1,true);frame();Check(app.IsCameraControlActive(),"RMB can capture again");
 SetActiveWindow(nullptr);SetFocus(nullptr);frame(1);Check(!app.IsCameraControlActive()&&glfwGetInputMode(window,GLFW_CURSOR)==GLFW_CURSOR_NORMAL,"focus loss safely releases editor capture");
 button(1,false);SetActiveWindow(hwnd);SetFocus(hwnd);frame();
 Check(!(ImGui::GetIO().ConfigFlags&ImGuiConfigFlags_NoMouse),"focus recovery restores ImGui mouse configuration");
}
static void NativeViewportDisplay(){
 Application app;Check(app.Initialize(),"display initialization");SyntheticEditorInput input;
 auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle();int originalWidth,originalHeight;glfwGetWindowSize(window,&originalWidth,&originalHeight);glfwHideWindow(window);
 SyntheticEditorInput::Queue([](ImGuiIO& io){io.AddFocusEvent(true);});
 app.NewScene();auto& ui=app.GetEditorLayer();auto cube=ui.CreatePrimitive(app,"cube");app.GetWorld().GetComponent<TransformComponent>(cube)->position={0,0,4};app.SetEditorCameraPose({0,0,-6},0,0);ui.SetGameView(false);
 auto frame=[&](unsigned n=3){app.SetFrameLimit(n);Check(app.Run()==0,"display frames");};frame(20);
 ImGui::FocusWindow(ImGui::FindWindowByName("Viewport"));frame();
 auto press=[&](ImGuiKey key){SyntheticEditorInput::Key(key,true);frame();SyntheticEditorInput::Key(key,false);frame();};
 auto shade=[&](ImGuiKey key){SyntheticEditorInput::Key(ImGuiMod_Alt,true);frame();press(key);SyntheticEditorInput::Key(ImGuiMod_Alt,false);frame();};
 auto* normal=ImGui::FindWindowByName("Viewport");const auto dock=normal->DockId;const auto pos=normal->Pos,size=normal->Size;
 app.SetViewportUnlit(true);shade(ImGuiKey_3);Check(!app.IsViewportUnlit(),"Edit Alt+3 Lit");shade(ImGuiKey_4);Check(app.IsViewportUnlit(),"Edit Alt+4 Unlit");
 press(ImGuiKey_F11);Check(ui.IsViewportMaximized(),"F11 maximizes in Edit");
 auto checkBounds=[&]{auto b=ui.GetViewportBounds();auto* main=ImGui::GetMainViewport();Near(b[0],main->Pos.x,1,"maximized left bound");Near(b[1],main->Pos.y,1,"maximized top bound");Near(b[2],main->Pos.x+main->Size.x,1,"maximized right bound");Near(b[3],main->Pos.y+main->Size.y,1,"maximized bottom bound");Check(ui.GetViewportPixelWidth()==int(std::lround(main->Size.x*ImGui::GetIO().DisplayFramebufferScale.x))&&ui.GetViewportPixelHeight()==int(std::lround(main->Size.y*ImGui::GetIO().DisplayFramebufferScale.y)),"fullscreen target and aspect match window");};checkBounds();
 Check(!ImGui::FindWindowByName("Inspector")->Active&&!ImGui::FindWindowByName("Main Toolbar")->Active,"fullscreen hides panels without changing preferences");
 Check(dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter())->ReadBackViewportPixels().size()==std::size_t(ui.GetViewportPixelWidth())*ui.GetViewportPixelHeight()*4,"fullscreen GPU target matches bounds");
 ui.ClearSelection();frame();auto full=ui.GetViewportBounds();const float cx=(full[0]+full[2])*.5f,cy=(full[1]+full[3])*.5f;
 SyntheticEditorInput::Position(cx,cy);frame();SyntheticEditorInput::Button(0,true);frame(1);SyntheticEditorInput::Button(0,false);frame();Check(ui.GetSelectedEntity()==cube,"fullscreen mouse picking selects Entity");
 bool gizmo=false;for(float dx:{15.f,20.f,25.f,30.f,35.f,45.f}){SyntheticEditorInput::Position(cx+dx,cy);frame(1);if(ImGuizmo::IsOver(ImGuizmo::TRANSLATE_X)){gizmo=true;break;}}Check(gizmo,"fullscreen transform gizmo uses current viewport bounds");
 ui.RecordEdit(app,"F11 transaction fixture");app.GetWorld().GetComponent<TransformComponent>(cube)->position.x=1;
 press(ImGuiKey_F11);Check(!ui.IsViewportMaximized()&&!ui.HasPendingTransaction()&&app.GetWorld().GetComponent<TransformComponent>(cube)->position.x==0,"F11 cancels pending transform edit safely");press(ImGuiKey_F11);Check(ui.IsViewportMaximized(),"fullscreen reentry after cancelled edit");
 std::size_t iniBytes=0;std::string ini=ImGui::SaveIniSettingsToMemory(&iniBytes);Check(ini.find("[Window][Viewport##Maximized]")==std::string::npos,"fullscreen window does not persist layout");
 glfwSetWindowSize(window,980,660);frame(5);checkBounds();press(ImGuiKey_F11);Check(!ui.IsViewportMaximized()&&normal->DockId==dock,"F11 restores previous dock assignment");
 // Restore the original outer size before comparing authored panel sizes.
 glfwSetWindowSize(window,originalWidth,originalHeight);frame(5);Check(ImGui::FindWindowByName("Inspector")->Active,"Inspector restored after fullscreen");Near(normal->Pos.x,pos.x,1,"normal viewport X restored");Near(normal->Pos.y,pos.y,1,"normal viewport Y restored");Near(normal->Size.x,size.x,1,"normal viewport width restored");Near(normal->Size.y,size.y,1,"normal viewport height restored");
 app.SetEditorPlayMode(true);frame();ImGui::FocusWindow(normal);frame();shade(ImGuiKey_3);Check(!app.IsViewportUnlit(),"Play Alt+3 Lit");shade(ImGuiKey_4);Check(app.IsViewportUnlit(),"Play Alt+4 Unlit");
 press(ImGuiKey_F11);Check(ui.IsViewportMaximized(),"Play F11 works without PlayerStart");app.SetEditorPaused(true);press(ImGuiKey_F11);Check(!ui.IsViewportMaximized(),"Pause F11 returns layout");press(ImGuiKey_F11);
 app.SetEditorPlayMode(false);frame();Check(ui.IsViewportMaximized(),"Stop retains fullscreen presentation");
 app.SetViewportUnlit(false);SyntheticEditorInput::Key(ImGuiMod_Alt,true);frame(1);ImGui::GetCurrentContext()->WantTextInputNextFrame=1;SyntheticEditorInput::Key(ImGuiKey_4,true);frame(1);Check(!app.IsViewportUnlit(),"text editing blocks display hotkeys");SyntheticEditorInput::Key(ImGuiKey_4,false);SyntheticEditorInput::Key(ImGuiMod_Alt,false);frame();
 ImGui::GetCurrentContext()->WantTextInputNextFrame=1;SyntheticEditorInput::Key(ImGuiKey_F11,true);frame(1);Check(ui.IsViewportMaximized(),"text editing blocks F11");SyntheticEditorInput::Key(ImGuiKey_F11,false);frame();press(ImGuiKey_F11);Check(!ui.IsViewportMaximized(),"final normal layout restored");
 Check(dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter())->GetValidationErrorCount()==0,"display DX12 validation clean");
}
static void NativePlayerInput(){
 Application app;Check(app.Initialize(),"input native initialization");SyntheticEditorInput input;
 auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle();auto hwnd=glfwGetWin32Window(window);
 glfwShowWindow(window);glfwFocusWindow(window);SetActiveWindow(hwnd);SetFocus(hwnd);glfwPollEvents();
 Check(glfwGetWindowAttrib(window,GLFW_FOCUSED)==GLFW_TRUE,"test window focused");
 app.NewScene();auto& ui=app.GetEditorLayer();ui.SetFixedDiagnosticLayout(true);
 auto frame=[&](unsigned n=4){app.SetFrameLimit(n);Check(app.Run()==0,"input native frames");};
 frame();auto bounds=ui.GetViewportBounds();float x=(bounds[0]+bounds[2])*.5f,y=(bounds[1]+bounds[3])*.5f;
 auto point=[&]{auto b=ui.GetViewportBounds();x=(b[0]+b[2])*.5f;y=(b[1]+b[3])*.5f;glfwSetCursorPos(window,x,y);SyntheticEditorInput::Position(x,y);ImGui::FocusWindow(ImGui::FindWindowByName(ui.IsViewportMaximized()?"Viewport##Maximized":"Viewport"));};
 auto key=[&](int k,bool down){SendMessageW(hwnd,down?WM_KEYDOWN:WM_KEYUP,k,(LPARAM(MapVirtualKeyW(k,MAPVK_VK_TO_VSC))<<16)|(down?0:LPARAM(0xc0000000)));};
 auto mouse=[&](bool down){SendMessageW(hwnd,down?WM_LBUTTONDOWN:WM_LBUTTONUP,down?MK_LBUTTON:0,MAKELPARAM(int(x),int(y)));SyntheticEditorInput::Button(0,down);};
 point();frame();auto editorPose=app.GetCameraPosition();auto editorYaw=app.GetCameraYaw();
 app.SetEditorPlayMode(true);frame();Check(!app.GetLuaRuntime().HasCameraOwner(),"no PlayerStart means no camera lease");
 SendMessageW(hwnd,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(int(x),int(y)));SyntheticEditorInput::Button(1,true);key('W',true);frame(20);
 Check(app.IsCameraControlActive(),"Play without player retains RMB free-fly");
 auto moved=app.GetCameraPosition();Check(std::abs(moved.x-editorPose.x)+std::abs(moved.y-editorPose.y)+std::abs(moved.z-editorPose.z)>.01f,"Play free-fly movement survives camera synchronization");
 double fx,fy;glfwGetCursorPos(window,&fx,&fy);glfwSetCursorPos(window,fx+20,fy-5);frame();Check(app.GetCameraYaw()>editorYaw,"Play free-fly mouse look works");
 app.SetEditorPaused(true);frame();Check(app.IsCameraControlActive(),"Pause does not steal editor free-fly capture");
 app.SetEditorPlayMode(false);Near(app.GetCameraPosition().x,editorPose.x,0,"Stop restores editor camera X");Near(app.GetCameraPosition().z,editorPose.z,0,"Stop restores editor camera Z");Near(app.GetCameraYaw(),editorYaw,0,"Stop restores editor yaw");
 Check(!app.IsCameraControlActive()&&glfwGetInputMode(window,GLFW_CURSOR)==GLFW_CURSOR_NORMAL,"Stop releases free-fly even while RMB held");frame();Near(app.GetCameraPosition().z,editorPose.z,0,"held RMB cannot move restored Stop pose");
 key('W',false);SendMessageW(hwnd,WM_RBUTTONUP,0,MAKELPARAM(int(x),int(y)));SyntheticEditorInput::Button(1,false);frame();
 auto start=ui.CreatePlayerStart(app);app.GetWorld().GetComponent<TransformComponent>(start)->position={0,2,0};
 app.GetWorld().GetComponent<PlayerStartComponent>(start)->characterHeight=2;app.GetWorld().GetComponent<PlayerStartComponent>(start)->eyeHeight=1.8f;
 app.SetEditorPlayMode(true);Near(app.GetCameraPosition().y,3.8f,.0001f,"initial camera uses configured body and eye height");frame();Entity player;for(auto e:app.GetWorld().Entities())if(app.GetWorld().HasComponent<PlayerComponent>(e))player=e;
 auto* script=app.GetWorld().GetComponent<LuaScriptComponent>(player);Check(script&&script->inputEnabled&&script->cameraEnabled,"spawn assigns Lua permissions");
 Check(script->scriptPath=="scripts/FirstPersonController.lua"&&app.GetLuaRuntime().Error(script->scriptPath).empty()&&app.GetLuaRuntime().InstanceCount()==1&&app.GetLuaRuntime().HasCameraOwner(),"active asset loads, starts and acquires camera");
 point();frame();mouse(true);auto* viewport=ImGui::FindWindowByName("Viewport");ImGui::SetActiveID(viewport->MoveId,viewport);
 Check(ImGui::IsAnyItemActive()&&!ui.BlocksGameplayInput(),"viewport MoveId allows gameplay capture");
 auto* inspector=ImGui::FindWindowByName("Inspector");Check(inspector!=nullptr,"Inspector exists in fixture");ImGui::SetActiveID(inspector->GetID("Value"),inspector);Check(ui.BlocksGameplayInput(),"Inspector widget retains input protection");
 ImGui::SetActiveID(viewport->GetID("RealWidget"),viewport);Check(ui.BlocksGameplayInput(),"real viewport widgets block gameplay");
 ImGui::SetActiveID(viewport->MoveId,viewport);frame();
 Check(glfwGetInputMode(window,GLFW_CURSOR)==GLFW_CURSOR_DISABLED,"Lua captures real GLFW cursor despite viewport active ID");mouse(false);key('W',true);frame(20);
 auto* movement=app.GetWorld().GetComponent<CharacterMovementComponent>(player);auto* body=app.GetWorld().GetComponent<RigidbodyComponent>(player);
 Check(std::hypot(movement->desiredDirection.x,movement->desiredDirection.z)>.9f&&std::hypot(body->velocity.x,body->velocity.z)>3.9f,"GLFW input reaches Lua then fixed CharacterMovement");
 Check(!app.IsCameraControlActive(),"Lua camera excludes editor free-fly");
 auto interpolated=app.GetRenderInterpolation().Resolve(player,*app.GetWorld().GetComponent<TransformComponent>(player));Near(app.GetCameraPosition().y-interpolated.position.y,.8f,.0001f,"Lua eye_offset follows configured height through interpolation");
 SyntheticEditorInput::Position(inspector->WorkRect.Min.x+10,inspector->WorkRect.Min.y+25);mouse(true);SendMessageW(hwnd,WM_RBUTTONDOWN,MK_RBUTTON,0);SyntheticEditorInput::Button(1,true);frame();
 Check(glfwGetInputMode(window,GLFW_CURSOR)==GLFW_CURSOR_DISABLED&&ImGui::GetCurrentContext()->HoveredWindow==nullptr&&!ImGui::IsAnyItemActive()&&!ImGui::GetIO().MouseDown[0]&&!ImGui::GetIO().MouseDown[1],"Play capture filters virtual hover and both buttons from UI");mouse(false);SendMessageW(hwnd,WM_RBUTTONUP,0,0);SyntheticEditorInput::Button(1,false);frame();
 auto displayKey=[&](ImGuiKey key){SyntheticEditorInput::Key(key,true);frame();SyntheticEditorInput::Key(key,false);frame();};
 displayKey(ImGuiKey_F11);Check(ui.IsViewportMaximized()&&glfwGetInputMode(window,GLFW_CURSOR)==GLFW_CURSOR_DISABLED,"F11 retains captured Lua cursor");
 SyntheticEditorInput::Key(ImGuiMod_Alt,true);frame();displayKey(ImGuiKey_4);Check(app.IsViewportUnlit(),"captured fullscreen Play Alt+4 Unlit");displayKey(ImGuiKey_3);Check(!app.IsViewportUnlit(),"captured fullscreen Play Alt+3 Lit");SyntheticEditorInput::Key(ImGuiMod_Alt,false);frame();
 auto yaw=app.GetCameraYaw();double mx,my;glfwGetCursorPos(window,&mx,&my);glfwSetCursorPos(window,bounds[2]+200,my-8);frame();
 Check(app.GetCameraYaw()>yaw+.01f,"disabled-cursor relative mouse reaches Lua camera");
 key('W',false);key(VK_TAB,true);frame();Check(glfwGetInputMode(window,GLFW_CURSOR)==GLFW_CURSOR_NORMAL,"Tab releases gameplay cursor");key(VK_TAB,false);frame();
 point();frame();mouse(true);frame();mouse(false);frame();Check(glfwGetInputMode(window,GLFW_CURSOR)==GLFW_CURSOR_DISABLED,"second viewport click recaptures");
 displayKey(ImGuiKey_F11);Check(!ui.IsViewportMaximized()&&glfwGetInputMode(window,GLFW_CURSOR)==GLFW_CURSOR_DISABLED,"F11 restores layout without releasing Lua capture");
 auto sink=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"QoL test focus",WS_POPUP,0,0,1,1,hwnd,nullptr,GetModuleHandleW(nullptr),nullptr);Check(sink!=nullptr,"owned focus sink");
 ShowWindow(sink,SW_SHOWNOACTIVATE);SetActiveWindow(sink);SetFocus(sink);Check(!glfwGetWindowAttrib(window,GLFW_FOCUSED),"native test window loses focus");frame(1);Check(glfwGetInputMode(window,GLFW_CURSOR)==GLFW_CURSOR_NORMAL,"Play focus loss releases cursor");DestroyWindow(sink);SetActiveWindow(hwnd);SetFocus(hwnd);point();frame();mouse(true);frame();mouse(false);frame();Check(glfwGetInputMode(window,GLFW_CURSOR)==GLFW_CURSOR_DISABLED,"Play recapture after focus recovery");
 ImGui::GetIO().WantTextInput=true;frame(1);Check(glfwGetInputMode(window,GLFW_CURSOR)==GLFW_CURSOR_NORMAL,"text editing releases gameplay capture");ImGui::GetIO().WantTextInput=false;
 script->scriptPath="scripts/KeyboardTurnController.lua";point();frame();yaw=app.GetCameraYaw();key('E',true);key('W',true);frame(20);
 Check(app.GetCameraYaw()>yaw&&std::hypot(movement->desiredDirection.x,movement->desiredDirection.z)>.9f&&glfwGetInputMode(window,GLFW_CURSOR)==GLFW_CURSOR_NORMAL,"keyboard alternative moves and turns without capture");key('E',false);key('W',false);
 yaw=app.GetCameraYaw();key('Q',true);frame(20);Check(app.GetCameraYaw()<yaw,"keyboard alternative turns with Q");key('Q',false);
 app.GetWorld().RemoveComponent<LuaScriptComponent>(player);key('W',true);frame(20);Near(std::hypot(body->velocity.x,body->velocity.z),0,.001f,"no controller means no automatic player WASD");key('W',false);
 auto& restarted=app.GetWorld().AddComponent<LuaScriptComponent>(player);restarted.scriptPath="scripts/FirstPersonController.lua";restarted.inputEnabled=restarted.cameraEnabled=true;frame();displayKey(ImGuiKey_F11);point();frame();mouse(true);frame();mouse(false);frame();Check(ui.IsViewportMaximized()&&glfwGetInputMode(window,GLFW_CURSOR)==GLFW_CURSOR_DISABLED,"fullscreen FPS recaptured before Stop");
 key(VK_ESCAPE,true);frame();Check(!app.IsEditorPlayMode()&&ui.IsViewportMaximized()&&glfwGetInputMode(window,GLFW_CURSOR)==GLFW_CURSOR_NORMAL&&app.GetLuaRuntime().InstanceCount()==0,"Escape stops fullscreen FPS and releases input");key(VK_ESCAPE,false);
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{Assets assets;
#if defined(ENABLE_DX12)
 if(argc>1&&std::string(argv[1])=="--dx12"){Native(assets);NativeEditorMouseInput();NativeViewportDisplay();NativePlayerInput();}else
#endif
 {InputFrames();CameraMouseRouting();Fps();FixedEdgesAndErrors(assets);}std::cout<<"Lua gameplay controller: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
