#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "ecs/CameraAttachments.h"
#include "ecs/ComponentRegistry.h"
#include "ecs/components/LuaScriptComponent.h"
#include "ecs/components/PrefabInstanceComponent.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "ecs/components/TagComponent.h"
#include "resources/ResourceManager.h"
#include "scripting/Prefab.h"
#include "scripting/EntityReference.h"
#include "scene/SceneSerializer.h"
#include <fstream>
#include <iostream>
#include <limits>
#if defined(ENABLE_DX12)
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#endif
using namespace ecs;
using Matrix=std::array<float,16>;
static int checks=0;
static void Check(bool v,const std::string& s){if(!v)throw std::runtime_error(s);++checks;}
static Matrix Model(RenderPose p){Matrix m;BuildRenderModelMatrix(m.data(),p);return m;}
static Matrix Product(const Matrix& a,const Matrix& b){return ShadowMultiply(a,b);}
static bool Equal(const Matrix& a,const Matrix& b,float tolerance=2e-4f){for(int i=0;i<16;++i)if(std::abs(a[i]-b[i])>tolerance)return false;return true;}
struct Files {
    std::string key="attachments-tests-"+NewSceneIdentity()+"/";
    std::filesystem::path folder=AssetPaths::ResolveAssetRoot()/std::filesystem::u8path(key);
    Files(){Check(std::filesystem::create_directory(folder),"owned fixture folder");}
    ~Files(){std::error_code error;std::filesystem::remove_all(folder,error);}
    std::string Write(const std::string& name,const std::string& text){std::ofstream f(folder/name);f<<text;Check(bool(f),"write fixture");return key+name;}
};
static Entity Node(World& w,const char* name,Vec3 position={}){auto e=w.CreateEntity();w.AddComponent<TransformComponent>(e).position=position;w.AddComponent<TagComponent>(e).name=name;return e;}
static void Mesh(World& w,Entity e){auto& m=w.AddComponent<MeshRendererComponent>(e);m.meshPath="models/primitives/cube.obj";w.AddComponent<MaterialComponent>(e).materialPath="materials/default_primitive.material.json";}
static nlohmann::json Ref(World& w,Entity e){if(!w.HasComponent<SceneIdentityComponent>(e))w.AddComponent<SceneIdentityComponent>(e);return nlohmann::json{{"$entity",w.GetComponent<SceneIdentityComponent>(e)->value}};}
static Entity Actor(World& w,const std::string& path){auto e=Node(w,"Controller");auto& s=w.AddComponent<LuaScriptComponent>(e);s.scriptPath=path;s.cameraEnabled=true;return e;}
static Entity PrefabRoot(World& w){for(auto e:w.Entities())if(w.HasComponent<PrefabInstanceComponent>(e))return e;return {};}
static std::string Prefab(Files& files){
    World source;auto root=Node(source,"Attached model",{100,200,300});source.GetComponent<TransformComponent>(root)->scale={.4f,.4f,.4f};Mesh(source,root);
    source.AddComponent<CameraAttachmentComponent>(root)={true,{.17f,-.13f,2.7f},{.1f,.2f,.15f}};
    auto child=Node(source,"Visual child",{1,0,0});Mesh(source,child);Check(source.SetParent(child,root,false),"prefab child parent");
    const auto key=files.key+"model.prefab.json";std::string error;Check(SceneSerializer::WriteAtomic(files.folder/"model.prefab.json",scripting::CapturePrefab(source,root),&error,false),error);return key;
}
struct Recorder:IRenderAdapter {
    std::vector<RenderInstanceData> draws,shadows;RenderInstanceData pending;unsigned batches=0;bool failBatch=false;std::uint64_t id=1;
    bool Initialize(IWindow*)override{return true;}void BeginFrame()override{draws.clear();shadows.clear();batches=0;}
    void Clear(float,float,float,float)override{}void SetTestTransform(const float* p)override{std::copy(p,p+16,pending.mvp.begin());}
    void SetNormalMatrix(const std::array<float,12>& n)override{pending.normal=n;}
    void SetModelMatrix(const Matrix& m)override{pending.model=m;}
    void SetTestColor(float,float,float,float)override{}
    RenderMeshHandle UploadMesh(const MeshData&)override{return {id++};}
    RenderTextureHandle CreateTexture2D(const TextureData&)override{return {id++};}
    RenderShaderHandle CreateShaderProgram(const ShaderResource&)override{return {id++};}
    void BindShader(RenderShaderHandle)override{}void BindTexture(std::uint32_t,RenderTextureHandle)override{}
    bool SupportsInstancing(RenderShaderHandle)const override{return true;}
    void DrawMesh(RenderMeshHandle)override{draws.push_back(pending);}
    bool DrawMeshInstanced(RenderMeshHandle,std::span<const RenderInstanceData> rows)override{if(failBatch)return false;++batches;draws.insert(draws.end(),rows.begin(),rows.end());return true;}
    bool BeginShadowPass(const ShadowFrame& f)override{return f.constants.parameters[0]>.5f;}
    void DrawShadowMesh(RenderMeshHandle,const RenderInstanceData& row)override{shadows.push_back(row);}
    bool DrawShadowMeshInstanced(RenderMeshHandle,std::span<const RenderInstanceData> rows)override{shadows.insert(shadows.end(),rows.begin(),rows.end());return true;}
    void DrawTestTriangle()override{}void DrawTestLine()override{}void DrawTestQuad()override{}void DrawTestCube()override{}
    void EndFrame()override{}void Present()override{}void Shutdown()override{}
};
static void Registry(Files& files,const std::string& prefab){
    World w;auto e=Node(w,"Attachment");auto& r=ComponentRegistry::Builtins();auto id=ComponentId("CameraAttachment");std::string error;
    Check(r.Add(w,e,id,&error),error);Check(r.Set(w,e,id,"localPosition",Vec3{.2f,-.3f,1},&error),error);Check(r.Set(w,e,id,"localRotation",Vec3{.1f,.2f,.3f},&error),error);
    Check(!r.Set(w,e,id,"localPosition",Vec3{std::numeric_limits<float>::infinity(),0,0},&error),"reject nonfinite attachment");
    Check(!r.Remove(w,e,ComponentId("Transform"),&error),"attachment requires Transform");
    auto before=SceneSerializer::CaptureWorld(w);Check(SceneSerializer::SaveWorld(files.folder/"scene.json",w,&error),error);World loaded;SceneSettings settings;
    Check(SceneSerializer::LoadWorld(files.folder/"scene.json",loaded,settings,&error),error);Check(SceneSerializer::CaptureWorld(loaded)==before,"scene roundtrip attachment fields");
    World::CloneMap map;auto clone=w.CloneData(&map);Check(r.Capture(w,e)==r.Capture(*clone,map.at(e)),"runtime clone attachment");
    auto root=scripting::SpawnPrefab(w,prefab,{40,50,60});Check(w.HasComponent<CameraAttachmentComponent>(root)&&w.Subtree(root).size()==2,"prefab root attachment and child disk roundtrip");
    auto camera=BuildCameraState({{1,2,3},{1,1,1},RenderRotation({.2f,.3f,.4f})},{},1.5f);auto pose=CameraAttachmentPose(w,root,camera);Check(pose.has_value(),"prefab authored attachment works without Lua attach");
    w.GetComponent<CameraAttachmentComponent>(root)->enabled=false;Check(!CameraAttachmentPose(w,root,camera),"disabled root restores authored pose");
    w.DestroyEntity(root);Check(!CameraAttachmentPose(w,root,camera),"destroyed handle ignored");
    auto parent=Node(w,"Scaled parent");w.GetComponent<TransformComponent>(parent)->scale={3,3,3};
    auto scaled=Node(w,"Attached scaled root");w.GetComponent<TransformComponent>(scaled)->scale={.5f,.5f,.5f};w.AddComponent<CameraAttachmentComponent>(scaled)={true,{0,0,2},{}};
    auto child=Node(w,"Scaled child",{1,0,0});Check(w.SetParent(scaled,parent,false)&&w.SetParent(child,scaled,false),"scaled attachment hierarchy");
    auto origin=BuildCameraState({}, {},1);auto scalePose=CameraAttachmentPose(w,scaled,origin),childPose=CameraAttachmentPose(w,child,origin);
    Check(scalePose&&std::abs(scalePose->scale.x-1.5f)<1e-5f,"attachment preserves authored world scale");
    Check(childPose&&std::abs(childPose->position.x-1.5f)<1e-5f&&std::abs(childPose->position.z-2)<1e-5f,"scaled prefab descendant follows attachment");

}
static void FrameConsistency(){
    World w;auto root=Node(w,"Physical root",{20,30,40});Mesh(w,root);auto& rb=w.AddComponent<RigidbodyComponent>(root);rb.velocity={1,2,3};w.AddComponent<ColliderComponent>(root);
    auto& a=w.AddComponent<CameraAttachmentComponent>(root);a.localPosition={.25f,-.35f,2};a.localRotation={.1f,.2f,.3f};w.GetComponent<TransformComponent>(root)->scale={.5f,.5f,.5f};
    auto child=Node(w,"Child",{1,0,0});Mesh(w,child);Check(w.SetParent(child,root,false),"render fixture hierarchy");
    auto local=w.LocalTransform(root);local.position=a.localPosition;local.rotation=a.localRotation;const auto rootRelative=Model(RenderPose::From(local));const auto childRelative=Product(rootRelative,Model(RenderPose::From(w.LocalTransform(child))));
    auto cam=Node(w,"Camera rig");w.AddComponent<CameraComponent>(cam);w.AddComponent<RigidbodyComponent>(cam);w.AddComponent<ColliderComponent>(cam);
    ResourceManager resources(nullptr);resources.Load<MeshResource>("models/primitives/cube.obj");resources.Load<ShaderResource>("dx12/textured.hlsl");resources.Load<MaterialResource>("materials/default_primitive.material.json");
    Recorder recorder;RenderSystem render;render.SetResourceManager(&resources);render.SetRenderAdapter(&recorder);render.SetCameraAttachmentsEnabled(true);
    RenderInterpolation interpolation;render.SetInterpolation(&interpolation);auto old=w.LocalTransform(root);auto velocity=rb.velocity;
    auto verify=[&](const CameraState& camera,bool instances){
        render.SetCameraState(camera);render.SetInstancingEnabled(instances);recorder.BeginFrame();render.Update(w,0);
        Check(recorder.draws.size()==2,"two ready visual meshes submitted");
        for(const auto& row:recorder.draws){auto relative=Product(camera.view,row.model);Check(Equal(relative,rootRelative)||Equal(relative,childRelative),"same-frame camera cancellation: no lag or accumulation");Check(Equal(row.mvp,Product(camera.projection,relative)),"MVP uses same current CameraState");}
        Check(recorder.shadows.size()==2,"shadow casters use visual attachments");for(const auto& row:recorder.shadows){auto relative=Product(camera.view,row.model);Check(Equal(relative,rootRelative)||Equal(relative,childRelative),"shadow model matches visual pose");}
        if(instances&&!recorder.failBatch)Check(recorder.batches==1,"actual instancing path exercised");
    };
    auto initial=BuildCameraState({}, {},1.5f);render.SetCameraState(initial);for(int i=0;i<12;++i){recorder.BeginFrame();render.Update(w,0);}
    scripting::GameplayCamera legacy;
    for(int frame=0;frame<120;++frame){
        auto& t=*w.GetComponent<TransformComponent>(cam);interpolation.BeginTick(w);t.position={float(frame)*.05f,std::sin(frame*.1f),float(frame)*-.03f};t.rotation={.2f*std::sin(frame*.1f),frame*.027f,.1f};interpolation.EndTick(w);interpolation.SetFrame(w,true,(frame%10)/10.);
        w.GetComponent<CameraComponent>(cam)->projection=frame%2?CameraProjection::Perspective:CameraProjection::Orthographic;
        const auto state=ResolveCameraState(w,cam,1.5f,&interpolation);verify(state,frame%3!=0);
        legacy.position=state.pose.position;legacy.yaw=frame*.03f;legacy.pitch=.4f*std::sin(frame*.2f);
        verify(BuildCameraState({legacy.position,{1,1,1},RenderRotation({-legacy.pitch,legacy.yaw,0})},{},1.5f),false);
    }
    recorder.failBatch=true;verify(initial,true);recorder.failBatch=false;
    Check(w.LocalTransform(root).position.x==old.position.x&&w.LocalTransform(root).rotation.x==old.rotation.x&&rb.velocity.x==velocity.x&&rb.velocity.z==velocity.z,"rendering never mutates physical Transform/Rigidbody");
    render.SetCameraAttachmentsEnabled(false);recorder.BeginFrame();render.Update(w,0);Check(std::any_of(recorder.draws.begin(),recorder.draws.end(),[&](const auto& row){return Equal(row.model,Model(RenderPose::From(w.WorldTransform(root))));}),"Edit rendering uses authored pose");
    render.SetCameraAttachmentsEnabled(true);w.RemoveComponent<CameraAttachmentComponent>(root);recorder.BeginFrame();render.Update(w,0);Check(std::any_of(recorder.draws.begin(),recorder.draws.end(),[&](const auto& row){return Equal(row.model,Model(RenderPose::From(w.WorldTransform(root))));}),"detach has no stale render override");
    w.AddComponent<CameraAttachmentComponent>(root);w.AddComponent<CameraAttachmentComponent>(child)={true,{0,0,4},{0,.2f,0}};auto nested=CameraAttachmentPose(w,child,initial);Check(nested&&std::abs(nested->position.z-4)<1e-5,"nearest nested attachment wins");
    w.DestroyEntity(root);recorder.BeginFrame();render.Update(w,0);Check(recorder.draws.empty(),"destroyed subtree leaves no packets");render.ReleaseGpuResources();render.SetRenderAdapter(nullptr);
}
static void Lua(Files& files,const std::string& prefab){
    const auto path=files.Write("attach.lua","return {on_start=function(self) self.model=World.spawn_prefab('"+prefab+"',Vec3.new(11,12,13));Camera.attach(self.model,Vec3.new(.1,-.2,2),Vec3.new(.2,.3,.4)) end,on_update=function(self) local x=World.get_position(self.entity).x;if x==1 then Camera.attach(self.model,Vec3.new(1,2,3),Vec3.new(0,0,0)) elseif x==2 then Camera.detach(self.model) elseif x==3 then Camera.attach(self.model,Vec3.new(0,0,2),Vec3.new(0,0,0));World.destroy(self.model) end end}");
    World w;scripting::GameplayCamera camera;scripting::GameplayServices host;host.camera=&camera;auto actor=Actor(w,path);scripting::LuaRuntime lua;lua.Start(w,{},host);lua.Update(0);Check(lua.Error(path).empty(),lua.Error(path));auto root=PrefabRoot(w);Check(root.IsValid(),"Lua spawns and attaches prefab");
    Check(w.GetComponent<CameraAttachmentComponent>(root)->localPosition.z==2&&w.LocalTransform(root).position.x==11,"attach only changes component, not Transform");
    w.GetComponent<TransformComponent>(actor)->position.x=1;lua.Update(0);Check(w.GetComponent<CameraAttachmentComponent>(root)->localPosition.x==1,"repeat attach updates offsets");
    w.GetComponent<TransformComponent>(actor)->position.x=2;lua.Update(0);Check(!w.HasComponent<CameraAttachmentComponent>(root)&&w.LocalTransform(root).position.y==12,"Lua detach restores physical display source");
    w.GetComponent<TransformComponent>(actor)->position.x=3;lua.Update(0);Check(!w.IsAlive(root)&&lua.Error(path).empty(),"attach then destroy safe phase boundary");lua.Stop();
    auto denied=files.Write("denied.lua","return {on_start=function(self) Camera.attach(self.entity,Vec3.new(0,0,1),Vec3.new(0,0,0)) end}");World d;auto target=Actor(d,denied);d.GetComponent<LuaScriptComponent>(target)->cameraEnabled=false;lua.Start(d,{},host);lua.Update(0);Check(lua.Error(denied).find("permission")!=std::string::npos&&!d.HasComponent<CameraAttachmentComponent>(target),"camera permission required without granting physics writes");lua.Stop();
    auto dead=files.Write("dead.lua","return {on_start=function(self) local target=World.spawn_prefab('"+prefab+"',Vec3.new(0,0,0));World.destroy(target);Camera.attach(target,Vec3.new(0,0,1),Vec3.new(0,0,0)) end}");World stale;auto e=Actor(stale,dead);lua.Start(stale,{},host);lua.Update(0);Check(stale.IsAlive(e)&&stale.GetAliveCount()==1&&lua.Error(dead).find("Dead")!=std::string::npos,"destroy-pending attachment handle rejected");lua.Stop();
}
#if defined(ENABLE_DX12)
static void Native(Files& files,const std::string& prefab){
    Application app;Check(app.Initialize(),"native initialize");SyntheticEditorInput input;glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());app.NewScene();auto& ui=app.GetEditorLayer();ui.SetFixedDiagnosticLayout(true);ui.SetDiagnosticViewport(360,240);ui.SetGameView(true);
    auto frame=[&](unsigned n=4){app.SetFrameLimit(n);Check(app.Run()==0,"native frames");};
    auto author=ui.CreateEntity(app,"Attachment Inspector");Check(ui.AddComponent(app,author,ComponentId("CameraAttachment")),ui.GetError());
    Check(ui.SetProperty(app,author,ComponentId("CameraAttachment"),"localPosition",Vec3{.4f,.1f,2}),ui.GetError());ui.UndoEdit(app);Check(app.GetWorld().GetComponent<CameraAttachmentComponent>(author)->localPosition.x==0,"attachment Undo");ui.RedoEdit(app);frame();Check(ImGui::FindWindowByName("Inspector")->Active,"actual attachment Inspector draws");
    auto a=ui.CreateCamera(app),b=ui.CreateCamera(app);auto& wa=app.GetWorld();wa.GetComponent<TransformComponent>(a)->position={3,2,-5};wa.GetComponent<CameraComponent>(b)->projection=CameraProjection::Orthographic;wa.GetComponent<CameraComponent>(b)->orthographicSize=5;
    auto path=files.Write("native.lua","return {fields={first={type='entity'},second={type='entity'}},on_start=function(self) Camera.set_active(self.first);self.model=World.spawn_prefab('"+prefab+"',Vec3.new(100,200,300));self.stage=0 end,on_update=function(self) local x=World.get_position(self.entity).x;if x==1 and self.stage<1 then Camera.set_active(self.second);Camera.attach(self.model,Vec3.new(.3,-.2,3),Vec3.new(0,.2,0));self.stage=1 elseif x==2 and self.stage<2 then Camera.detach(self.model);self.stage=2 elseif x==3 and self.stage<3 then World.destroy(self.model);self.model=World.spawn_prefab('"+prefab+"',Vec3.new(200,300,400));Camera.attach(self.model,Vec3.new(0,0,3),Vec3.new(0,0,0));self.stage=3 end end}");
    auto actor=Actor(wa,path);wa.GetComponent<LuaScriptComponent>(actor)->properties={{"first",Ref(wa,a)},{"second",Ref(wa,b)}};
    auto settings=app.GetSceneSettings();settings.environment.enabled=false;settings.shadows.enabled=false;settings.antiAliasing=AntiAliasingMode::FxaaHigh;settings.background.background=EditorBackground::Solid;settings.background.solid={.02f,.03f,.04f};Check(app.SetSceneSettings(settings),"native attachment settings");app.SetViewportUnlit(true);ui.ClearSelection();
    auto authored=SceneSerializer::CaptureWorld(wa,app.GetSceneSettings());app.SetEditorPlayMode(true);app.SetEditorPaused(true);app.GetLuaRuntime().Update(0);Check(app.GetLuaRuntime().Error(path).empty(),app.GetLuaRuntime().Error(path));frame(16);
    auto& runtime=app.GetWorld();auto root=PrefabRoot(runtime);Check(root.IsValid()&&runtime.HasComponent<CameraAttachmentComponent>(root),"prefab component renders without repeated attach");auto physical=runtime.LocalTransform(root);auto ra=app.MapToRuntime(a),rb=app.MapToRuntime(b),controller=app.MapToRuntime(actor);auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());
    auto centroid=[&](){auto pixels=gpu->ReadBackHdrPixels();Check(pixels.size()==360*240*4,"native attachment HDR");double x=0,y=0,count=0;for(int j=0;j<240;++j)for(int i=0;i<360;++i)if(pixels[(j*360+i)*4]>.3f){x+=i;y+=j;++count;}return std::array<double,3>{count?x/count:0,count?y/count:0,count};};
    auto reference=centroid();Check(reference[2]>50,"attached prefab root and child visible");
    for(int i=0;i<12;++i){auto& t=*runtime.GetComponent<TransformComponent>(ra);t.position={i*.4f,2+std::sin(float(i)),float(i)*-.3f};t.rotation={.3f*std::sin(float(i)),i*.17f,.2f};frame(2);auto next=centroid();Check(std::hypot(next[0]-reference[0],next[1]-reference[1])<.05,"no camera-relative image lag on current render frame");}
    Check(runtime.LocalTransform(root).position.x==physical.position.x&&runtime.LocalTransform(root).rotation.x==physical.rotation.x,"native render leaves authored/physical pose intact");
    runtime.GetComponent<TransformComponent>(controller)->position.x=1;app.GetLuaRuntime().Update(0);frame();Check(app.GetRenderCameraState().entity==rb&&app.GetRenderCameraState().projection[15]==1&&centroid()[2]>50,"ECS camera switch, Orthographic and live offset change");
    runtime.GetComponent<TransformComponent>(controller)->position.x=2;app.GetLuaRuntime().Update(0);frame();Check(!runtime.HasComponent<CameraAttachmentComponent>(root)&&centroid()[2]==0,"detach restores distant authored pose with no stale packets");
    runtime.GetComponent<TransformComponent>(controller)->position.x=3;app.GetLuaRuntime().Update(0);frame();auto second=PrefabRoot(runtime);Check(!runtime.IsAlive(root)&&second.IsValid()&&second!=root&&centroid()[2]>50,"replace prefab safely without stale generation override");
    runtime.GetComponent<CameraComponent>(ra)->enabled=false;runtime.DestroyEntity(rb);frame();Check(!app.GetRenderCameraState().entity.IsValid()&&centroid()[2]>50,"ECS to legacy fallback retains camera attachments");
    app.SetEditorPlayMode(false);frame();Check(SceneSerializer::CaptureWorld(wa,app.GetSceneSettings())==authored,"Stop preserves authored components and transforms");
    app.SetEditorPlayMode(true);app.SetEditorPaused(true);app.GetLuaRuntime().Update(0);frame();Check(PrefabRoot(app.GetWorld()).IsValid()&&!app.GetWorld().IsAlive(second),"repeat Play uses fresh attachment handles");app.SetEditorPlayMode(false);
    Check(gpu->GetValidationErrorCount()==0,"zero DX12 validation errors");app.Shutdown();
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{Files files;auto prefab=Prefab(files);
#if defined(ENABLE_DX12)
    if(argc>1&&std::string(argv[1])=="--dx12")Native(files,prefab);else
#endif
    {Registry(files,prefab);FrameConsistency();Lua(files,prefab);}
    std::cout<<"Camera Attachments: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
