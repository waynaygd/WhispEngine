#define NOMINMAX
#include "ecs/World.h"
#include "ecs/systems/PhysicsSystem.h"
#include "ecs/components/TransformComponent.h"
#include "ecs/components/RigidbodyComponent.h"
#include "ecs/components/ColliderComponent.h"
#include "ecs/PlayerSpawn.h"
#include "core/Application.h"
#include "scene/SceneSerializer.h"
#include "EditorTestPreferences.h"
#include "editor/LightGizmoMath.h"
#if defined(ENABLE_DX12)
#include "SyntheticEditorInput.h"
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <GLFW/glfw3.h>
#endif
#include <filesystem>
#include <iostream>
#include <cmath>
#include <stdexcept>
using namespace ecs;
static void Check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
static void Reproduce(){
 World w;PhysicsSystem physics(nullptr,9.81f,.985f,3,.05f,.85f,12);
 auto floor=w.CreateEntity();w.AddComponent<TransformComponent>(floor).position={0,-1.371443f,0};
 w.AddComponent<ColliderComponent>(floor).halfExtents={24.244f,.025f,24.244f};w.AddComponent<RigidbodyComponent>(floor).isStatic=true;
 auto e=w.CreateEntity();w.AddComponent<TransformComponent>(e).position={0,3,0};
 auto& c=w.AddComponent<ColliderComponent>(e);c.halfExtents={.3f,.85f,.3f};c.autoFitFromMesh=false;c.restitution=0;c.friction=.8f;
 auto& rb=w.AddComponent<RigidbodyComponent>(e);rb.mass=70;
 for(int i=0;i<600;++i)physics.Update(w,1.f/60);
 float initial=w.GetComponent<TransformComponent>(e)->position.y,minimum=initial;
 for(int i=0;i<120;++i){rb.velocity.z=4;rb.sleeping=false;rb.sleepTimer=0;physics.Update(w,1.f/60);minimum=std::min(minimum,w.GetComponent<TransformComponent>(e)->position.y);}
 auto& t=*w.GetComponent<TransformComponent>(e);
 std::cout<<"unconstrained box: rest="<<initial<<" minimum moving="<<minimum<<" dip="<<initial-minimum<<" pitch="<<t.rotation.x<<" roll="<<t.rotation.z<<'\n';
}
static void Movement(float dt){
 World w;EventBus events;PhysicsSystem physics(&events,9.81f,.985f,3,.05f,.85f,12);
 auto floor=w.CreateEntity();w.AddComponent<TransformComponent>(floor).position={0,-1.371443f,0};
 w.AddComponent<ColliderComponent>(floor).halfExtents={24.244f,.025f,24.244f};w.AddComponent<RigidbodyComponent>(floor).isStatic=true;
 auto start=w.CreateEntity();w.AddComponent<TransformComponent>(start).position={0,3,0};w.AddComponent<PlayerStartComponent>(start);
 auto player=SpawnPlayer(w);Check(!w.IsAlive(start),"runtime marker removed");
 events.SubscribeCollision([&](const CollisionEvent& e){GroundCharacter(w,player,e);});
 auto tick=[&]{ApplyCharacterMovement(w,player);physics.Update(w,dt);};
 auto& m=*w.GetComponent<CharacterMovementComponent>(player);auto& rb=*w.GetComponent<RigidbodyComponent>(player);auto& t=*w.GetComponent<TransformComponent>(player);
 for(int i=0;i<int(5/dt);++i)tick();Check(m.grounded,"landed on floor");Check(rb.sleeping,"rest sleeps");
 const float rest=t.position.y;Check(std::abs(rest-(-1.371443f+.025f+.85f))<.002f,"box resting geometry");
 SampleCharacterInput(m,{0,0,1},false,true);float minimum=rest;
 for(int i=0;i<int(1/dt);++i){tick();minimum=std::min(minimum,t.position.y);}
 std::cout<<"locked box dt="<<dt<<" dip="<<rest-minimum<<" pitch="<<t.rotation.x<<" roll="<<t.rotation.z<<'\n';
 Check(rest-minimum<.002f,"extra dip when waking/moving");Check(t.position.z>3,"horizontal speed");Check(std::abs(t.rotation.x)+std::abs(t.rotation.z)<1e-6f,"upright body");
 SampleCharacterInput(m,{},false,false);tick();Check(std::abs(rb.velocity.x)+std::abs(rb.velocity.z)<.0001f,"released input stops");
 m.grounded=false;GroundCharacter(w,player,{player,floor,{1,0,0}});Check(!m.grounded,"wall is not ground");
 tick();Check(m.grounded,"floor reestablishes grounded");
 SampleCharacterInput(m,{},true,true);tick();Check(rb.velocity.y>4,"jump");
 SampleCharacterInput(m,{},true,true);Check(!m.jumpRequested,"held Space not queued");
 for(int i=0;i<int(2/dt);++i){SampleCharacterInput(m,{},true,true);tick();}
 Check(m.grounded&&std::abs(t.position.y-rest)<.002f,"held jump lands without repeating");
 SampleCharacterInput(m,{},false,true);SampleCharacterInput(m,{},true,true);tick();Check(rb.velocity.y>4,"release then jump again");
 for(int i=0;i<int(2/dt);++i)tick();
 auto wall=w.CreateEntity();w.AddComponent<TransformComponent>(wall).position={0,0,t.position.z+2};
 w.AddComponent<ColliderComponent>(wall).halfExtents={2,3,.2f};w.AddComponent<RigidbodyComponent>(wall).isStatic=true;
 float wallZ=w.GetComponent<TransformComponent>(wall)->position.z;
 SampleCharacterInput(m,{0,0,1},false,true);for(int i=0;i<int(1/dt);++i)tick();
 Check(t.position.z<=wallZ-.2f-PlayerHalfExtents.z+.002f,"wall collision");
 // Walking off a finite floor must still fall; no per-frame Y clamp.
 SampleCharacterInput(m,{1,0,0},false,true);for(int i=0;i<int(8/dt);++i)tick();Check(t.position.y<rest-1,"gravity after leaving floor");
}
static void Workflow(){
 Application app;auto& ui=app.GetEditorLayer();app.NewScene();auto old=ui.CreateEntity(app,"Previous");ui.MarkSaved();auto count=ui.UndoCount();
 auto start=ui.CreatePlayerStart(app);auto& w=app.GetWorld();
 Check(w.HasComponent<TagComponent>(start)&&w.GetComponent<TagComponent>(start)->name=="PlayerStart","marker tag");
 Check(w.HasComponent<TransformComponent>(start)&&w.GetComponent<PlayerStartComponent>(start)->enabled,"marker defaults");
 Check(!w.HasComponent<RigidbodyComponent>(start)&&!w.HasComponent<ColliderComponent>(start),"marker nonphysical");
 Check(ui.GetSelectedEntity()==start&&ui.IsSceneDirty()&&ui.UndoCount()==count+1,"selection/dirty/single undo");
 ui.UndoEdit(app);Check(!w.IsAlive(start)&&ui.GetSelectedEntity()==old,"undo creation restores selection");ui.RedoEdit(app);start=ui.GetSelectedEntity();
 auto& t=*w.GetComponent<TransformComponent>(start);t.position={1,2,3};t.rotation={.2f,.7f,0};t.scale={9,9,9};
 auto steep=t;steep.rotation.x=3.14f;Check(PlayerLookDirection(steep).y>.99f,"preview uses runtime pitch limit");
 auto low=PlayerBoxPoint(t,{-PlayerHalfExtents.x,-PlayerHalfExtents.y,-PlayerHalfExtents.z});
 auto high=PlayerBoxPoint(t,{PlayerHalfExtents.x,PlayerHalfExtents.y,PlayerHalfExtents.z});Check(std::abs(high.y-low.y-1.7f)<1e-6f&&std::abs(low.y-t.position.y)<1e-6f,"preview feet/full height");
 auto before=SceneSerializer::CaptureWorld(w,app.GetSceneSettings());auto history=ui.UndoCount();app.SetEditorPlayMode(true);
 auto& runtime=app.GetWorld();Entity player;for(auto e:runtime.Entities()){Check(!runtime.HasComponent<PlayerStartComponent>(e),"no runtime marker");if(runtime.HasComponent<PlayerComponent>(e))player=e;}
 Check(runtime.IsAlive(player),"player spawned");auto& pt=*runtime.GetComponent<TransformComponent>(player);auto& collider=*runtime.GetComponent<ColliderComponent>(player);
 Check(std::abs(pt.position.y-2.85f)<1e-6f&&pt.rotation.x==0&&pt.scale.x==1,"feet spawn, upright, unscaled");
 Check(collider.halfExtents.x==PlayerHalfExtents.x&&collider.halfExtents.y==PlayerHalfExtents.y&&collider.halfExtents.z==PlayerHalfExtents.z&&!collider.autoFitFromMesh,"preview/runtime same dimensions");
 Check(std::abs(app.GetCameraPosition().y-3.7f)<1e-6f,"eyes measured from feet");Check(!ui.CreatePlayerStart(app).IsValid(),"Play creation blocked");
 app.SetEditorPlayMode(false);Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==before&&ui.UndoCount()==history&&ui.GetSelectedEntity()==start,"Stop preserves scene/history/selection");
 auto entity=w.CreateEntity();ComponentRegistry::Builtins().Restore(w,entity,{{"Player",{{"moveSpeed",7.f},{"eyeHeight",1.7f}}}});
 Check(w.GetComponent<CharacterMovementComponent>(entity)->moveSpeed==7,"legacy speed migration");
 ComponentRegistry::Builtins().Restore(w,entity,{{"Player",{{"moveSpeed",7.f}}},{"CharacterMovement",{{"moveSpeed",5.f}}}});
 Check(w.GetComponent<CharacterMovementComponent>(entity)->moveSpeed==5,"explicit movement speed wins migration");
 app.NewScene();app.SetEditorPlayMode(true);Check(app.GetWorld().Entities().empty(),"no marker safe");app.SetEditorPlayMode(false);
 World markers;auto disabled=markers.CreateEntity();markers.AddComponent<PlayerStartComponent>(disabled).enabled=false;markers.AddComponent<RigidbodyComponent>(disabled);markers.AddComponent<ColliderComponent>(disabled);markers.AddComponent<TransformComponent>(disabled);
 auto active=markers.CreateEntity();markers.AddComponent<PlayerStartComponent>(active);markers.AddComponent<TransformComponent>(active).position={3,4,5};
 auto spawned=SpawnPlayer(markers);Check(markers.Entities().size()==1&&markers.GetComponent<TransformComponent>(spawned)->position.x==3,"first active marker; all legacy physical markers removed");
}
#if defined(ENABLE_DX12)
static void Gpu(){
 Application app;Check(app.Initialize(),"app init");glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());SyntheticEditorInput input;app.NewScene();auto& ui=app.GetEditorLayer();ui.SetFixedDiagnosticLayout(true);ui.SetGameView(false);
 auto floor=ui.CreatePrimitive(app,"plane");auto& w=app.GetWorld();w.GetComponent<TransformComponent>(floor)->scale={24.244f,1,24.244f};w.GetComponent<TransformComponent>(floor)->position={0,-1.371443f,0};
 auto& c=w.AddComponent<ColliderComponent>(floor);c.halfExtents={24.244f,.025f,24.244f};c.autoFitFromMesh=false;w.AddComponent<RigidbodyComponent>(floor).isStatic=true;
 auto marker=ui.CreatePlayerStart(app);w.GetComponent<TransformComponent>(marker)->position={0,1,5};w.GetComponent<TransformComponent>(marker)->rotation.y=.7f;
 auto root=std::filesystem::current_path()/"../../diagnostics/player";std::filesystem::create_directories(root);
 app.SetFrameCaptureOutput((root/"player-start-editor.ppm").string());app.SetFrameLimit(90);Check(app.Run()==0,"editor scene run");
 ui.ClearSelection();app.SetFrameLimit(2);app.SetFrameCaptureOutput({});app.Run();
 auto bounds=ui.GetViewportBounds();float width=bounds[2]-bounds[0],height=bounds[3]-bounds[1];
 auto camera=editor::GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),width/height,app.GetCameraNearPlane(),app.GetCameraFarPlane());
 auto icon=editor::ProjectGizmo(w.GetComponent<TransformComponent>(marker)->position,camera);Check(bool(icon),"marker icon visible");
 SyntheticEditorInput::Position(bounds[0]+(*icon)[0]*width-8,bounds[1]+(*icon)[1]*height-24);app.Run();
 SyntheticEditorInput::Button(0,true);app.Run();SyntheticEditorInput::Button(0,false);app.Run();Check(ui.GetSelectedEntity()==marker,"actual icon click selects marker before mesh picking");
 app.SetFrameCaptureOutput((root/"player-start-editor.ppm").string());app.Run();
 auto before=SceneSerializer::CaptureWorld(w,app.GetSceneSettings());app.SetEditorPlayMode(true);app.SetFrameCaptureOutput((root/"player-runtime.ppm").string());app.SetFrameLimit(240);Check(app.Run()==0,"runtime scene run");
 Entity player;for(auto e:app.GetWorld().Entities())if(app.GetWorld().HasComponent<PlayerComponent>(e))player=e;
 Check(app.GetWorld().IsAlive(player),"GPU runtime player");auto* t=app.GetWorld().GetComponent<TransformComponent>(player);Check(std::abs(t->position.y-(-1.371443f+.025f+.85f))<.003f,"actual runtime floor landing");
 auto rendered=app.GetRenderInterpolation().Resolve(player,*t);Check(std::abs(app.GetCameraPosition().y-PlayerEyePosition(rendered.position,1.7f).y)<1e-6f,"camera uses same interpolated physical pose");
 app.SetEditorPaused(true);auto paused=t->position;app.SetFrameLimit(4);app.SetFrameCaptureOutput({});app.Run();Check(t->position.y==paused.y,"Pause freezes player physics");app.SetEditorPaused(false);
 auto* renderer=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(renderer&&renderer->GetValidationErrorCount()==0,"DX12 validation");app.SetEditorPlayMode(false);Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==before,"rendered Play isolated");
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{
#if defined(ENABLE_DX12)
 if(argc>1&&std::string(argv[1])=="--dx12"){Gpu();std::cout<<"Player DX12 scene passed\n";return 0;}
#endif
 Reproduce();Movement(1.f/60);Movement(1.f/120);Workflow();std::cout<<"Character movement/workflow passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
