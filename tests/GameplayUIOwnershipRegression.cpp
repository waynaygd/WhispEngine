#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "ApplicationTestAccess.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "core/RuntimeLevel.h"
#include "ecs/components/UICanvasComponent.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "scripting/EntityReference.h"
#include "resources/ResourceManager.h"
#include <fstream>
#include <iostream>
#if defined(ENABLE_DX12)
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <GLFW/glfw3.h>
#endif
using namespace ecs;using Json=nlohmann::json;namespace fs=std::filesystem;
static unsigned checks=0;
static void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);++checks;}
static std::string Bytes(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static Entity Node(World& w,const std::string& name,Vec3 p={}){auto e=w.CreateEntity();w.AddComponent<TransformComponent>(e).position=p;w.AddComponent<TagComponent>(e).name=name;w.AddComponent<SceneIdentityComponent>(e);return e;}
static Entity Tagged(const World& w,const std::string& name){for(auto e:w.Entities())if(auto* t=w.GetComponent<TagComponent>(e);t&&t->name==name)return e;return {};}
static Json Ref(const World& w,Entity e){return {{"$entity",w.GetComponent<SceneIdentityComponent>(e)->value}};}
static Entity Hud(const World& w,Entity player){return scripting::ResolveEntityReference(w,w.GetComponent<LuaScriptComponent>(player)->properties.at("hud_entity"));}
static ui::Document Document(){
 auto d=ui::EmptyDocument();d.referenceResolution={400,300};
 ui::Widget text;text.id="Health";text.parent=d.root;text.type="Text";text.position={20,20};text.size={360,40};text.text="Authored";text.fontSize=24;d.widgets.push_back(text);
 ui::Widget bar;bar.id="HealthBar";bar.parent=d.root;bar.type="ProgressBar";bar.position={20,80};bar.size={200,20};bar.color={1,0,0,1};bar.fillColor={0,1,0,1};bar.value=1;d.widgets.push_back(bar);
 ui::Widget button;button.id="Button";button.parent=d.root;button.type="Button";button.position={20,140};button.size={100,40};d.widgets.push_back(button);ui::AnimationClip clip;clip.id="Fade";clip.widget="Health";clip.duration=.01f;d.animations.push_back(clip);return d;
}
struct Files {
 fs::path directory=AssetPaths::GetExecutableDirectory()/("hud_"+NewSceneIdentity());
 std::shared_ptr<const ProjectContext> project,other,previous=ProjectManager::Active();
 std::map<fs::path,std::string> originals;
 Files(){
  Check(directory.lexically_normal().parent_path()==AssetPaths::GetExecutableDirectory(),"owned directory containment");Check(fs::create_directory(directory),"unique fixture directory");project=ProjectManager::Create(directory,"Ownership");other=ProjectManager::Create(directory,"Other");ProjectManager::Activate(project);
  auto write=[&](const std::string& key,const std::string& value){auto p=project->ResolveAsset(key);fs::create_directories(p.parent_path());std::ofstream(p)<<value;};
  write("ui/hud.ui.json",Json(Document()).dump(2));
  write("scripts/hud_controller.lua",R"(return {
   fields={hud_entity={type='entity',label='Player HUD'},camera={type='entity'}},
   on_start=function(self)
    assert(World.is_alive(self.hud_entity) and World.get_parent(self.hud_entity)==self.entity)
    assert(World.is_alive(self.camera) and World.get_parent(self.camera)==self.entity)
    self.hud=ui.get(self.hud_entity)
    local second=World.get_position(self.entity).x>=5
    self.hud:set_text('Health',second and 'HP 25' or 'HP 75')
    self.hud:set_progress('HealthBar',second and 0.65 or 0.35)
   end,
   on_update=function(self)
    if not World.is_alive(self.hud_entity) then return end
    if not self.hud:valid() then self.hud=ui.get(self.hud_entity) end
    local p=World.get_position(self.entity);local command=p.z
    if command==10 then self.hud:set_text('Health','HP 10');self.hud:set_progress('HealthBar',0.1)
    elseif command==20 then assert(World.load_scene('Scenes/B.json'))
    elseif command==30 then assert(World.load_scene('Scenes/A.json'))
    elseif command==40 then World.destroy(self.entity) end
    if command==10 or command==20 or command==30 then World.set_position(self.entity,Vec3.new(p.x,p.y,0)) end
   end,
   on_stop=function(self) local e=World.find_by_tag('Observer')[1];if e then local p=World.get_position(e);World.set_position(e,Vec3.new(p.x,p.y+1,p.z)) end end,
   on_destroy=function(self) local e=World.find_by_tag('Observer')[1];if e then local p=World.get_position(e);World.set_position(e,Vec3.new(p.x,p.y+1,p.z)) end end
  })");
  write("scripts/canvas.lua",R"(return {on_ui_event=function(self,event) if event.type=='click' or event.type=='animation_complete' then local e=World.find_by_tag('Observer')[1];if e then local p=World.get_position(e);World.set_position(e,Vec3.new(p.x+1,p.y,p.z)) end end end})");
  World prefab;auto player=Node(prefab,"Player");auto& body=prefab.AddComponent<RigidbodyComponent>(player);body.useGravity=false;body.lockRotation=true;prefab.AddComponent<ColliderComponent>(player).autoFitFromMesh=false;prefab.AddComponent<CharacterMovementComponent>(player);
  auto camera=Node(prefab,"PlayerCamera",{0,1.7f,0});prefab.AddComponent<CameraComponent>(camera);Check(prefab.SetParent(camera,player,false),"camera child");
  auto hud=Node(prefab,"PlayerHUD");prefab.AddComponent<UICanvasComponent>(hud).document="ui/hud.ui.json";prefab.AddComponent<LuaScriptComponent>(hud).scriptPath="scripts/canvas.lua";Check(prefab.SetParent(hud,player,false),"HUD child");
  auto& script=prefab.AddComponent<LuaScriptComponent>(player);script.scriptPath="scripts/hud_controller.lua";script.properties={{"hud_entity",Ref(prefab,hud)},{"camera",Ref(prefab,camera)}};
  std::string error;Check(SceneSerializer::WriteAtomic(project->ResolveAsset("prefabs/player.prefab.json"),scripting::CapturePrefab(prefab,player),&error,false),error);
  prefab.DestroyEntity(hud);prefab.GetComponent<LuaScriptComponent>(player)->properties["hud_entity"]=nullptr;Check(SceneSerializer::WriteAtomic(project->ResolveAsset("prefabs/bare.prefab.json"),scripting::CapturePrefab(prefab,player),&error,false),error);
  for(auto name:{"A","B"}){World scene;auto marker=Node(scene,"Start",{std::string(name)=="B"?10.f:0.f,0,0});scene.AddComponent<PlayerStartComponent>(marker).spawnMode=3;Node(scene,"Observer");SceneSettings settings;settings.environment.enabled=false;settings.shadows.enabled=false;settings.background.background=EditorBackground::Solid;settings.background.solid={0,0,0};settings.antiAliasing=AntiAliasingMode::Off;Check(SceneSerializer::SaveWorld(project->ResolveScene("Scenes/"+std::string(name)+".json"),scene,&error,&settings),error);}
  auto settings=project->Settings();settings.startupScene="Scenes/A.json";settings.maps.gameStartupMap="Scenes/A.json";settings.maps.defaultGameMode="Default";settings.maps.modes["Default"]={"prefabs/player.prefab.json",true};project=ProjectManager::WithSettings(*project,settings);
  for(auto& entry:fs::recursive_directory_iterator(project->root))if(entry.is_regular_file()&&entry.path()!=project->ResolveAsset("prefabs/bare.prefab.json"))originals[entry.path()]=Bytes(entry.path());
 }
 void Verify(){for(auto& [p,data]:originals)Check(Bytes(p)==data,"immutable fixture source: "+p.filename().string());}
 ~Files(){ProjectManager::Activate(previous);if(directory.lexically_normal().parent_path()==AssetPaths::GetExecutableDirectory()){std::error_code e;fs::remove_all(directory,e);}}
};
static std::vector<float> Geometry(const ui::DrawList& d){std::vector<float> values;for(auto& v:d.vertices){values.insert(values.end(),{v.x,v.y,v.u,v.v});values.insert(values.end(),v.color.begin(),v.color.end());}for(auto& c:d.commands)values.insert(values.end(),{c.clip.x,c.clip.y,c.clip.w,c.clip.h});return values;}
static void Cpu(Files& files){
 ProjectManager::Activate(files.project);ResourceManager resources(nullptr);ui::RuntimeUI runtime;scripting::LuaRuntime lua;scripting::GameplayServices host;host.runtimeUI=&runtime;host.postfxResources=&resources;
 World world;auto observer=Node(world,"Observer");auto a=scripting::SpawnPrefab(world,"prefabs/player.prefab.json",{}),b=scripting::SpawnPrefab(world,"prefabs/player.prefab.json",{10,0,0});auto ha=Hud(world,a),hb=Hud(world,b);Check(ha!=hb&&world.Parent(ha)==a&&world.Parent(hb)==b,"two local typed references remapped independently");Check(world.HasComponent<UICanvasComponent>(ha)&&world.Subtree(a).size()==3,"Prefab contains authored camera and Canvas");
 lua.Start(world,{},host);lua.Update(0);Check(lua.Error("scripts/hud_controller.lua").empty(),lua.Error("scripts/hud_controller.lua"));auto first=runtime.Get(ha),second=runtime.Get(hb);Check(runtime.Read(first,"Health").text=="HP 75"&&runtime.Read(second,"Health").text=="HP 25","ui.get(entity) in on_start owns independent Lua-updated text");Check(std::abs(runtime.Read(first,"HealthBar").value-.35f)<.001f&&std::abs(runtime.Read(second,"HealthBar").value-.65f)<.001f,"independent progress state");
 bool ambiguous=false;try{runtime.Get("ui/hud.ui.json");}catch(const std::exception&){ambiguous=true;}Check(ambiguous,"legacy key lookup diagnoses ambiguous owners");
 auto font=ui::RasterizeFont();auto build=[&]{return runtime.Build(world,resources,400,300,font,{1},[](auto&){return RenderTextureHandle{};});};auto geometry=Geometry(build());world.GetComponent<TransformComponent>(a)->position={99,88,77};world.GetComponent<TransformComponent>(a)->rotation={.4f,.8f,.3f};world.GetComponent<TransformComponent>(a)->scale={2,3,4};Check(Geometry(build())==geometry,"screen overlay ignores parent translation rotation scale");
 // Queue a click for B, then delete the child before event dispatch.
 world.GetComponent<UICanvasComponent>(ha)->enabled=false;runtime.Sync(world,resources);runtime.Pointer(30,150,400,300,true,true);runtime.Pointer(30,150,400,300,false,true);lua.DispatchUIEvents();Check(world.GetComponent<TransformComponent>(observer)->position.x==1,"live Canvas still delivers interactive click to its behaviour");world.GetComponent<TransformComponent>(observer)->position.x=0;runtime.Pointer(30,150,400,300,true,true);runtime.Pointer(30,150,400,300,false,true);runtime.Play(second,"Fade");runtime.Advance(.1f);world.DestroyEntity(hb);lua.DispatchUIEvents();Check(world.GetComponent<TransformComponent>(observer)->position.x==0,"queued click and animation events skip a destroyed Canvas");runtime.Sync(world,resources);Check(!runtime.Valid(second)&&runtime.Instances().size()==1,"direct child deletion invalidates handle and state");Check(build().vertices.empty(),"disabled Canvas renders no geometry");world.GetComponent<UICanvasComponent>(ha)->enabled=true;
 runtime.Sync(world,resources);Check(runtime.Get("ui/hud.ui.json").entity==ha,"legacy document-key lookup retained for a unique Canvas");
 runtime.Set(first,"Health","text","runtime override");auto changed=Document();changed.widgets[1].text="new authored default";std::string error;Check(UILoader::Save("ui/hud.ui.json",changed,&error),error);resources.Reload<UIResource>("ui/hud.ui.json");runtime.Sync(world,resources);Check(!runtime.Valid(first),"reload invalidates old Canvas revision");first=runtime.Get(ha);Check(runtime.Read(first,"Health").text=="runtime override","UI3 override survives resource reload");
 world.DestroyEntity(a);Check(!world.IsAlive(ha)&&!runtime.Valid(first),"Player destroy cascades immediately to HUD handle validity");lua.Update(0);runtime.Sync(world,resources);Check(runtime.Instances().empty()&&build().vertices.empty(),"destroyed Player has no UI state or render data");Check(world.GetComponent<TransformComponent>(observer)->position.y==2,"deleted Player receives on_stop and on_destroy");auto respawnMarker=Node(world,"RespawnStart");auto& respawnSettings=world.AddComponent<PlayerStartComponent>(respawnMarker);respawnSettings.spawnMode=2;respawnSettings.playerPrefab="prefabs/player.prefab.json";auto respawn=SpawnPlayer(world);auto respawnHud=Hud(world,respawn);runtime.Sync(world,resources);lua.Update(0);auto fresh=runtime.Get(respawnHud);Check(respawn!=a&&respawnHud!=ha&&runtime.Valid(fresh)&&!runtime.Valid(first),"same-World Player respawn has new HUD generation and handle");Check(runtime.Read(fresh,"Health").text=="HP 75","respawn starts independent HUD state");lua.Stop();Check(world.GetComponent<TransformComponent>(observer)->position.y==6,"Stop retires remaining Player callbacks");
 {std::ofstream restore(files.project->ResolveAsset("ui/hud.ui.json"),std::ios::binary|std::ios::trunc);restore<<files.originals.at(files.project->ResolveAsset("ui/hud.ui.json"));}resources.ClearAll();
 World authored;auto marker=Node(authored,"Start");auto& start=authored.AddComponent<PlayerStartComponent>(marker);start.spawnMode=3;auto before=SceneSerializer::CaptureWorld(authored);
 for(auto scene:{"Scenes/A.json","Scenes/B.json","Scenes/A.json"}){auto level=PrepareRuntimeLevel(authored,files.project.get(),scene);auto player=level.player;Check(level.prefabSpawn&&level.world->Parent(Hud(*level.world,player))==player,"GameMode Player spawn includes typed HUD");lua.Start(*level.world,{},host);lua.Update(0);Check(lua.Error("scripts/hud_controller.lua").empty(),lua.Error("scripts/hud_controller.lua"));auto handle=runtime.Get(Hud(*level.world,player));Check(runtime.Valid(handle),"fresh Canvas after CPU travel preparation");lua.Stop();Check(!runtime.Valid(handle)&&runtime.Instances().empty(),"Stop retires UI before World destruction");}
 Check(SceneSerializer::CaptureWorld(authored)==before,"authored marker remains unchanged");
 // Missing/foreign typed references fail the behaviour safely, without aliasing another Canvas.
 World bad;auto root=scripting::SpawnPrefab(bad,"prefabs/player.prefab.json",{});bad.GetComponent<LuaScriptComponent>(root)->properties["hud_entity"]={{"$entity","missing"}};lua.Start(bad,{},host);lua.Update(0);Check(!lua.Error("scripts/hud_controller.lua").empty(),"invalid typed HUD link is diagnosed");lua.Stop();
 World otherWorld;auto foreign=Node(otherWorld,"Foreign");runtime.Sync(bad,resources);bool rejected=false;try{runtime.Get(foreign);}catch(const std::exception&){rejected=true;}Check(rejected,"foreign World entity cannot alias HUD");rejected=false;try{runtime.Get(root);}catch(const std::exception&){rejected=true;}Check(rejected,"non-Canvas Entity rejected without fallback lookup");runtime.Clear();resources.ClearAll();ProjectManager::Activate(files.other);runtime.Sync(otherWorld,resources);Check(runtime.Instances().empty(),"project change uses an empty independent UI session");ProjectManager::Activate(files.project);files.Verify();
}
#if defined(ENABLE_DX12)
static void Native(Files& files){
 ProjectManager::Activate(files.previous);Application app;Check(app.Initialize(Application::StartupMode::ProjectBrowser),"DX12 project browser");SyntheticEditorInput input;glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());std::string error;Check(app.OpenProject(files.project->descriptorPath,Application::ProjectDecision::Discard,&error),error);
 ApplicationTestAccess::FrameDelta(app,1.f/60);auto& editor=app.GetEditorLayer();editor.SetDiagnosticViewport(400,300);editor.SetUIPreviewVisible(false);auto frame=[&](unsigned count=8){app.SetFrameLimit(count);Check(app.Run()==0,"native editor frames");};frame();
 // Build a HUD through the existing Prefab Editor and the same operations used by Inspector.
 Check(editor.OpenPrefabEditor(app,"prefabs/bare.prefab.json"),editor.GetError());frame();auto root=editor.GetSelectedEntity();auto hud=editor.CreateChild(app,root);Check(editor.RenameEntity(app,hud,"PlayerHUD"),"name HUD child");Check(editor.AddComponent(app,hud,ComponentId("UICanvas")),editor.GetError());Check(editor.SetProperty(app,hud,ComponentId("UICanvas"),"document",std::string("ui/hud.ui.json")),editor.GetError());Check(editor.SetLuaEntityReference(app,root,"hud_entity",hud),"Inspector typed field assignment");editor.UndoEdit(app);Check(!Hud(app.GetWorld(),root).IsValid(),"typed field Undo");editor.RedoEdit(app);Check(Hud(app.GetWorld(),root)==hud,"typed field Redo");Check(editor.SavePrefabEditor(app,true),editor.GetError());Check(editor.ClosePrefabEditor(app),editor.GetError());Check(editor.OpenPrefabEditor(app,"prefabs/bare.prefab.json"),editor.GetError());frame();root=editor.GetSelectedEntity();hud=Hud(app.GetWorld(),root);Check(app.GetWorld().Parent(hud)==root&&app.GetWorld().GetComponent<UICanvasComponent>(hud)->document=="ui/hud.ui.json","Prefab reopen preserves Canvas and local reference");Check(editor.ClosePrefabEditor(app),editor.GetError());
 root=editor.InstantiatePrefab(app,"prefabs/bare.prefab.json");Check(root.IsValid(),editor.GetError());hud=Hud(app.GetWorld(),root);Check(editor.SetProperty(app,hud,ComponentId("UICanvas"),"sortOrder",7)&&editor.ApplyPrefab(app,root),editor.GetError());hud=Hud(app.GetWorld(),root);Check(editor.SetProperty(app,hud,ComponentId("UICanvas"),"sortOrder",9)&&editor.RevertPrefab(app,root),editor.GetError());hud=Hud(app.GetWorld(),root);Check(app.GetWorld().GetComponent<UICanvasComponent>(hud)->sortOrder==7,"Prefab Apply Revert preserves owned HUD");Check(editor.DeleteEntity(app,root),editor.GetError());frame();auto authored=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());
 auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(gpu,"DX12 adapter");auto pixels=[&]{return gpu->ReadBackViewportPixels();};auto at=[](int x,int y){return std::size_t(y*400+x)*4;};
 app.SetEditorPlayMode(true);frame(20);auto player=Tagged(app.GetWorld(),"Player");Check(player.IsValid(),app.GetPlayError());hud=Hud(app.GetWorld(),player);auto handle=app.GetRuntimeUI().Get(hud);Check(app.GetLuaRuntime().Error("scripts/hud_controller.lua").empty(),app.GetLuaRuntime().Error("scripts/hud_controller.lua"));Check(app.GetRuntimeUI().Read(handle,"Health").text=="HP 75","Lua receives HUD before on_start");auto original=pixels();Check(original[at(30,90)+1]>240&&original[at(130,90)]>240,"real HUD bar pixels after Player spawn with Preview Off");unsigned letters=0;for(int y=20;y<60;++y)for(int x=20;x<180;++x)letters+=original[at(x,y)]>200;Check(letters>80,"Lua-authored HUD text reaches actual DX12 pixels");
 app.GetWorld().GetComponent<TransformComponent>(player)->position={3,4,2};app.GetWorld().GetComponent<TransformComponent>(player)->rotation={0,.8f,0};frame();Check(pixels()==original,"Player Transform never moves Screen Space HUD");
 app.GetWorld().GetComponent<TransformComponent>(player)->position.z=10;frame();auto edited=pixels();Check(app.GetRuntimeUI().Read(handle,"Health").text=="HP 10"&&edited[at(50,90)]>240&&edited[at(50,90)+1]<10&&edited!=original,"Lua text and ProgressBar update independently of resource");
 app.GetWorld().GetComponent<UICanvasComponent>(hud)->enabled=false;frame();auto hidden=pixels();Check(hidden[at(30,90)+1]<10,"disabled HUD hidden");app.GetWorld().GetComponent<UICanvasComponent>(hud)->enabled=true;frame();Check(pixels()[at(30,90)+1]>240,"HUD state restored on enable");
 for(auto destination:{"Scenes/B.json","Scenes/A.json"}){auto oldPlayer=player;auto oldHud=hud;auto old=handle;auto oldWorld=app.GetWorld().GetIdentity();app.GetWorld().GetComponent<TransformComponent>(player)->position.z=std::string(destination)=="Scenes/B.json"?20.f:30.f;app.GetLuaRuntime().Update(0);Check(app.GetWorld().GetIdentity()==oldWorld,"Lua travel request is asynchronous");frame(16);Check(app.GetLevelTravelError().empty(),app.GetLevelTravelError());Check(!app.GetWorld().IsAlive(oldPlayer)&&!app.GetWorld().IsAlive(oldHud)&&!app.GetRuntimeUI().Valid(old),"travel rejects old Player HUD and Canvas handle");player=Tagged(app.GetWorld(),"Player");hud=Hud(app.GetWorld(),player);handle=app.GetRuntimeUI().Get(hud);Check(app.GetRuntimeUI().Instances().size()==1,"travel has exactly one new owned HUD");Check(app.GetRuntimeUI().Read(handle,"Health").text==(std::string(destination)=="Scenes/B.json"?"HP 25":"HP 75"),"travel creates new state without HP persistence");auto image=pixels();Check(image[at(30,90)+1]>240,"new HUD rendered after travel");}
 auto old=handle;app.GetWorld().GetComponent<TransformComponent>(player)->position.z=40;frame();Check(!app.GetWorld().IsAlive(hud)&&!app.GetRuntimeUI().Valid(old)&&app.GetRuntimeUI().Instances().empty(),"Lua destroys Player and HUD state");Check(pixels()[at(30,90)+1]<10,"no stale HUD render data after deletion");app.SetEditorPlayMode(false);frame();Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==authored,"Stop restores authored scene");
 app.SetEditorPlayMode(true);frame(12);player=Tagged(app.GetWorld(),"Player");handle=app.GetRuntimeUI().Get(Hud(app.GetWorld(),player));Check(app.GetRuntimeUI().Read(handle,"Health").text=="HP 75","repeated Play recreates fresh HUD");Check(app.OpenProject(files.other->descriptorPath,Application::ProjectDecision::Discard,&error),error);frame();Check(!app.GetRuntimeUI().Valid(handle)&&app.GetRuntimeUI().Instances().empty(),"Project Switch clears runtime HUD and handles");Check(gpu->GetValidationErrorCount()==0,"zero DX12 Validation Errors");app.Shutdown();files.Verify();
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{Files files;
#if defined(ENABLE_DX12)
 if(argc>1&&std::string(argv[1])=="--dx12")Native(files);else
#endif
 Cpu(files);std::cout<<"Gameplay UI Ownership: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"Gameplay UI Ownership failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
