#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "ecs/PlayerSpawn.h"
#include "ecs/CameraAttachments.h"
#include "ecs/ComponentRegistry.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "ecs/components/PrefabInstanceComponent.h"
#include "scripting/EntityReference.h"
#include "scene/SceneSerializer.h"
#include <fstream>
#include <iostream>
#if defined(ENABLE_DX12)
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#endif
using namespace ecs;
static unsigned checks=0;
static void Check(bool value,const std::string& text){if(!value)throw std::runtime_error(text);++checks;}
static void Near(float a,float b,const char* text){Check(std::abs(a-b)<.002f,text);}
static Entity Node(World& w,Vec3 position={}){auto e=w.CreateEntity();w.AddComponent<TransformComponent>(e).position=position;return e;}
static Entity Start(World& w,const std::string& key,Vec3 position={3,4,5}){auto e=Node(w,position);w.AddComponent<PlayerStartComponent>(e).playerPrefab=key;return e;}
static Entity Player(const World& w){for(auto e:w.Entities())if(w.HasComponent<PlayerComponent>(e))return e;return {};}
static nlohmann::json Ref(World& w,Entity e){if(!w.HasComponent<SceneIdentityComponent>(e))w.AddComponent<SceneIdentityComponent>(e);return {{"$entity",w.GetComponent<SceneIdentityComponent>(e)->value}};}
static std::string Bytes(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
struct Files {
    std::string token="player_"+NewSceneIdentity(),prefabKey,scriptKey;
    std::filesystem::path prefabs,scripts;
    Files(){std::replace(token.begin(),token.end(),'-','_');auto root=AssetPaths::ResolveAssetRoot();prefabs=root/"prefabs"/token;scripts=root/"scripts"/token;Check(std::filesystem::create_directory(prefabs)&&std::filesystem::create_directory(scripts),"owned fixture directories");prefabKey="prefabs/"+token+"/player.prefab.json";scriptKey="scripts/"+token+"/controller.lua";}
    ~Files(){std::error_code error;std::filesystem::remove_all(prefabs,error);std::filesystem::remove_all(scripts,error);}
    std::string WritePrefab(const std::string& name,const nlohmann::json& data){std::string error;Check(SceneSerializer::WriteAtomic(prefabs/name,data,&error,false),error);return "prefabs/"+token+"/"+name;}
    void Prepare(){
        std::ofstream(scripts/"identity.lua")<<"return {value=7}";
        std::ofstream(scripts/"controller.lua")<<"local M=Script.require('"<<token<<".identity');return {fields={camera={type='entity'},visual={type='entity'}},on_start=function(self) assert(M.value==7 and World.is_alive(self.camera) and World.get_parent(self.camera)==self.entity);assert(World.find_by_tag('Player')[1]==self.entity);Camera.set_active(self.camera);local f=Camera.get_forward(self.camera);local p=Camera.get_position(self.camera);assert(math.abs(f.x*f.x+f.y*f.y+f.z*f.z-1)<.0001 and p.y==World.get_position(self.camera).y);World.set_local_rotation(self.camera,Vec3.new(.1,.2,.3));Camera.get_up(self.camera);World.set_scale(self.visual,Vec3.new(.25,.25,.25)) end}";
        World w;auto root=Node(w,{100,200,300});w.GetComponent<TransformComponent>(root)->rotation={.1f,.2f,.3f};w.GetComponent<TransformComponent>(root)->scale={1.2f,1.2f,1.2f};
        w.AddComponent<RigidbodyComponent>(root).useGravity=false;w.AddComponent<ColliderComponent>(root).autoFitFromMesh=false;w.AddComponent<CharacterMovementComponent>(root).moveSpeed=6;
        auto camera=Node(w,{0,1.5f,0});w.AddComponent<CameraComponent>(camera);Check(w.SetParent(camera,root,false),"camera parenting");
        auto visual=Node(w);w.AddComponent<MeshRendererComponent>(visual).meshPath="models/primitives/cube.obj";w.AddComponent<MaterialComponent>(visual).materialPath="materials/default_primitive.material.json";w.AddComponent<CameraAttachmentComponent>(visual)={true,{.2f,-.1f,3},{}};Check(w.SetParent(visual,root,false),"visual parenting");
        auto& script=w.AddComponent<LuaScriptComponent>(root);script.scriptPath=scriptKey;script.cameraEnabled=true;script.inputEnabled=true;script.properties={{"camera",Ref(w,camera)},{"visual",Ref(w,visual)}};
        WritePrefab("player.prefab.json",scripting::CapturePrefab(w,root));
    }
};
static void Cpu(Files& f){
    auto source=Bytes(f.prefabs/"player.prefab.json");World w;auto parent=Node(w,{10,0,0});w.GetComponent<TransformComponent>(parent)->rotation={.15f,.7f,.1f};w.GetComponent<TransformComponent>(parent)->scale={2,2,2};
    auto marker=Start(w,f.prefabKey);Check(w.SetParent(marker,parent,false),"parented marker");auto spawn=w.WorldTransform(marker);auto authored=SceneSerializer::CaptureWorld(w);std::string error;
    Check(SceneSerializer::SaveWorld(f.prefabs/"scene.json",w,&error),error);World loaded;SceneSettings settings;Check(SceneSerializer::LoadWorld(f.prefabs/"scene.json",loaded,settings,&error),error);Check(SceneSerializer::CaptureWorld(loaded)==authored,"scene link roundtrip");
    World::CloneMap map;auto runtime=w.CloneData(&map);bool prefab=false;TransformComponent point;auto player=SpawnPlayer(*runtime,&point,&prefab);
    Check(prefab&&runtime->IsAlive(player)&&runtime->Subtree(player).size()==3,"runtime prefab root and descendants");Check(!runtime->IsAlive(map.at(marker))&&w.IsAlive(marker),"runtime marker removed; authoring untouched");
    auto t=runtime->LocalTransform(player);Near(t.position.x,spawn.position.x,"spawn world X; no prefab translation twice");Near(t.position.y,spawn.position.y,"spawn world Y");Near(t.position.z,spawn.position.z,"spawn world Z");Near(t.scale.x,1.2f,"marker scale ignored");
    TransformComponent local;local.rotation={.1f,.2f,.3f};TransformComponent rotation;rotation.rotation=spawn.rotation;float ma[16],mb[16],actual[16];BuildRenderModelMatrix(ma,RenderPose::From(rotation));BuildRenderModelMatrix(mb,RenderPose::From(local));ImportMatrix a,b;std::copy(ma,ma+16,a.begin());std::copy(mb,mb+16,b.begin());auto expected=MultiplyMatrix(a,b);auto r=t;r.position={};r.scale={1,1,1};BuildRenderModelMatrix(actual,RenderPose::From(r));for(int i=0;i<16;++i)Near(actual[i],float(expected[i]),"full spawn rotation composition");
    Check(runtime->HasComponent<RigidbodyComponent>(player)&&runtime->HasComponent<ColliderComponent>(player)&&runtime->HasComponent<CharacterMovementComponent>(player),"prefab owns physics and movement");Check(!runtime->HasComponent<TagComponent>(player),"no required or injected Tag component");
    auto* script=runtime->GetComponent<LuaScriptComponent>(player);auto camera=scripting::ResolveEntityReference(*runtime,script->properties.at("camera"));auto visual=scripting::ResolveEntityReference(*runtime,script->properties.at("visual"));Check(runtime->Parent(camera)==player&&runtime->HasComponent<CameraComponent>(camera),"fresh prefab-local camera reference");
    scripting::GameplayCamera hostCamera;scripting::GameplayServices host;host.camera=&hostCamera;scripting::LuaRuntime lua;lua.Start(*runtime,{},host);lua.Update(0);Check(lua.Error(f.scriptKey).empty(),lua.Error(f.scriptKey));Near(runtime->WorldTransform(visual).scale.x,.25f,"on_start after complete spawn and module loading");Check(hostCamera.Active(*runtime)==camera,"Lua selects child camera");
    auto state=ResolveCameraState(*runtime,camera,1.5f);auto pose=CameraAttachmentPose(*runtime,visual,state);Check(pose.has_value(),"player visual uses Camera Attachments");
    PhysicsSystem physics(nullptr,9.81f,.985f,3,.05f,.85f,12);runtime->GetComponent<RigidbodyComponent>(player)->velocity={1,0,0};auto before=runtime->WorldTransform(player).position.x;physics.Update(*runtime,1.f/60);Check(runtime->WorldTransform(player).position.x>before,"spawned body participates in physics");
    runtime->DestroyEntity(player);lua.Update(0);Check(!runtime->IsAlive(camera)&&!CameraAttachmentPose(*runtime,visual,state),"destroy subtree invalidates refs and attachments");lua.Stop();Check(SceneSerializer::CaptureWorld(w)==authored&&Bytes(f.prefabs/"player.prefab.json")==source,"runtime does not modify scene or prefab");
    // Generic, non-character prefab: preserve pivot/scale, add identity only.
    World generic;auto root=Node(generic,{100,200,300});generic.GetComponent<TransformComponent>(root)->pivot={.1f,.2f,.3f};generic.GetComponent<TransformComponent>(root)->scale={2,3,4};auto genericKey=f.WritePrefab("generic.prefab.json",scripting::CapturePrefab(generic,root));World simple;Start(simple,genericKey);auto p=SpawnPlayer(simple);Check(simple.GetAliveCount()==1&&simple.HasComponent<PlayerComponent>(p)&&!simple.HasComponent<RigidbodyComponent>(p)&&!simple.HasComponent<CharacterMovementComponent>(p)&&!simple.HasComponent<CameraComponent>(p),"generic prefab needs no body, controller or camera");Near(simple.LocalTransform(p).pivot.y,.2f,"generic pivot retained");Near(simple.LocalTransform(p).scale.y,3,"generic scale retained");
    World none;Check(!SpawnPlayer(none).IsValid(),"no PlayerStart keeps no implicit player");auto off=Start(none,f.prefabKey);none.GetComponent<PlayerStartComponent>(off)->enabled=false;Check(!SpawnPlayer(none).IsValid()&&none.GetAliveCount()==0,"disabled markers removed without spawn");
    World many;auto first=Start(many,genericKey,{1,2,3});Start(many,f.prefabKey,{7,8,9});Check(many.LocalTransform(SpawnPlayer(many)).position.x==1&&many.GetAliveCount()==1&&!many.IsAlive(first),"first eligible marker wins deterministically");
    World legacy;auto old=Start(legacy,"");auto& s=*legacy.GetComponent<PlayerStartComponent>(old);s.characterHeight=2;s.eyeHeight=1.8f;auto oldPlayer=SpawnPlayer(legacy);Near(legacy.LocalTransform(oldPlayer).position.y,5,"legacy feet/body rule");Near(legacy.GetComponent<PlayerComponent>(oldPlayer)->eyeHeight,1.8f,"legacy eye height");Check(legacy.GetComponent<LuaScriptComponent>(oldPlayer)->scriptPath=="scripts/FirstPersonController.lua","legacy controller retained");
    // Read and post-instantiation failures must leave no partial player or fallback.
    auto invalid=f.WritePrefab("invalid.prefab.json",{{"version",2},{"entities",nlohmann::json::array()}});
    World markerPrefab;auto bad=Start(markerPrefab,"");auto recursive=f.WritePrefab("marker.prefab.json",scripting::CapturePrefab(markerPrefab,bad));
    for(const auto& key:{"prefabs/"+f.token+"/missing.prefab.json",std::string("../escape.prefab.json"),invalid,recursive}){World fail;auto start=Start(fail,key);try{SpawnPlayer(fail);Check(false,"invalid prefab must fail");}catch(const std::exception& e){Check(std::string(e.what()).find(key)!=std::string::npos,"specific assigned prefab diagnostic");}Check(fail.IsAlive(start)&&fail.GetAliveCount()==1&&!Player(fail).IsValid()&&fail.GetComponent<PlayerStartComponent>(start)->playerPrefab==key,"failure rollback without marker loss or fallback");}
    MapsAndModesSettings modes;modes.defaultGameMode="Default";modes.defaultPlayerPrefab=genericKey;modes.modes["Default"]={f.prefabKey,true};
    auto resolve=[&](int mode,const std::string& overrideKey){World test;auto marker=Start(test,overrideKey);test.GetComponent<PlayerStartComponent>(marker)->spawnMode=mode;auto player=SpawnPlayer(test,nullptr,nullptr,&modes);return test.HasComponent<PrefabInstanceComponent>(player)?test.GetComponent<PrefabInstanceComponent>(player)->asset:std::string();};
    Check(resolve(3,"")==f.prefabKey,"GameMode prefab wins project fallback");Check(resolve(2,genericKey)==genericKey,"explicit override wins GameMode");Check(resolve(1,f.prefabKey).empty(),"explicit Legacy ignores assigned prefab");Check(resolve(0,"").empty(),"old empty assignment remains legacy despite defaults");
    modes.modes["Default"].defaultPlayerPrefab.clear();Check(resolve(3,"")==genericKey,"project fallback");modes.modes["Default"].autoSpawnPlayer=false;World offMode;Start(offMode,f.prefabKey);Check(!SpawnPlayer(offMode,nullptr,nullptr,&modes).IsValid()&&offMode.GetAliveCount()==0,"Auto Spawn off removes markers but spawns no player");
    modes.modes["Default"].autoSpawnPlayer=true;World noMarker;Check(!SpawnPlayer(noMarker,nullptr,nullptr,&modes).IsValid(),"Auto Spawn requires marker");modes.defaultGameMode="Missing";try{World badMode;Start(badMode,"");SpawnPlayer(badMode,nullptr,nullptr,&modes);Check(false,"missing GameMode must fail");}catch(const std::exception& e){Check(std::string(e.what()).find("Missing GameMode")!=std::string::npos,"missing GameMode diagnostic");}
    auto& registry=ComponentRegistry::Builtins();auto d=registry.Find(ComponentId("PlayerStart"));auto property=std::find_if(d->properties.begin(),d->properties.end(),[](const auto& p){return p.key=="playerPrefab";});Check(property!=d->properties.end()&&property->asset==AssetKind::Prefab,"existing Prefab resource picker descriptor");Check(!registry.Set(w,marker,d->id,"playerPrefab",std::string("../escape.prefab.json"),&error),"unsafe picker key rejected");
}
#if defined(ENABLE_DX12)
static void Native(Files& f){
    Application app;Check(app.Initialize(),"native initialize");SyntheticEditorInput input;glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());app.NewScene();auto& ui=app.GetEditorLayer();ui.SetFixedDiagnosticLayout(true);ui.SetDiagnosticViewport(360,240);
    auto frame=[&](unsigned n=4){app.SetFrameLimit(n);Check(app.Run()==0,"native frames");};frame(12);
    auto marker=ui.CreatePlayerStart(app);auto& authored=app.GetWorld();Check(ui.SetProperty(app,marker,ComponentId("Transform"),"position",Vec3{3,4,5}),ui.GetError());
    // Open the actual standard resource picker on the PlayerStart Inspector field.
    frame();auto* inspector=ImGui::FindWindowByName("Inspector");Check(inspector&&inspector->Active,"PlayerStart Inspector");ImGui::SetWindowDock(inspector,0,ImGuiCond_Always);ImGui::SetWindowPos(inspector,{600,100});ImGui::SetWindowSize(inspector,{500,650});frame();
    auto seed=ImHashData(&marker.index,sizeof(marker.index),inspector->ID);seed=ImHashData(&marker.generation,sizeof(marker.generation),seed);auto component=ImHashStr("PlayerStart",0,seed);auto prop=ImHashStr("playerPrefab",0,ImHashStr("Properties",0,component));auto picker=ImHashStr("##value",0,prop);
    ImGui::ActivateItemByID(picker);frame();auto* combo=ImGui::FindWindowByName("##Combo_00");Check(combo&&combo->Active,"native Player Prefab picker opens");ImGui::ActivateItemByID(ImHashStr(f.prefabKey.c_str(),0,combo->ID));frame();Check(authored.GetComponent<PlayerStartComponent>(marker)->playerPrefab==f.prefabKey,"native picker assigns indexed Prefab");
    ui.UndoEdit(app);Check(authored.GetComponent<PlayerStartComponent>(marker)->playerPrefab.empty(),"assignment Undo");ui.RedoEdit(app);Check(authored.GetComponent<PlayerStartComponent>(marker)->playerPrefab==f.prefabKey,"assignment Redo");
    std::string error;const auto sceneKey="scenes/"+f.token+".json";auto scenePath=AssetPaths::ResolveAssetOutputPath(sceneKey);struct SceneFile{std::filesystem::path path;~SceneFile(){std::error_code e;std::filesystem::remove(path,e);}} owned{scenePath};Check(app.SaveSceneAs(sceneKey,false,&error),error);Check(app.OpenScene(sceneKey,&error),error);marker={};for(auto e:app.GetWorld().Entities())if(app.GetWorld().HasComponent<PlayerStartComponent>(e))marker=e;
    // Existing Prefab document editing changes only this generated fixture.
    Check(ui.OpenDocument(app,f.prefabKey),ui.GetError());frame();auto root=app.GetWorld().HierarchyRoots(app.GetWorld().Entities()).front();Check(ui.SetProperty(app,root,ComponentId("CharacterMovement"),"moveSpeed",7.f),ui.GetError());Check(ui.SaveActiveDocument(app),ui.GetError());ui.BackToSceneDocument(app);frame();Check(!app.IsPrefabEditing(),ui.GetError());
    auto savedPrefab=Bytes(f.prefabs/"player.prefab.json"),savedScene=Bytes(scenePath);auto before=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());
    auto enter=[&]{app.SetEditorPlayMode(true);Check(app.IsEditorPlayMode()&&app.GetPlayError().empty(),app.GetPlayError());app.SetEditorPaused(true);app.GetLuaRuntime().Update(0);frame(18);auto p=Player(app.GetWorld());Check(app.GetWorld().IsAlive(p),"runtime player in hierarchy");Near(app.GetWorld().LocalTransform(p).position.x,3,"native PlayerStart position");Near(app.GetWorld().GetComponent<CharacterMovementComponent>(p)->moveSpeed,7,"saved Prefab edit used at spawn");Check(app.GetLuaRuntime().Error(f.scriptKey).empty(),app.GetLuaRuntime().Error(f.scriptKey));auto camera=scripting::ResolveEntityReference(app.GetWorld(),app.GetWorld().GetComponent<LuaScriptComponent>(p)->properties.at("camera"));Check(app.GetRenderCameraState().entity==camera&&app.GetWorld().Parent(camera)==p,"render uses spawned child Camera Entity");return p;};
    auto first=enter();app.GetWorld().DestroyEntity(first);frame();Check(!app.GetRenderCameraState().entity.IsValid(),"destroyed player camera falls back safely");app.SetEditorPlayMode(false);frame();Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==before,"Stop restores exact authored Scene");auto second=enter();Check(second.world!=first.world,"repeat Play has fresh World handles");app.SetEditorPlayMode(false);frame();Check(Bytes(scenePath)==savedScene&&Bytes(f.prefabs/"player.prefab.json")==savedPrefab,"Play/Stop never writes source files");
    Check(ui.SetProperty(app,marker,ComponentId("PlayerStart"),"playerPrefab",std::string("prefabs/"+f.token+"/missing.prefab.json")),ui.GetError());auto failedBefore=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());auto identity=app.GetWorld().GetIdentity();auto selection=ui.GetSelectedEntity();app.SetEditorPlayMode(true);frame();Check(!app.IsEditorPlayMode()&&app.GetWorld().GetIdentity()==identity&&ui.GetSelectedEntity()==selection&&SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==failedBefore,"failed Play keeps authoring World whole");Check(!app.GetPlayError().empty()&&ImGui::FindWindowByName("Play failed")->Active,"specific persistent error visible in actual editor");ui.UndoEdit(app);app.SetEditorPlayMode(true);Check(app.IsEditorPlayMode()&&app.GetPlayError().empty(),"retry after fixing assignment");app.SetEditorPlayMode(false);frame();
    // Reload a generated project containing the saved scene and prefab assets.
    auto projectParent=AssetPaths::GetExecutableDirectory()/f.token;
    Check(std::filesystem::create_directory(projectParent),"owned project directory");
    struct OwnedProject {std::filesystem::path path;~OwnedProject(){std::error_code e;std::filesystem::remove_all(path,e);}} projectFiles{projectParent};
    auto project=ProjectManager::Create(projectParent,"PlayerWorkflow");
    std::filesystem::create_directories(project->ResolveAsset("prefabs/"+f.token));
    std::filesystem::create_directories(project->ResolveAsset("scripts/"+f.token));
    std::filesystem::copy(f.prefabs,project->ResolveAsset("prefabs/"+f.token),std::filesystem::copy_options::recursive);
    std::filesystem::copy(f.scripts,project->ResolveAsset("scripts/"+f.token),std::filesystem::copy_options::recursive);
    std::filesystem::copy_file(scenePath,project->ResolveScene("Scenes/Player.json"));
    Check(app.OpenProject(project->descriptorPath,Application::ProjectDecision::Discard,&error),error);
    Check(app.OpenScene("Scenes/Player.json",&error),error);frame();
    app.SetEditorPlayMode(true);Check(app.IsEditorPlayMode(),app.GetPlayError());app.SetEditorPaused(true);app.GetLuaRuntime().Update(0);frame();
    auto projectPlayer=Player(app.GetWorld());Check(projectPlayer.IsValid()&&app.GetLuaRuntime().Error(f.scriptKey).empty(),"project load then prefab player and Lua startup");
    Check(app.GetRenderCameraState().entity.IsValid(),"project player camera selection");app.SetEditorPlayMode(false);
    Check(app.CloseProject(Application::ProjectDecision::Discard,&error),error);frame();
    auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(gpu&&gpu->GetValidationErrorCount()==0,"zero DX12 Validation Errors");app.Shutdown();
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{Files files;files.Prepare();
#if defined(ENABLE_DX12)
    if(argc>1&&std::string(argv[1])=="--dx12")Native(files);else
#endif
    Cpu(files);std::cout<<"Player Workflow: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Player Workflow failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
