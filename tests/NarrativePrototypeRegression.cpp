#define NOMINMAX
#include "EditorTestPreferences.h"
#include "ApplicationTestAccess.h"
#include "core/ProjectContext.h"
#include "ecs/NavGrid.h"
#include "ecs/PlayerSpawn.h"
#include "ecs/CameraState.h"
#include "ecs/components/UICanvasComponent.h"
#include "ecs/components/LuaScriptComponent.h"
#include "ecs/systems/PhysicsSystem.h"
#include "scene/SceneSerializer.h"
#include "resources/ResourceManager.h"
#include "scripting/Prefab.h"
#include "scripting/LuaRuntime.h"
#include "ui/RuntimeUI.h"
#include <fstream>
#include <iostream>
#include <limits>
#include "platform/GlfwWindow.h"
#include "render/RenderFactory.h"
#include "ecs/systems/RenderSystem.h"
#if defined(NARRATIVE_DX12)
#include "render/backends/dx12/Dx12RenderAdapter.h"
#endif
#if defined(NARRATIVE_VULKAN)
#include "render/backends/vulkan/VkRenderAdapter.h"
#endif
#include <GLFW/glfw3.h>
using namespace ecs;using J=nlohmann::json;namespace fs=std::filesystem;
static unsigned checks=0;
static void Check(bool v,const std::string& message){if(!v)throw std::runtime_error(message);++checks;}
template<class F>static void Reject(F f,const char* message){bool rejected=false;try{f();}catch(...){rejected=true;}Check(rejected,message);}
static Entity Tagged(const World& w,const std::string& name){for(auto e:w.Entities())if(auto* t=w.GetComponent<TagComponent>(e);t&&t->name==name)return e;throw std::runtime_error("Missing "+name);}
static std::shared_ptr<const ProjectContext> Open(const fs::path& path){auto p=ProjectManager::Prepare(path);ProjectManager::Activate(p);return p;}
static void DataChecks(const std::shared_ptr<const ProjectContext>& project){
 ResourceManager r(nullptr);auto a=r.Load<DataResource>("data/skills.table.json");Check(a&&a->IsLoaded(),a->GetErrorMessage());Check(a==r.Load<DataResource>("data/skills.table.json"),"ResourceManager caches typed data");
 auto d=*a->GetData().document;auto csv=DataLoader::ExportCSV(d);Check(DataLoader::ImportCSV(d,csv)==d,"CSV roundtrip typed records");d["rows"][0]["name"]="comma, quote\" and\nКириллица";Check(DataLoader::ImportCSV(d,DataLoader::ExportCSV(d))==d,"CSV quoted multiline UTF-8");
 auto invalid=d;invalid["rows"].push_back(invalid["rows"][0]);Reject([&]{DataLoader::Validate(invalid);},"duplicate ID rejected");invalid=d;invalid["rows"][0]["value"]="4";Reject([&]{DataLoader::Validate(invalid);},"wrong field type rejected");invalid=d;invalid["rows"][0]["value"]=100;Reject([&]{DataLoader::Validate(invalid);},"range rejected");Reject([&]{DataLoader::ImportCSV(d,"id,id\n1,2");},"duplicate CSV columns rejected");Reject([&]{DataLoader::ImportCSV(d,"\"unterminated");},"malformed CSV rejected");Reject([&]{DataLoader::SafePath("../escape.data.json");},"traversal rejected");
 for(auto& entry:fs::directory_iterator(project->ResolveAsset("data"))){auto key="data/"+entry.path().filename().string();auto resource=r.Load<DataResource>(key);Check(resource&&resource->IsLoaded(),key+": "+resource->GetErrorMessage());}

 auto empty=d;empty["rows"]=J::array();empty["schema"]["fields"]["value"]["type"]="NotAType";Reject([&]{DataLoader::Validate(empty);},"empty tables still validate schema types");
 DataLoader::ValidateValue({{"type","Vec3"}},J::array({1,2,3}));DataLoader::ValidateValue({{"type","Color"}},J::array({1,.5,0,1}));
 Reject([&]{DataLoader::ValidateValue({{"type","Boolean"}},1);},"boolean type enforced");Reject([&]{DataLoader::ValidateValue({{"type","Color"}},J::array({1,2,0,1}));},"color range enforced");Reject([&]{DataLoader::ValidateValue({{"type","Enum"},{"values",J::array({"a","b"})}},"c");},"enum membership enforced");
 Reject([&]{DataLoader::ValidateValue({{"type","Resource"}},"missing.data.json");},"missing resource diagnosed");
 auto probe=project->ResolveAsset("scripts/data_probe.lua");{std::ofstream f(probe);f<<R"(return {fields={},on_start=function(self)
 local a=Data.load('data/stats.data.json');assert(a.origin.x==0 and a.role=='detective' and a.active==true and #a.coat==4)
 a.name='runtime copy';assert(Data.load('data/stats.data.json').name~='runtime copy')
 local s=Data.find('data/skills.table.json','logic');s.value=99;assert(Data.find('data/skills.table.json','logic').value==4)
 assert(Data.find('data/skills.table.json','missing')==nil and #Data.rows('data/skills.table.json')==3)
 end})";}
 World probeWorld;auto owner=probeWorld.CreateEntity();probeWorld.AddComponent<TransformComponent>(owner);probeWorld.AddComponent<LuaScriptComponent>(owner).scriptPath="scripts/data_probe.lua";scripting::LuaRuntime runtime;scripting::GameplayServices services;services.postfxResources=&r;runtime.Start(probeWorld,{},services);runtime.Update(0);Check(runtime.Error("scripts/data_probe.lua").empty(),runtime.Error("scripts/data_probe.lua"));runtime.Stop();fs::remove(probe);
 auto key="data/regression.data.json";J fixture={{"version",1},{"kind","DataAsset"},{"type","Regression"},{"schema",{{"type","Object"},{"fields",{{"text",{{"type","String"}}}}}}},{"value",{{"text","first"}}}};std::string error;
 Check(DataLoader::Save(key,fixture,&error),error);auto snapshot=r.Load<DataResource>(key);auto original=snapshot->GetData().document;r.WatchForHotReload<DataResource>(key);fixture["value"]["text"]="second";Check(DataLoader::Save(key,fixture,&error),error);auto path=DataLoader::SafePath(key);fs::last_write_time(path,fs::last_write_time(path)+std::chrono::seconds(2));r.PollHotReload();Check(snapshot==r.Get<DataResource>(key)&&snapshot->GetData().document->at("value").at("text")=="second","hot reload preserves handle and replaces definition");Check(original->at("value").at("text")=="first","previous immutable snapshot survives reload");
 {std::ofstream f(path);f<<"{invalid";}r.Reload<DataResource>(key);Check(snapshot->GetData().document->at("value").at("text")=="second","invalid reload retains last good data");fs::remove(path);
}
struct Game {
 World world;SceneSettings settings;ResourceManager resources{nullptr};ui::RuntimeUI ui;scripting::LuaRuntime lua;scripting::GameplayCamera camera;PhysicsSystem physics;Entity player,view;bool click=false;Vec3 pointer{-1,-1,1.6f};
 Game(const std::shared_ptr<const ProjectContext>& p){std::string error;Check(SceneSerializer::LoadWorld(p->ResolveScene(p->startupScene),world,settings,&error),error);player=Tagged(world,"Detective");view=Tagged(world,"Camera");scripting::GameplayServices h;h.inputAllowed=[](){return true;};h.pressed=[&](const std::string& a){return click&&a=="HarborClick";};h.mousePosition=[&]{return pointer;};h.bindAction=[](auto&,auto&){return true;};h.postfxResources=&resources;h.runtimeUI=&ui;h.camera=&camera;h.setUIInputMode=[](auto&){};lua.Start(world,{},h);Tick(1);Good();}
 ~Game(){lua.Stop();}
 void Good(){Check(lua.Error("scripts/harbor.lua").empty(),lua.Error("scripts/harbor.lua"));}
 void Tick(int n){for(int i=0;i<n;++i){lua.Update(1.f/60);click=false;lua.FixedUpdate(1.f/60);ApplyCharacterMovement(world,player);physics.Update(world,1.f/60);}Good();}
 void Point(Vec3 p){auto s=ResolveCameraState(world,view,1.6f);auto& m=s.viewProjection;float x=m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],y=m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],w=m[3]*p.x+m[7]*p.y+m[11]*p.z+m[15];pointer={(x/w+1)*.5f,(1-y/w)*.5f,1.6f};Check(pointer.x>=0&&pointer.x<=1&&pointer.y>=0&&pointer.y<=1,"target visible in camera");click=true;Tick(1);}
 void Talk(const std::string& tag){Check(!ui.Read(ui.Get(player),"Dialogue").visible,"previous dialogue closed");Point(world.WorldTransform(Tagged(world,tag)).position);for(int i=0;i<1500&&!ui.Read(ui.Get(player),"Dialogue").visible;++i)Tick(1);Check(ui.Read(ui.Get(player),"Dialogue").visible,"approach and open "+tag);}
 void Button(const std::string& id){auto& d=ui.Instances().at(player).state;auto layout=ui::ComputeLayout(d,{0,0,1280,800});auto rect=layout.at(id);float x=rect.x+rect.w*.5f,y=rect.y+rect.h*.5f;Check(ui.Pointer(x,y,1280,800,true,true),"runtime UI pointer down "+id);ui.Pointer(x,y,1280,800,false,true);lua.DispatchUIEvents();Good();}
 void Choice(int n){Button("Choice"+std::to_string(n));}
};
static void NavigationChecks(){
 World w;auto obstacle=w.CreateEntity();w.AddComponent<TransformComponent>(obstacle).position={0,1,0};w.AddComponent<ColliderComponent>(obstacle).halfExtents={1,1,1};
 auto route=FindGridPath(w,{-3,0,0},{3,0,0},{-5,0,-5},{5,0,5},.25f,.3f);Check(!route.empty(),"A* finds detour");for(auto p:route)Check(std::abs(p.x)>=1.3f||std::abs(p.z)>=1.3f,"route clears obstacle and character radius");
 Check(FindGridPath(w,{-3,0,0},{0,0,0},{-5,0,-5},{5,0,5},.25f,.3f).empty(),"blocked goal fails");Check(FindGridPath(w,{-9,0,0},{3,0,0},{-5,0,-5},{5,0,5},.25f,.3f).empty(),"out of bounds fails");Reject([&]{FindGridPath(w,{},{},{-1000,0,-1000},{1000,0,1000},.1f,.3f);},"unbounded allocation rejected");
 auto s=BuildCameraState({},CameraComponent{},1.6f);auto [o,d]=CameraScreenRay(s,.5,.5);Check(std::abs(d.z-1)<.001f,"perspective centre ray");s.settings.projection=CameraProjection::Orthographic;auto a=CameraScreenRay(s,0,0),b=CameraScreenRay(s,1,1);Check(a.second.z==b.second.z&&a.first.x<b.first.x&&a.first.y>b.first.y,"orthographic parallel top-left rays");Reject([&]{CameraScreenRay(s,-1,0);},"invalid screen coordinate rejected");
}
static void Integration(const std::shared_ptr<const ProjectContext>& p){
 auto original=J::parse(std::ifstream(p->ResolveScene(p->startupScene)));World a,b;SceneSettings settings;std::string error;Check(SceneSerializer::RestoreWorld(original,a,settings,&error),error);auto captured=SceneSerializer::CaptureWorld(a,settings);auto save=p->ResolveScene("Scenes/RegressionRoundtrip.json");Check(SceneSerializer::SaveWorld(save,a,&error,&settings),error);Check(SceneSerializer::LoadWorld(save,b,settings,&error),error);Check(SceneSerializer::CaptureWorld(b,settings)==captured,"scene Save/Reload exact ECS roundtrip");fs::remove(save);
 auto prefab=scripting::ReadPrefab("prefabs/radio.prefab.json");Check(prefab.at("entities")[0].at("components").at("Tag").at("name")=="Radio","v3 inherited NPC resolved");World prefabs;auto npc=scripting::SpawnPrefab(prefabs,"prefabs/radio.prefab.json",{2,0,1});auto serialized=SceneSerializer::CaptureWorld(prefabs);World again;Check(SceneSerializer::RestoreWorld(serialized,again,settings,&error),error);Check(Tagged(again,"Radio").IsValid(),"inherited prefab scene serialization");
 for(bool success:{true,false}){
  // Deterministic skill outcomes without replacing the gameplay interpreter.
  ResourceManager resources(nullptr);auto r=resources.Load<DataResource>("data/skills.table.json");auto skills=*r->GetData().document;auto changed=skills;changed["rows"][0]["value"]=success?10:0;Check(DataLoader::Save("data/skills.table.json",changed,&error),error);
  struct Restore{J d;~Restore(){DataLoader::Save("data/skills.table.json",d);}} restore{skills};
  Game g(p);g.Button("JournalToggle");Check(g.ui.Read(g.ui.Get(g.player),"Journal").visible,"journal opens");g.Button("JournalToggle");g.Button("SkillsToggle");Check(g.ui.Read(g.ui.Get(g.player),"Skills").visible,"skills open");g.Button("SkillsToggle");
  g.Talk("Keeper");g.Choice(1);g.Choice(1);Check(!g.ui.Read(g.ui.Get(g.player),"Dialogue").visible,"dialogue safely ends");
  g.Talk("Receipt");g.Choice(1);
  if(success){g.Talk("Radio");g.Choice(1);Check(g.ui.Read(g.ui.Get(g.player),"Feedback").text.find("10")!=std::string::npos,"skill check feedback");g.Choice(1);}
  else{
   // Difficulty is 9: a zero skill can still pass on 2d6. Force failure by
   // authoring a valid higher difficulty in this isolated test project.
   auto path=DataLoader::SafePath("data/radio.data.json");auto graph=J::parse(std::ifstream(path));auto altered=graph;altered["value"]["nodes"][0]["choices"][0]["difficulty"]=99;Check(DataLoader::Save("data/radio.data.json",altered,&error),error);
   struct RestoreGraph{J d;~RestoreGraph(){DataLoader::Save("data/radio.data.json",d);}} graphRestore{graph};
   g.Talk("Radio");g.Choice(1);Check(g.ui.Read(g.ui.Get(g.player),"Speaker").text.size()>0,"failure node displayed");g.Choice(1);g.Talk("Cashier");g.Choice(1);g.Choice(1);g.Talk("Radio");g.Choice(1);g.Choice(1);
  }
  g.Talk("Keeper");g.Choice(2);g.Choice(success?1:2);g.Choice(1);Check(g.ui.Read(g.ui.Get(g.player),"Quest").text.find(success?"свет":"рапорт")!=std::string::npos,"detective ending journal");Check(!g.ui.Read(g.ui.Get(g.player),"Dialogue").visible,"ending closes UI");g.Tick(120);auto v=g.world.GetComponent<RigidbodyComponent>(g.player)->velocity;Check(std::hypot(v.x,v.z)<.01f,"character stops at interaction");
 }
 Game fresh(p);auto freshQuest=fresh.ui.Read(fresh.ui.Get(fresh.player),"Quest").text;std::replace(freshQuest.begin(),freshQuest.end(),char(10),char(32));Check(freshQuest=="Найдите фонарь, выслушайте свидетелей и решите судьбу рапорта.","fresh Play resets state");
}
static void Native(const std::shared_ptr<const ProjectContext>& p){
#if defined(NARRATIVE_DX12)
 Application app;Check(app.Initialize(Application::StartupMode::ProjectBrowser),"DX12 project manager startup");glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());std::string error;Check(app.OpenProject(p->descriptorPath,Application::ProjectDecision::Discard,&error),error);auto& editor=app.GetEditorLayer();editor.SetDiagnosticViewport(1280,800);editor.SetGameView(true);ApplicationTestAccess::FrameDelta(app,1.f/60);auto frame=[&](int n=8){app.SetFrameLimit(n);Check(app.Run()==0,"native frames");};frame();editor.SelectAsset("data/stats.data.json","DataAsset");frame();Check(!editor.HasProjectEdits(),"opening DataAsset Inspector leaves no unsaved edits");editor.SelectAsset("data/skills.table.json","DataTable");frame();Check(!editor.HasProjectEdits(),"opening DataTable editor leaves no unsaved edits");editor.SelectAsset("","");auto authored=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());
 app.SetEditorPlayMode(true);frame(20);Check(app.GetLuaRuntime().Error("scripts/harbor.lua").empty(),app.GetLuaRuntime().Error("scripts/harbor.lua"));Check(app.GetRenderCameraState().settings.projection==CameraProjection::Orthographic,"native isometric orthographic camera");auto player=Tagged(app.GetWorld(),"Detective");auto handle=app.GetRuntimeUI().Get(player);Check(app.GetRuntimeUI().Valid(handle),"native HUD live");auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(gpu,"DX12 adapter");auto pixels=gpu->ReadBackViewportPixels();Check(pixels.size()==1280*800*4,"actual prototype frame readback");std::ofstream ppm("narrative-prototype.ppm",std::ios::binary);ppm<<"P6\n1280 800\n255\n";for(std::size_t i=0;i<pixels.size();i+=4)ppm.write(reinterpret_cast<const char*>(pixels.data()+i),3);ppm.close();

 // Exercise the real Application scheduler and physics using deterministic host
 // input, so desktop focus and pointer position cannot make the test flaky.
 bool clicked=false;Vec3 mouse{-1,-1,1.6f};auto host=ApplicationTestAccess::GameplayHost(app);
 host.inputAllowed=[](){return true;};host.mousePosition=[&]{return mouse;};host.pressed=[&](const std::string& action){return clicked&&action=="HarborClick";};
 app.GetLuaRuntime().Start(app.GetWorld(),{},host);frame(1);handle=app.GetRuntimeUI().Get(player);
 auto view=Tagged(app.GetWorld(),"Camera"),keeper=Tagged(app.GetWorld(),"Keeper");auto state=ResolveCameraState(app.GetWorld(),view,1.6f);auto q=app.GetWorld().WorldTransform(keeper).position;auto& m=state.viewProjection;
 float sx=m[0]*q.x+m[4]*q.y+m[8]*q.z+m[12],sy=m[1]*q.x+m[5]*q.y+m[9]*q.z+m[13];mouse={(sx+1)*.5f,(1-sy)*.5f,1.6f};clicked=true;frame(1);clicked=false;
 for(int i=0;i<300&&!app.GetRuntimeUI().Read(handle,"Dialogue").visible;++i)frame(4);
 Check(app.GetLuaRuntime().Error("scripts/harbor.lua").empty(),app.GetLuaRuntime().Error("scripts/harbor.lua"));auto finalPosition=app.GetWorld().WorldTransform(player).position;std::cout<<"Native actor position: "<<finalPosition.x<<","<<finalPosition.y<<","<<finalPosition.z<<"; status: "<<app.GetRuntimeUI().Read(handle,"Status").text<<"; pointer: "<<mouse.x<<","<<mouse.y<<"; paused: "<<app.IsEditorPaused()<<"\n";Check(app.GetRuntimeUI().Read(handle,"Dialogue").visible,"native click-to-move approaches NPC and opens dialogue");
 Check(app.GetRuntimeUI().Read(handle,"Line").text.find(char(10))!=std::string::npos,"native dialogue wraps full UTF-8 text");frame(4);auto dialoguePixels=gpu->ReadBackViewportPixels();Check(dialoguePixels!=pixels,"dialogue and portrait change rendered pixels");std::ofstream portrait("narrative-dialogue.ppm",std::ios::binary);portrait<<"P6\n1280 800\n255\n";for(std::size_t i=0;i<dialoguePixels.size();i+=4)portrait.write(reinterpret_cast<const char*>(dialoguePixels.data()+i),3);portrait.close();
 auto response=[&](int n){auto& doc=app.GetRuntimeUI().Instances().at(player).state;auto rect=ui::ComputeLayout(doc,{0,0,1280,800}).at("Choice"+std::to_string(n));float x=rect.x+rect.w*.5f,y=rect.y+rect.h*.5f;Check(app.GetRuntimeUI().Pointer(x,y,1280,800,true,true),"native choice pointer");app.GetRuntimeUI().Pointer(x,y,1280,800,false,true);app.GetLuaRuntime().DispatchUIEvents();};
 response(1);Check(app.GetRuntimeUI().Read(handle,"Line").text.find("радиостанцию")!=std::string::npos,"native response advances data graph");response(1);Check(!app.GetRuntimeUI().Read(handle,"Dialogue").visible,"native reply closes conversation");
 app.SetEditorPlayMode(false);frame();Check(!app.GetRuntimeUI().Valid(handle),"Stop invalidates Canvas");Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==authored,"Stop restores authoring scene");app.SetEditorPlayMode(true);frame(10);Check(app.GetLuaRuntime().Error("scripts/harbor.lua").empty(),app.GetLuaRuntime().Error("scripts/harbor.lua"));app.SetEditorPlayMode(false);Check(app.OpenProject(p->descriptorPath,Application::ProjectDecision::Discard,&error),error);frame();Check(gpu->GetValidationErrorCount()==0,"zero DX12 validation errors");app.Shutdown();
