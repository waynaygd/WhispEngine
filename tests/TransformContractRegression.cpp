#define NOMINMAX
#include "EditorTestPreferences.h"
#include "ecs/TransformAccess.h"
#include "ecs/RenderInterpolation.h"
#include "ecs/ComponentRegistry.h"
#include "ecs/PhysicsQueries.h"
#include "ecs/systems/PhysicsSystem.h"
#include "ecs/systems/MotionSystem.h"
#include "ecs/systems/BoundsBounceSystem.h"
#include "ecs/components/VelocityComponent.h"
#include "ecs/components/BoundsBounceComponent.h"
#include "ecs/components/LuaScriptComponent.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "scripting/LuaRuntime.h"
#include "scripting/Prefab.h"
#include "scene/SceneSerializer.h"
#include "core/Application.h"
#include <fstream>
#include <iostream>
#include <limits>
#include <source_location>
#include <thread>
using namespace ecs;
static int checks=0;
static void Check(bool ok,const char* message,std::source_location line=std::source_location::current()){
    if(!ok)throw std::runtime_error(std::string(message)+" at "+std::to_string(line.line()));++checks;
}
static void Near(double a,double b,double tolerance=2e-5){Check(std::abs(a-b)<=tolerance,"numeric oracle");}
static void MatrixNear(const Matrix4& a,const Matrix4& b){for(int k=0;k<16;++k)Near(a[k],b[k]);}
static Entity Object(World& w){auto e=w.CreateEntity();w.AddComponent<TransformComponent>(e);w.AddComponent<SceneIdentityComponent>(e);return e;}
static void Math(){
    TransformComponent t;t.position={3,-4,5};t.rotation.z=1.57079632679f;t.scale={-2,3,4};
    auto m=TryComposeTRS(t);Check(bool(m),"compose valid TRS");Near((*m)[12],3);Near((*m)[13],-4);Near((*m)[14],5);Near((*m)[0],0);Near((*m)[1],-2);Near((*m)[4],-3);Near((*m)[10],4);
    for(Vec3 rotation:{Vec3{},Vec3{.2f,.4f,-.7f},Vec3{.3f,1.57079632679f,.8f}})
    for(Vec3 scale:{Vec3{1,1,1},Vec3{-2,3,4},Vec3{-2,-3,4},Vec3{-2,-3,-4}}){
        t.rotation=rotation;t.scale=scale;m=TryComposeTRS(t);auto value=DecomposeTRS(*m,scale);Check(bool(value),"representable signed TRS round trip");MatrixNear(ComposeTRS(*value),*m);
        auto inverse=InverseAffine(*m);Check(bool(inverse),"checked inverse");MatrixNear(MultiplyTransformMatrices(*m,*inverse),IdentityTransformMatrix());MatrixNear(MultiplyTransformMatrices(*inverse,*m),IdentityTransformMatrix());
    }
    auto shear=IdentityTransformMatrix();shear[4]=.5;Check(!DecomposeTRS(shear),"shear is not TRS");auto inverse=InverseAffine(shear);Check(bool(inverse),"invertible affine shear accepted by inverse");Near((*inverse)[4],-.5);MatrixNear(MultiplyTransformMatrices(shear,*inverse),IdentityTransformMatrix());
    for(double scale:{0.,1e-9}){auto singular=IdentityTransformMatrix();singular[0]=scale;Check(!InverseAffine(singular)&&!DecomposeTRS(singular),"singular/near singular rejected");}
    for(double bad:{std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})for(int k:{0,12,15}){auto invalid=IdentityTransformMatrix();invalid[k]=bad;Check(!Finite(invalid)&&!InverseAffine(invalid)&&!DecomposeTRS(invalid),"nonfinite matrix rejected");}
    auto perspective=IdentityTransformMatrix();perspective[3]=.2;Check(!InverseAffine(perspective)&&!DecomposeTRS(perspective),"nonaffine rejected");
    t.position.x=std::numeric_limits<float>::infinity();Check(!TryComposeTRS(t),"invalid TRS compose rejected");
}
static void Access(){
    World w,foreign;auto e=Object(w);TransformComponent value;value.position={4,5,6};value.rotation={.2f,.4f,.1f};value.scale={-2,3,4};std::string error;
    Check(TrySetWorldTransform(w,e,value,&error),"world value write");MatrixNear(*ReadWorldMatrix(w,e),ComposeTRS(value));Near(ReadWorldPosition(w,e)->x,4);Near(ReadWorldRotation(w,e)->y,.4f);Near(ReadWorldScale(w,e)->x,-2);
    const auto before=ComponentRegistry::Builtins().Capture(w,e);
    auto bad=value;bad.position.y=std::numeric_limits<float>::quiet_NaN();Check(!TrySetWorldTransform(w,e,bad,&error)&&!error.empty(),"full write failure");
    Check(!TrySetWorldRotation(w,e,{0,std::numeric_limits<float>::infinity(),0}),"field write failure");auto shear=IdentityTransformMatrix();shear[4]=.3;Check(!TrySetWorldMatrix(w,e,shear),"matrix write rejects shear");
    Check(ComponentRegistry::Builtins().Capture(w,e)==before,"checked failure has no partial mutation");
    auto none=w.CreateEntity();Check(!ReadWorldTransform(w,none)&&!TrySetWorldPosition(w,none,{}),"missing component not created");auto other=Object(foreign);Check(!ReadWorldMatrix(w,other)&&!TrySetWorldTransform(w,other,value),"foreign World rejected");
    Check(TrySetWorldPosition(w,e,{8,9,10})&&TrySetWorldRotation(w,e,{.5f,.6f,.7f})&&TrySetWorldScale(w,e,{-1,0,2}),"individual writes retain legacy zero scale");Check(ReadWorldScale(w,e)->y==0&&!InverseAffine(*ReadWorldMatrix(w,e)),"finite singular TRS distinguished from invertibility");
    bool ownerRejected=false;std::thread thread([&]{try{TrySetWorldPosition(w,e,{});}catch(const std::logic_error&){ownerRejected=true;}});thread.join();Check(ownerRejected,"writes require owner thread");
    Check(TrySetWorldTransform(w,e,value),"restore value");Check(TrySetWorldMatrix(w,e,ComposeTRS(value)),"matrix write valid signed TRS");MatrixNear(*ReadWorldMatrix(w,e),ComposeTRS(value));
    w.DestroyEntity(e);auto recycled=Object(w);Check(recycled.index==e.index&&!ReadWorldTransform(w,e)&&!TrySetWorldPosition(w,e,{}),"stale generation rejected");
}
struct Fixture{
    std::string key="scripts/transform-contract-"+NewSceneIdentity()+"/test.lua";
    std::filesystem::path file=scripting::SafeAsset(key,".lua");
    Fixture(){Check(std::filesystem::create_directory(file.parent_path()),"owned fixture");}
    ~Fixture(){std::error_code error;std::filesystem::remove_all(file.parent_path(),error);}
    void Write(const char* source){std::ofstream out(file);out<<source;Check(bool(out),"write generated fixture");}
};
static void LuaAndScene(){
    Fixture fixture;fixture.Write(R"(return {
      on_start=function(self)
        World.set_position(self.entity,Vec3.new(7,8,9));World.set_rotation(self.entity,Vec3.new(.2,.3,.4));World.set_scale(self.entity,Vec3.new(-2,3,4));
        assert(World.get_position(self.entity).x==7);assert(World.get_scale(self.entity).x==-2)
      end,
      on_fixed_update=function(self) local p=World.get_position(self.entity);p.x=p.x+1;World.set_position(self.entity,p) end})");
    World w;auto e=Object(w);w.AddComponent<LuaScriptComponent>(e).scriptPath=fixture.key;scripting::LuaRuntime lua;lua.Start(w);lua.Update(0);Check(lua.Error(fixture.key).empty(),"Lua legacy get/set signatures");Near(ReadWorldPosition(w,e)->x,7);Near(ReadWorldRotation(w,e)->y,.3f);Near(ReadWorldScale(w,e)->z,4);
    for(int i=0;i<3;++i)lua.FixedUpdate(1.f/60);Near(ReadWorldPosition(w,e)->x,10);lua.Stop();
    auto snapshot=SceneSerializer::CaptureWorld(w);auto path=fixture.file.parent_path()/"scene.json";std::string error;Check(SceneSerializer::SaveWorld(path,w,&error),"schema v4 disk Save");World loaded;SceneSettings settings;Check(SceneSerializer::LoadWorld(path,loaded,settings,&error),"schema v4 disk Load");Check(SceneSerializer::CaptureWorld(loaded,settings)==snapshot,"serialization unchanged numeric TRS and stable IDs");
    fixture.Write("return {on_start=function(self) World.set_position(self.entity,Vec3.new(0/0,1,2)) end}");lua.Start(w);lua.Update(0);Check(!lua.Error(fixture.key).empty(),"Lua checked failure protected");Near(ReadWorldPosition(w,e)->x,10);lua.Stop();
}
static void PhysicsAndRender(){
    World w;auto e=Object(w);w.AddComponent<RigidbodyComponent>(e).useGravity=false;w.GetComponent<RigidbodyComponent>(e)->velocity={2,0,0};w.AddComponent<ColliderComponent>(e).autoFitFromMesh=false;
    Check(TrySetWorldPosition(w,e,{3,4,5}),"physics initial world write");PhysicsSystem physics(nullptr,0,1,1);RenderInterpolation interpolation;interpolation.BeginTick(w);physics.Update(w,.05f);interpolation.EndTick(w);
    Near(ReadWorldPosition(w,e)->x,3.1,1e-4);auto hit=Raycast(w,{3.1f,4,0},{0,0,1},20);Check(hit&&hit->entity==e,"raycast reads same physical world position");
    auto physical=*ReadWorldTransform(w,e);interpolation.SetFrame(w,true,.5);auto pose=interpolation.Resolve(e,physical);Near(pose.position.x,3.05,1e-4);Near(ReadWorldPosition(w,e)->x,3.1,1e-4);
    float model[16];BuildRenderModelMatrix(model,pose);Near(model[12],3.05,1e-4);interpolation.SetFrame(w,false,0);BuildRenderModelMatrix(model,interpolation.Resolve(e,*ReadWorldTransform(w,e)));Near(model[12],3.1,1e-4);
    auto motion=Object(w);w.AddComponent<VelocityComponent>(motion).linear={4,0,0};MotionSystem mover;mover.Update(w,.5f);Near(ReadWorldPosition(w,motion)->x,2);
    auto& bounds=w.AddComponent<BoundsBounceComponent>(motion);bounds.minX=-1;bounds.maxX=1;bounds.minY=-1;bounds.maxY=1;BoundsBounceSystem bounce;bounce.Update(w,0);Near(ReadWorldPosition(w,motion)->x,1);Near(w.GetComponent<VelocityComponent>(motion)->linear.x,-4);
}
static void EditorIsolation(){
    Application app;app.NewScene();auto& ui=app.GetEditorLayer();auto& w=app.GetWorld();auto a=ui.CreateEntity(app,"A"),b=ui.CreateEntity(app,"B");TrySetWorldPosition(w,a,{-2,0,0});TrySetWorldPosition(w,b,{2,0,0});
    ui.ResetHistory();ui.SelectViewportEntity(app,a,false,false);ui.SelectViewportEntity(app,b,true,false);Check(ui.BeginGroupTransform(app,false),"existing group start");TransformComponent target;target.position={5,0,0};Check(ui.UpdateGroupTransform(app,ComposeTRS(target)),"group uses world contract");ui.EndGroupTransform(app);Near(ReadWorldPosition(w,a)->x,3);Near(ReadWorldPosition(w,b)->x,7);
    ui.UndoEdit(app);Near(ReadWorldPosition(w,a)->x,-2);ui.RedoEdit(app);Near(ReadWorldPosition(w,a)->x,3);
    auto before=SceneSerializer::CaptureWorld(w,app.GetSceneSettings());app.SetEditorPlayMode(true);Check(app.IsEditorPlayMode(),"Play runtime clone");auto selected=ui.GetSelectedEntity();Check(TrySetWorldPosition(app.GetWorld(),selected,{99,0,0}),"runtime world write");Near(ReadWorldPosition(w,b)->x,7);app.SetEditorPlayMode(false);Check(SceneSerializer::CaptureWorld(w,app.GetSceneSettings())==before,"Stop editor isolation");
}
int main(){ScopedEditorPreferenceRestore preferences;try{Math();Access();LuaAndScene();PhysicsAndRender();EditorIsolation();std::cout<<"Transform contract: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
