#define NOMINMAX
#include "EditorTestPreferences.h"
#include "ecs/Hierarchy.h"
#include "ecs/TransformAccess.h"
#include "ecs/ComponentRegistry.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "ecs/components/TagComponent.h"
#include "ecs/components/VelocityComponent.h"
#include "ecs/components/BoundsBounceComponent.h"
#include "ecs/systems/MotionSystem.h"
#include "ecs/systems/BoundsBounceSystem.h"
#include "ecs/systems/RenderSystem.h"
#include "ecs/components/MeshRendererComponent.h"
#include "ecs/components/MaterialComponent.h"
#include "resources/ResourceManager.h"
#include "render/IRenderAdapter.h"
#include "ecs/HierarchyPhysics.h"
#include "ecs/PhysicsQueries.h"
#include "ecs/systems/PhysicsSystem.h"
#include "ecs/LightGather.h"
#include "scripting/GameplayServices.h"
#include "scripting/LuaRuntime.h"
#include "scripting/Prefab.h"
#include "ecs/PlayerSpawn.h"
#include "ecs/MutationQueue.h"
#include "scene/SceneSerializer.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <source_location>
#if defined(ENABLE_DX12)
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <GLFW/glfw3.h>
#endif
using namespace ecs;
static int checks=0;
static void Check(bool value,const char* message,std::source_location at=std::source_location::current()){if(!value)throw std::runtime_error(std::string(message)+" line "+std::to_string(at.line()));++checks;}
static void Near(double a,double b,double tolerance=3e-4){Check(std::isfinite(a)&&std::abs(a-b)<=tolerance,"numeric oracle");}
static Entity Object(World& w,const char* id,Vec3 position={}){auto e=w.CreateEntity();w.AddComponent<SceneIdentityComponent>(e).value=id;w.AddComponent<TransformComponent>(e).position=position;w.AddComponent<TagComponent>(e).name=id;return e;}
template<class F>static void Reject(World& w,F operation){auto before=nlohmann::json::array();for(auto e:w.Entities())before.push_back(ComponentRegistry::Builtins().Capture(w,e));auto identity=w.GetIdentity();bool threw=false;try{operation();}catch(const std::exception&){threw=true;}auto after=nlohmann::json::array();for(auto e:w.Entities())after.push_back(ComponentRegistry::Builtins().Capture(w,e));Check(threw&&before==after&&identity==w.GetIdentity(),"unsupported operation preserves World");}
static void BlockA(){
 World data(World::DataMode::HierarchyData);auto p=Object(data,"parent"),c=Object(data,"child",{2,0,0}),g=Object(data,"grandchild",{3,0,0});Check(SetParent(data,c,p)&&SetParent(data,g,c),"fixture");
 World::CloneMap map;auto runtime=data.CloneData(&map);auto parent=map.at(p),child=map.at(c),grand=map.at(g);runtime->AddComponent<VelocityComponent>(parent).linear={4,0,0};MotionSystem motion;motion.Update(*runtime,.5f);
 Near(ReadWorldPosition(*runtime,child)->x,4);Near(ReadWorldPosition(*runtime,grand)->x,5);Near(ReadWorldPosition(data,c)->x,2);Check(!runtime->IsAlive(p),"editor handles fenced");
 auto& bounds=runtime->AddComponent<BoundsBounceComponent>(parent);bounds.minX=-1;bounds.maxX=1;BoundsBounceSystem bounce;bounce.Update(*runtime,0);Near(ReadWorldPosition(*runtime,grand)->x,4);Near(runtime->GetComponent<VelocityComponent>(parent)->linear.x,-4);
 runtime->DestroyEntity(child);auto fresh=Object(*runtime,"fresh");Check(!ReadWorldPosition(*runtime,child)&&fresh.generation!=child.generation,"generation reuse fences reads");Near(ReadWorldPosition(data,g)->x,3);
}
struct RuntimeRecorder:IRenderAdapter {
 std::vector<RenderInstanceData> main,shadow;std::vector<MaterialRenderState> states;RenderInstanceData pending;MaterialRenderState material;RenderSubmissionStatistics stats;std::uint64_t next=1;unsigned debugCubes=0;
 bool Initialize(IWindow*)override{return true;}void BeginFrame()override{main.clear();shadow.clear();states.clear();stats={};}
 void Clear(float,float,float,float)override{}void SetTestTransform(const float* p)override{std::copy(p,p+16,pending.mvp.begin());}void SetTestColor(float r,float g,float b,float a)override{pending.tint={r,g,b,a};}
 void SetModelMatrix(const ShadowMatrix& m)override{pending.model=m;}void SetNormalMatrix(const std::array<float,12>& n)override{pending.normal=n;}void SetMaterialState(const MaterialRenderState& m)override{material=m;}
 RenderMeshHandle UploadMesh(const MeshData&)override{return {next++};}RenderTextureHandle CreateTexture2D(const TextureData&)override{return {next++};}RenderShaderHandle CreateShaderProgram(const ShaderResource&)override{return {next++};}
 bool SupportsInstancing(RenderShaderHandle)const override{return true;}
 void DrawMesh(RenderMeshHandle)override{main.push_back(pending);states.push_back(material);++stats.drawCalls;++stats.renderedInstances;}
 bool DrawMeshInstanced(RenderMeshHandle,std::span<const RenderInstanceData> d)override{main.insert(main.end(),d.begin(),d.end());states.insert(states.end(),d.size(),material);++stats.drawCalls;++stats.instancedDrawCalls;stats.renderedInstances+=d.size();return true;}
 bool BeginShadowPass(const ShadowFrame&)override{return true;}void DrawShadowMesh(RenderMeshHandle,const RenderInstanceData& d)override{shadow.push_back(d);}
 bool DrawShadowMeshInstanced(RenderMeshHandle,std::span<const RenderInstanceData> d)override{shadow.insert(shadow.end(),d.begin(),d.end());return true;}
 RenderSubmissionStatistics GetSubmissionStatistics()const override{return stats;}
 void DrawTestTriangle()override{}void DrawTestLine()override{}void DrawTestQuad()override{}void DrawTestCube()override{++debugCubes;}void EndFrame()override{}void Present()override{}void Shutdown()override{}
};
static void Mesh(World& w,Entity e){auto& m=w.AddComponent<MeshRendererComponent>(e);m.meshPath="models/validation_cube.obj";m.shaderPath="dx12/textured.hlsl";m.texturePath="defaults/texture";w.AddComponent<MaterialComponent>(e).materialPath="materials/stress.material.json";}
static void BlockB(){
 RuntimeRecorder adapter;ResourceManager resources(nullptr);resources.Load<MeshResource>("models/validation_cube.obj");resources.Load<ShaderResource>("dx12/textured.hlsl");resources.Load<TextureResource>("defaults/texture");resources.Load<MaterialResource>("materials/stress.material.json");
 RenderSystem render;render.SetResourceManager(&resources);render.SetRenderAdapter(&adapter);RenderInterpolation interpolation;render.SetInterpolation(&interpolation);
 World world(World::DataMode::HierarchyData);auto p=Object(world,"parent"),c=Object(world,"child",{2,0,0}),g=Object(world,"grandchild",{3,0,0});Check(SetParent(world,c,p)&&SetParent(world,g,c),"render hierarchy");Mesh(world,c);Mesh(world,g);
 world.GetComponent<TransformComponent>(c)->rotation.z=.78539816339f;Check(TrySetWorldScale(world,p,{2,1,1}),"render shear fixture");interpolation.BeginTick(world);Check(TrySetWorldPosition(world,p,{4,0,0}),"render parent write");interpolation.EndTick(world);interpolation.SetFrame(world,true,.2);Check(interpolation.Count()==0,"hierarchy interpolation explicitly disabled");
 for(int i=0;i<12;++i){adapter.BeginFrame();render.Update(world,0);}
 auto verify=[&]{Check(adapter.main.size()==2&&adapter.shadow.size()==2,"actual mesh and shadow submissions");for(auto entity:{c,g}){auto expected=*ReadWorldMatrix(world,entity);auto at=std::find_if(adapter.main.begin(),adapter.main.end(),[&](auto& d){return std::abs(d.model[12]-expected[12])<1e-4&&std::abs(d.model[13]-expected[13])<1e-4;});Check(at!=adapter.main.end(),"matrix located by world translation");for(int i=0;i<16;++i)Near(at->model[i],expected[i]);Check(std::any_of(adapter.shadow.begin(),adapter.shadow.end(),[&](auto& d){return d.model==at->model;}),"shadows use same full affine matrix");
   // Inverse-transpose normal must remain perpendicular to transformed tangents.
   double dot=0;for(int row=0;row<3;++row)dot+=at->normal[row*4+1]*at->model[row];Near(dot,0);
  }Check(render.GetShadowWorldBounds().size()==2,"world bounds produced");
  const auto& mesh=resources.Get<MeshResource>("models/validation_cube.obj")->GetData().meshData;std::size_t index=0;
  for(auto entity:{c,g}){auto m=*ReadWorldMatrix(world,entity);const auto& b=render.GetShadowWorldBounds()[index++];for(int row=0;row<3;++row){double lo=INFINITY,hi=-INFINITY;for(const auto& vertex:mesh.vertices){double value=m[12+row];for(int col=0;col<3;++col)value+=m[col*4+row]*vertex.position[col];lo=std::min(lo,value);hi=std::max(hi,value);}Near(b.minimum[row],lo);Near(b.maximum[row],hi);}}
 };
 verify();auto batched=adapter.main;Check(adapter.stats.instancedDrawCalls==1,"affine instancing used");render.SetInstancingEnabled(false);adapter.BeginFrame();render.Update(world,0);verify();for(std::size_t i=0;i<batched.size();++i)Check(batched[i].model==adapter.main[i].model&&batched[i].normal==adapter.main[i].normal,"instanced/unbatched affine consistency");
 Check(TrySetWorldScale(world,p,{-2,1,1}),"negative parent scale");adapter.BeginFrame();render.Update(world,0);verify();Check(std::all_of(adapter.states.begin(),adapter.states.end(),[](auto& s){return s.reverseWinding||s.doubleSided;}),"reflection culling policy");
}
static void BlockC(){
 World w(World::DataMode::HierarchyData);auto parent=Object(w,"parent"),box=Object(w,"box",{2,0,0});Check(SetParent(w,box,parent),"static collider child");auto& collider=w.AddComponent<ColliderComponent>(box);collider.autoFitFromMesh=false;collider.offset={.25f,0,0};w.AddComponent<RigidbodyComponent>(box).isStatic=true;
 Check(TrySetWorldScale(w,parent,{2,1,1}),"box nonuniform scale supported");auto hit=Raycast(w,{0,0,0},{1,0,0},20);Check(hit&&hit->entity==box,"child collider raycast");Near(hit->distance,3.5);
 Check(TrySetWorldRotation(w,parent,{0,0,1.57079632679f}),"rotate parent collider");hit=Raycast(w,{0,0,0},{0,1,0},20);Check(bool(hit),"rotated collider hit");Near(hit->distance,3.5);Check(TrySetWorldRotation(w,parent,{}),"restore rotation");Check(TrySetWorldScale(w,parent,{-2,1,1}),"reflect static box");hit=Raycast(w,{0,0,0},{-1,0,0},20);Check(bool(hit),"reflected collider hit");Near(hit->distance,3.5);Check(TrySetWorldScale(w,parent,{2,1,1}),"restore scale");
 Check(TrySetWorldPosition(w,parent,{5,0,0}),"move parent then query immediately");hit=Raycast(w,{0,0,0},{1,0,0},20);Near(hit->distance,8.5);Check(FindInRadius(w,{9,0,0},.1f)==std::vector<Entity>{box},"radius uses current child world position");
 w.GetComponent<TransformComponent>(box)->rotation.z=.78539816339f;Reject(w,[&]{(void)Raycast(w,{0,0,0},{1,0,0},20);});PhysicsSystem physics;Reject(w,[&]{physics.Update(w,.01f);});w.GetComponent<TransformComponent>(box)->rotation={};
 w.GetComponent<RigidbodyComponent>(box)->isStatic=false;Reject(w,[&]{physics.Update(w,.01f);});w.GetComponent<RigidbodyComponent>(box)->isStatic=true;
 collider.type=ColliderType::Sphere;Reject(w,[&]{(void)Raycast(w,{0,0,0},{1,0,0},20);});Check(TrySetWorldScale(w,parent,{2,2,2}),"sphere uniform scale");hit=Raycast(w,{0,0,0},{1,0,0},20);Check(hit&&hit->entity==box,"scaled child sphere query");Near(hit->distance,8.5);collider.type=ColliderType::Box;
 EventBus bus;int entered=0,exited=0;bus.SubscribeCollision([&](const CollisionEvent& event){entered+=event.kind==CollisionKind::TriggerEnter;exited+=event.kind==CollisionKind::TriggerExit;});PhysicsSystem triggers(&bus);
 collider.isTrigger=true;auto probe=Object(w,"probe",{9,0,0});w.AddComponent<ColliderComponent>(probe);triggers.Update(w,.01f);Check(entered==1&&exited==0,"trigger enter child geometry");Check(TrySetWorldPosition(w,parent,{15,0,0}),"trigger parent moves");triggers.Update(w,.01f);Check(entered==1&&exited==1,"trigger exit after parent motion");
 auto dynamic=Object(w,"dynamic",{0,5,0}),visual=Object(w,"visual",{1,5,0});Check(SetParent(w,visual,dynamic),"dynamic root visual child");w.AddComponent<ColliderComponent>(dynamic);w.AddComponent<RigidbodyComponent>(dynamic);auto old=ReadWorldPosition(w,visual)->y;physics.Update(w,.02f);Check(ReadWorldPosition(w,visual)->y<old,"root solver motion inherited by visual child");
 RenderInterpolation interpolation;interpolation.BeginTick(w);interpolation.EndTick(w);interpolation.SetFrame(w,true,.5);scripting::GameplayCamera camera;camera.following=true;camera.target=visual;camera.offset={0,1,0};scripting::ResolveGameplayCamera(camera,w,&interpolation);Near(camera.position.y,ReadWorldPosition(w,visual)->y+1);w.DestroyEntity(visual);scripting::ResolveGameplayCamera(camera,w);Check(!camera.following,"camera stale target fenced");
 RuntimeRecorder debugAdapter;RenderSystem debug;debug.SetRenderAdapter(&debugAdapter);debug.SetDebugCollidersEnabled(true);debug.Update(w,0);Check(debugAdapter.debugCubes==3,"physics debug draws current resolved collider geometry");
 World lightWorld(World::DataMode::HierarchyData);auto lp=Object(lightWorld,"p"),light=Object(lightWorld,"light",{2,0,0});Check(SetParent(lightWorld,light,lp),"light child");lightWorld.AddComponent<SpotLightComponent>(light);auto* transform=lightWorld.GetComponent<TransformComponent>(lp);transform->rotation.y=1.57079632679f;transform->scale={-2,3,1};auto gathered=GatherSceneLights(lightWorld,{},{});Check(gathered.lights.size()==1,"spot gathered");Near(gathered.lights[0].directionType[0],1);Near(gathered.lights[0].directionType[2],0);Near(gathered.lights[0].positionRange[2],4);
 lightWorld.RemoveComponent<SpotLightComponent>(light);lightWorld.AddComponent<DirectionalLightComponent>(light);gathered=GatherSceneLights(lightWorld,{},{});Near(gathered.lighting.directional.direction[0],1);lightWorld.RemoveComponent<DirectionalLightComponent>(light);lightWorld.AddComponent<PointLightComponent>(light);gathered=GatherSceneLights(lightWorld,{},{});Near(gathered.lights[0].positionRange[2],4);
 World tie(World::DataMode::HierarchyData);auto z=Object(tie,"zzz"),a=Object(tie,"aaa");tie.AddComponent<DirectionalLightComponent>(z).primary=true;tie.AddComponent<DirectionalLightComponent>(a).primary=true;tie.GetComponent<TransformComponent>(z)->rotation.y=1.57079632679f;gathered=GatherSceneLights(tie,{},{});Check(gathered.diagnostics.primaryIndex==a.index&&SceneSerializer::PrimaryDirectionalId(tie)=="aaa","runtime primary follows stable identity persistence policy");Near(gathered.lighting.directional.direction[2],1);
 World range(World::DataMode::HierarchyData);auto huge=Object(range,"huge",{1100000,0,0});range.AddComponent<ColliderComponent>(huge).offset={-1100000,0,0};Reject(range,[&]{(void)Raycast(range,{0,0,0},{1,0,0},10);});Reject(range,[&]{physics.Update(range,.01f);});
 range.RemoveComponent<ColliderComponent>(huge);Reject(range,[&]{(void)FindInRadius(range,{1000000,0,0},200000);});
 World visualQuery(World::DataMode::HierarchyData);auto vp=Object(visualQuery,"parent"),vc=Object(visualQuery,"child",{2,0,0});Check(SetParent(visualQuery,vc,vp)&&TrySetWorldScale(visualQuery,vp,{2,1,1}),"visual query fixture");visualQuery.GetComponent<TransformComponent>(vc)->rotation.z=.78539816339f;Check(FindInRadius(visualQuery,{4,0,0},.1f)==std::vector<Entity>{vc},"radius query retains shear world translation");
 World lightRange(World::DataMode::HierarchyData);auto largeParent=Object(lightRange,"large parent",{3e38f,0,0}),largeLight=Object(lightRange,"large light");lightRange.AddComponent<RelationshipComponent>(largeLight).parent="large parent";lightRange.GetComponent<TransformComponent>(largeLight)->position={3e38f,0,0};lightRange.AddComponent<PointLightComponent>(largeLight);Check(ValidateHierarchy(lightRange),"finite double light world position may exceed float");Reject(lightRange,[&]{(void)GatherSceneLights(lightRange,{},{});});
}
struct ScriptFixture {
 std::string key="scripts/runtime-hierarchy-"+NewSceneIdentity()+"/test.lua";std::filesystem::path path=scripting::SafeAsset(key,".lua");
 ScriptFixture(){Check(std::filesystem::create_directory(path.parent_path()),"owned runtime script fixture");}
 ~ScriptFixture(){std::error_code error;std::filesystem::remove_all(path.parent_path(),error);}
 void Write(const std::string& source){std::ofstream out(path);out<<source;Check(bool(out),"write runtime script fixture");}
};
static nlohmann::json Ref(const char* id){return {{"$entity",id}};}
static void BlockD(){
 ScriptFixture files;
 files.Write(R"(return {fields={child={type='entity'},root={type='entity'},observer={type='entity'}},
 on_start=function(self)
  World.set_position(self.root,Vec3.new(10,0,0));local p=World.get_position(self.child);assert(math.abs(p.x-12)<.001)
  World.set_position(self.child,Vec3.new(15,2,0));World.set_rotation(self.child,Vec3.new(0,0,.25));World.set_scale(self.child,Vec3.new(2,2,2))
  local r=World.get_rotation(self.child);local s=World.get_scale(self.child);assert(math.abs(r.z-.25)<.001 and math.abs(s.x-2)<.001)
  World.set_position(self.observer,World.get_position(self.child))
 end})");
 World author(World::DataMode::HierarchyData);auto p=Object(author,"parent"),c=Object(author,"child",{2,0,0}),observer=Object(author,"observer"),actor=Object(author,"actor");Check(SetParent(author,c,p),"Lua hierarchy fixture");auto& script=author.AddComponent<LuaScriptComponent>(actor);script.scriptPath=files.key;script.properties={{"root",Ref("parent")},{"child",Ref("child")},{"observer",Ref("observer")}};
 World::CloneMap map;auto runtime=author.CloneData(&map);scripting::LuaRuntime lua;lua.Start(*runtime);lua.Update(0);Check(lua.Error(files.key).empty(),"Lua immediate world reads/writes");Near(ReadWorldPosition(*runtime,map.at(observer))->x,15);Near(ReadWorldPosition(author,c)->x,2);lua.Stop();Check(lua.InstanceCount()==0,"Stop releases instances");
 // Checked shear errors are protected and do not change the authored local.
 files.Write(R"(return {fields={child={type='entity'}},on_start=function(self) local p=World.get_position(self.child);World.set_position(self.child,Vec3.new(p.x+1,p.y,p.z));World.get_rotation(self.child) end})");
 World shear(World::DataMode::HierarchyData);auto sp=Object(shear,"parent"),sc=Object(shear,"child");Check(SetParent(shear,sc,sp),"shear Lua fixture");shear.GetComponent<TransformComponent>(sc)->rotation.z=.78539816339f;Check(TrySetWorldScale(shear,sp,{2,1,1}),"shear parent");auto sa=Object(shear,"actor");auto& ss=shear.AddComponent<LuaScriptComponent>(sa);ss.scriptPath=files.key;ss.properties={{"child",Ref("child")}};lua.Start(shear);lua.Update(0);Check(!lua.Error(files.key).empty(),"shear TRS read raises protected error");Near(ReadWorldPosition(shear,sc)->x,1);lua.Stop();
 files.Write(R"(return {fields={child={type='entity'}},on_start=function(self) World.set_scale(self.child,Vec3.new(2,2,2)) end})");auto before=*shear.GetComponent<TransformComponent>(sc);lua.Start(shear);lua.Update(0);Check(!lua.Error(files.key).empty()&&ComposeTRS(before)==ComposeTRS(*shear.GetComponent<TransformComponent>(sc)),"failed Lua shear setter is atomic");lua.Stop();
 files.Write(R"(return {fields={observer={type='entity'},root={type='entity'},child={type='entity'},extra={type='entity'},code={type='number',default=1}},
 on_start=function(self) Timer.every(.1,'tick');if self.code==2 then assert(Camera.acquire());assert(Input.capture_cursor(true));Camera.follow(self.entity,Vec3.new(0,1,0)) end end,
 on_fixed_update=function(self) if self.code==1 and World.get_position(self.entity).x>10 then World.destroy(self.root);World.destroy(self.child) end end,
 on_event=function(self,event) local p=World.get_position(self.observer);World.set_position(self.observer,Vec3.new(p.x,p.y,p.z+1)) end,
 on_stop=function(self) local p=World.get_position(self.observer);World.set_position(self.observer,Vec3.new(p.x,p.y+1,p.z)) end,
 on_destroy=function(self) local p=World.get_position(self.observer);World.set_position(self.observer,Vec3.new(p.x*10+self.code,p.y,p.z));if self.code==2 then World.destroy(self.extra) end end})");
 World life(World::DataMode::HierarchyData);auto lp=Object(life,"parent"),lc=Object(life,"child"),lg=Object(life,"grandchild"),extra=Object(life,"extra"),log=Object(life,"observer");Check(SetParent(life,lc,lp)&&SetParent(life,lg,lc),"lifecycle closure");int code=0;
 for(auto e:{lp,lc,lg,extra}){auto& s=life.AddComponent<LuaScriptComponent>(e);s.scriptPath=files.key;s.cameraEnabled=s.inputEnabled=true;s.properties={{"root",Ref("parent")},{"child",Ref("child")},{"extra",Ref("extra")},{"observer",Ref("observer")},{"code",++code}};}
 scripting::GameplayCamera camera;bool captured=false;scripting::GameplayServices services;services.camera=&camera;services.captureCursor=[&](bool value){captured=value;return true;};services.inputAllowed=[](){return true;};
 lua.Start(life,{},services);lua.Update(0);Check(lua.Error(files.key).empty()&&lua.InstanceCount()==4&&captured&&lua.HasCameraOwner(),"hierarchy instances/timers/control start");
 auto replacement=life.CloneData();Reject(life,[&]{life.ReplaceData(*replacement);});Check(lua.InstanceCount()==4,"ReplaceData requires Stop before publication");
 lua.QueueCollision({lp,lc,{}});Check(TrySetWorldPosition(life,lp,{11,0,0}),"request destruction during fixed callback");lua.FixedUpdate(.01f);
 Check(!life.IsAlive(lp)&&!life.IsAlive(lc)&&!life.IsAlive(lg)&&!life.IsAlive(extra)&&life.IsAlive(log)&&lua.InstanceCount()==0,"subtree + nested request removed exactly once");Near(ReadWorldPosition(life,log)->x,3214);Near(ReadWorldPosition(life,log)->y,4);Near(ReadWorldPosition(life,log)->z,0);Check(!captured&&!lua.HasCameraOwner()&&!camera.following,"cursor/camera ownership released");
 lua.Update(.5f);Near(ReadWorldPosition(life,log)->x,3214);Near(ReadWorldPosition(life,log)->z,0);Check(lua.Error(files.key).empty(),"callbacks/timers/events clean after closure retirement");lua.Stop();life.ReplaceData(*replacement);Check(!life.IsAlive(log),"Stop permits replacement and fences previous handles");
 // Native/deferred World deletion must pass through the same Lua closure hook.
 lua.Start(life,{},services);lua.Update(0);Entity parent;for(auto e:life.Entities())if(life.GetComponent<SceneIdentityComponent>(e)->value=="parent")parent=e;MutationQueue queue;queue.Destroy(parent);queue.Destroy(parent);queue.Apply(life);lua.Update(0);Entity log2;for(auto e:life.Entities())if(life.GetComponent<SceneIdentityComponent>(e)->value=="observer")log2=e;Near(ReadWorldPosition(life,log2)->x,3214);Near(ReadWorldPosition(life,log2)->y,4);Check(lua.InstanceCount()==0&&lua.Error(files.key).empty(),"native MutationQueue lifecycle exactly once");lua.Stop();
 World markers(World::DataMode::HierarchyData);auto marker=Object(markers,"marker"),gameChild=Object(markers,"game child");markers.AddComponent<PlayerStartComponent>(marker).controllerScript.clear();Check(SetParent(markers,gameChild,marker),"marker child fixture");Reject(markers,[&]{(void)SpawnPlayer(markers);});Check(markers.IsAlive(gameChild),"marker child protected before mutation");Check(ClearParent(markers,gameChild),"explicit marker fixture detach");auto player=SpawnPlayer(markers);Check(markers.IsAlive(player)&&!markers.IsAlive(marker)&&markers.IsAlive(gameChild)&&markers.HasComponent<SceneIdentityComponent>(player),"root PlayerStart spawn and safe cleanup");markers.RequireRuntimeWorld();
 // Existing Lua prefab entry creates a validated root in HierarchyData.
 const auto prefabPath=files.path.parent_path()/"spawn.prefab.json";const auto prefabKey=files.key.substr(0,files.key.find_last_of('/')+1)+"spawn.prefab.json";
 {std::ofstream out(prefabPath);out<<nlohmann::json{{"version",1},{"components",{{"Transform",nlohmann::json::object()},{"Tag",{{"name","spawned"}}}}}};}
 files.Write("return {on_start=function(self) local e=World.spawn_prefab('"+prefabKey+"',Vec3.new(4,5,6));assert(World.get_position(e).x==4) end}");World spawning(World::DataMode::HierarchyData);auto creator=Object(spawning,"creator");spawning.AddComponent<LuaScriptComponent>(creator).scriptPath=files.key;lua.Start(spawning);lua.Update(0);Check(lua.Error(files.key).empty()&&spawning.GetAliveCount()==2,"Lua runtime root spawn uses staged runtime validation");lua.Stop();
 {std::ofstream out(prefabPath);out<<nlohmann::json{{"version",1},{"components",{{"Transform",{{"scale",{2,2,2}}}},{"Collider",nlohmann::json::object()},{"Rigidbody",nlohmann::json::object()}}}};}
 Reject(spawning,[&]{(void)scripting::SpawnPrefab(spawning,prefabKey,{0,0,0});});
 files.Write(R"(return {fields={child={type='entity'}},on_start=function(self) World.set_scale(self.child,Vec3.new(2,2,2)) end})");auto dynamic=Object(spawning,"dynamic");spawning.AddComponent<RigidbodyComponent>(dynamic);spawning.AddComponent<ColliderComponent>(dynamic);spawning.GetComponent<LuaScriptComponent>(creator)->properties={{"child",Ref("dynamic")}};lua.Start(spawning);lua.Update(0);Check(!lua.Error(files.key).empty()&&spawning.GetComponent<TransformComponent>(dynamic)->scale.x==1,"unsupported Lua physics scale rolls back");lua.Stop();
}
// Closed integration harness, deliberately separate from public Editor/Project
// opening. Uses the real World pipeline, Physics, Lua and interpolation boundary.
struct ClosedRuntimeHarness {
 struct State {
  std::unique_ptr<World> world;World::CloneMap map;EventBus bus;PhysicsSystem* physics=nullptr;scripting::LuaRuntime lua;RenderInterpolation interpolation;bool paused=false;
  explicit State(const World& author){world=author.CloneData(&map);world->RequireRuntimeWorld();(void)SpawnPlayer(*world);physics=&world->AddSystem<PhysicsSystem>(&bus);world->AddSystem<MotionSystem>();bus.SubscribeCollision([this](const auto& e){lua.QueueCollision(e);});lua.Start(*world);}
  ~State(){lua.Stop();if(physics)physics->ResetState();}
  void Tick(float dt){if(paused){interpolation.SetFrame(*world,false,0);return;}lua.Update(dt);if(dt>0){interpolation.BeginTick(*world);lua.FixedUpdate(dt);world->UpdateFixedSystems(dt);interpolation.EndTick(*world);}world->UpdateFrameSystems(dt);interpolation.SetFrame(*world,true,.5);}
 };
 std::unique_ptr<State> state;
 void Start(const World& author){auto staged=std::make_unique<State>(author);state.swap(staged);}
 void Pause(bool value){state->paused=value;if(value)state->lua.SuspendInput();}
 void Stop(){state.reset();}
};
static void RuntimeLifecycle(){
 ScriptFixture files;files.Write("return {on_update=function(self,dt) end}");World author(World::DataMode::HierarchyData);auto p=Object(author,"body",{0,5,0}),c=Object(author,"visual",{1,5,0}),marker=Object(author,"marker",{10,2,0});Check(SetParent(author,c,p),"session visual child");author.AddComponent<ColliderComponent>(p);author.AddComponent<RigidbodyComponent>(p);author.AddComponent<PlayerStartComponent>(marker).controllerScript.clear();author.AddComponent<LuaScriptComponent>(c).scriptPath=files.key;auto original=SceneSerializer::CaptureWorld(author);
 ClosedRuntimeHarness session;std::uint64_t lastIdentity=0;
 for(int cycle=0;cycle<3;++cycle){session.Start(author);auto& state=*session.state;Check(state.world->IsHierarchyData()&&state.world->GetIdentity()!=author.GetIdentity()&&state.world->GetIdentity()!=lastIdentity,"runtime clone identity/mode");lastIdentity=state.world->GetIdentity();auto body=state.map.at(p),child=state.map.at(c);Check(!state.world->IsAlive(p)&&GetParent(*state.world,child)==body&&!state.world->IsAlive(state.map.at(marker)),"runtime relationships and marker cleanup");state.Tick(.02f);Check(state.lua.InstanceCount()==1&&state.lua.Error(files.key).empty(),"real pipeline runs Lua instance");auto position=*ReadWorldPosition(*state.world,child);session.Pause(true);state.Tick(.1f);Near(ReadWorldPosition(*state.world,child)->y,position.y);session.Pause(false);state.Tick(.02f);Check(ReadWorldPosition(*state.world,child)->y<position.y,"Resume advances root physics and child world");Check(state.interpolation.Count()==0,"hierarchy render interpolation remains disabled");
  World::CloneMap invalidMap;auto invalid=author.CloneData(&invalidMap);invalid->AddComponent<RigidbodyComponent>(invalidMap.at(c));auto* previous=session.state.get();bool rejected=false;try{session.Start(*invalid);}catch(const std::exception&){rejected=true;}Check(rejected&&session.state.get()==previous&&state.world->IsAlive(body),"failed runtime preparation preserves existing session");
  state.world->Clear();state.Tick(.01f);Check(state.lua.InstanceCount()==0&&state.world->GetAliveCount()==0&&state.physics->GetStatistics().bodies==0,"native Clear retires Lua before storage and clears next physics step");
  session.Stop();Check(!session.state&&SceneSerializer::CaptureWorld(author)==original,"Stop preserves exact author scene/state");}
 World guarded(World::DataMode::HierarchyData);auto parent=Object(guarded,"parent"),child=Object(guarded,"child"),outside=Object(guarded,"outside");Check(SetParent(guarded,child,parent),"retirement guard fixture");int owner=0;bool rejected=false;guarded.AttachHierarchyLifecycle(&owner,[&](const auto& closure){Check(closure==std::vector<Entity>{child,parent}&&guarded.HasComponent<TransformComponent>(child),"children-first callback sees live components");rejected=!SetParent(guarded,outside,child);});guarded.DestroyEntity(parent);guarded.DetachHierarchyLifecycle(&owner);Check(rejected&&guarded.IsAlive(outside)&&!GetParent(guarded,outside).IsValid(),"retirement cannot adopt unplanned descendants");
}
#if defined(ENABLE_DX12)
static void Dx12Hierarchy(){
 GlfwWindow window;Check(window.Create(320,240,"WhispEngine runtime hierarchy regression"),"hierarchy DX12 window");Dx12RenderAdapter adapter;Check(adapter.Initialize(&window),"hierarchy DX12 initialization");glfwHideWindow(window.GetGlfwHandle());
 ResourceManager resources(nullptr);resources.Load<MeshResource>("models/validation_cube.obj");resources.Load<ShaderResource>("dx12/textured.hlsl");resources.Load<TextureResource>("defaults/texture");resources.Load<MaterialResource>("materials/stress.material.json");
 RenderSystem render;render.SetResourceManager(&resources);render.SetRenderAdapter(&adapter);render.SetCameraTransform({0,0,-8},0,0);render.SetCameraProjection(1.04719755f,4.f/3,.1f,40);ShadowSettings shadows;shadows.resolution=512;shadows.focusDistance=8;shadows.coverage=20;render.SetShadowSettings(&shadows);
 World world(World::DataMode::HierarchyData);auto p=Object(world,"parent"),a=Object(world,"left",{-1,0,0}),b=Object(world,"right",{1,0,0});Check(SetParent(world,a,p)&&SetParent(world,b,p),"GPU hierarchy fixture");Mesh(world,a);Mesh(world,b);world.GetComponent<MaterialComponent>(a)->tint[0]=.1f;world.GetComponent<MaterialComponent>(b)->tint[2]=.1f;world.GetComponent<TransformComponent>(a)->rotation.z=.6f;world.GetComponent<TransformComponent>(b)->rotation.z=-.4f;Check(TrySetWorldScale(world,p,{1.7f,1,1}),"GPU shear fixture");
 auto frame=[&](bool instanced,bool capture=true){render.SetInstancingEnabled(instanced);window.PollEvents();adapter.BeginFrame();adapter.Clear(.03f,.04f,.05f,1);render.Update(world,0);adapter.EndFrame();auto pixels=capture?adapter.ReadBackFramePixels():std::vector<std::uint8_t>{};adapter.Present();return pixels;};
 for(int warm=0;warm<12;++warm)(void)frame(true,false);
 auto ordinary=frame(false),instanced=frame(true);Check(!ordinary.empty()&&ordinary==instanced,"GPU affine ordinary/instanced pixels identical");Check(adapter.GetSubmissionStatistics().renderedInstances==2&&adapter.GetSubmissionStatistics().instancedDrawCalls==1,"GPU actual affine instanced draw");
 auto depth=adapter.ReadBackShadowDepth();Check(!depth.empty()&&std::any_of(depth.begin(),depth.end(),[](float d){return d<.999f;}),"GPU affine shadow casters produce depth");unsigned changed=0;for(std::size_t i=4;i<ordinary.size();i+=4)changed+=ordinary[i]!=ordinary[0]||ordinary[i+1]!=ordinary[1]||ordinary[i+2]!=ordinary[2];Check(changed>100,"GPU affine geometry is visible");
 Check(TrySetWorldScale(world,p,{-1.7f,1,1}),"GPU reflection");auto reflected=frame(true);Check(reflected!=ordinary,"GPU reflected geometry changes image");Check(frame(false)==reflected,"GPU reflected ordinary/instanced pixels identical");
 Check(TrySetWorldPosition(world,p,{.5f,.2f,0}),"GPU parent translation");Check(frame(true)!=reflected,"GPU child geometry follows parent");world.DestroyEntity(p);(void)frame(true);Check(adapter.GetSubmissionStatistics().renderedInstances==0,"GPU subtree deletion submits no stale objects");Check(adapter.GetValidationErrorCount()==0,"DX12 hierarchy ERROR/CORRUPTION count zero");render.ReleaseGpuResources();render.SetRenderAdapter(nullptr);adapter.Shutdown();
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{std::string mode=argc>1?argv[1]:"all";if(mode=="all"||mode=="--a")BlockA();if(mode=="all"||mode=="--b")BlockB();if(mode=="all"||mode=="--c")BlockC();if(mode=="all"||mode=="--d")BlockD();if(mode=="all"||mode=="--runtime")RuntimeLifecycle();
#if defined(ENABLE_DX12)
 if(mode=="--dx12")Dx12Hierarchy();
#endif
 Check(checks>0,"known regression mode");std::cout<<"Runtime hierarchy "<<mode<<": "<<checks<<" checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