#else
 throw std::runtime_error("DX12 is disabled in this build");
#endif
}
static void Vulkan(const std::shared_ptr<const ProjectContext>& p){
 GlfwWindow window;Check(window.Create(640,400,"Narrative prototype Vulkan"),"Vulkan window");glfwHideWindow(window.GetGlfwHandle());auto adapter=RenderFactory::Create(RenderBackend::Vulkan);Check(adapter&&adapter->Initialize(&window),"Vulkan backend initialization");Game g(p);RenderSystem render;render.SetRenderAdapter(adapter.get());render.SetResourceManager(&g.resources);render.SetCameraState(ResolveCameraState(g.world,g.view,1.6f));for(int i=0;i<12;++i){window.PollEvents();adapter->BeginFrame();adapter->Clear(.05,.1,.12,1);render.Update(g.world,1.f/60);adapter->EndFrame();adapter->Present();}Check(render.RuntimeUI().Instances().size()==1,"Vulkan project HUD built");
#if defined(NARRATIVE_VULKAN)
auto* vk=dynamic_cast<VkRenderAdapter*>(adapter.get());Check(vk&&vk->ValidationEnabled(),"Vulkan validation layer enabled");Check(vk->GetValidationErrorCount()==0,"zero Vulkan validation errors");
#endif
render.ReleaseGpuResources();render.SetRenderAdapter(nullptr);adapter->Shutdown();
}
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{
 if(argc>2&&std::string(argv[1])=="--installed"){auto previous=ProjectManager::Active();auto installed=Open(fs::path(argv[2]));Native(installed);ProjectManager::Activate(previous);std::cout<<"Installed project: "<<checks<<" checks passed\n";return 0;}
 fs::path source=fs::u8path(NARRATIVE_SOURCE);fs::path scratch=fs::current_path()/"narrative-integration-owned";Check(!fs::exists(scratch),"isolated test directory must be fresh");fs::copy(source,scratch,fs::copy_options::recursive);struct Cleanup{fs::path p;~Cleanup(){std::error_code ec;fs::remove_all(p,ec);}} cleanup{scratch};auto previous=ProjectManager::Active();auto p=Open(scratch/"DiscoElysium2.whispproject");
 if(argc>1&&std::string(argv[1])=="--dx12")Native(p);else if(argc>1&&std::string(argv[1])=="--vulkan")Vulkan(p);else{DataChecks(p);NavigationChecks();Integration(p);}ProjectManager::Activate(previous);std::cout<<"Narrative prototype: "<<checks<<" checks passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<"Narrative prototype failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}

