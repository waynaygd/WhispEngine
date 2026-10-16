#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "ApplicationTestAccess.h"
#include "scripting/LuaRuntime.h"
#include "scripting/Prefab.h"
#include "scripting/EntityReference.h"
#include "ecs/ComponentRegistry.h"
#include "ecs/components/LuaScriptComponent.h"
#include "ecs/components/LightComponent.h"
#include "scene/SceneSerializer.h"
#include <fstream>
#include <iostream>
#if defined(ENABLE_DX12)
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <imgui_internal.h>
#endif
using namespace ecs;
using Json=nlohmann::json;
static unsigned checks=0;
static void Check(bool value,const char* text){if(!value)throw std::runtime_error(text);++checks;}
static void Near(float a,float b,const char* text){Check(std::abs(a-b)<.0001f,text);}
struct Fixtures {
    std::string key="scripts/api-"+NewSceneIdentity()+"/test.lua";
    std::filesystem::path path=scripting::SafeAsset(key,".lua");
    Fixtures(){Check(std::filesystem::create_directory(path.parent_path()),"owned fixture directory");}
    ~Fixtures(){std::error_code ec;std::filesystem::remove_all(path.parent_path(),ec);}
    void Write(const std::string& source){std::ofstream out(path);out<<source;Check(out.good(),"fixture write");}
};
// A fixture actor delivers the actual gameplay event through World.send_event;
// moving only the runtime sender is the test's explicit start signal.
static const char* concertSignal=R"(return {
 fields={concert={type='entity'}},
 on_update=function(self)
   if not self.sent and World.get_position(self.entity).x>0 then
     World.send_event(self.concert,'start_concert',0); self.sent=true
   end
 end})";
static std::string ConcertSenderScript(const Fixtures& files){
    std::ofstream out(files.path.parent_path()/"concert-signal.lua");out<<concertSignal;
    Check(out.good(),"write independent Concert event fixture");
    return files.key.substr(0,files.key.find_last_of('/')+1)+"concert-signal.lua";
}
static void StartConcert(scripting::LuaRuntime& lua,World& world,Entity sender){
    world.GetComponent<TransformComponent>(sender)->position.x=1;
    lua.Update(0); // enqueue using the gameplay API; delivery is on the next Update
    lua.Update(.1f);
}
static Entity Object(World& w){auto e=w.CreateEntity();w.AddComponent<TransformComponent>(e);w.AddComponent<SceneIdentityComponent>(e);return e;}
static Json Ref(World& w,Entity e){return {{"$entity",w.GetComponent<SceneIdentityComponent>(e)->value}};}
static Entity Actor(World& w,const std::string& path){auto e=Object(w);w.AddComponent<LuaScriptComponent>(e).scriptPath=path;return e;}
static void Cpu(){
    Fixtures files;Application app;app.NewScene();auto& w=app.GetWorld();auto& ui=app.GetEditorLayer();
    auto globe=ui.CreateEntity(app,"Globe"),light=ui.CreateLight(app,LightType::Spot),actor=ui.CreateEntity(app,"Concert");
    Check(ui.AddComponent(app,actor,ComponentId("LuaScript")),"add existing Lua component");
    Check(ui.SetProperty(app,actor,ComponentId("LuaScript"),"scriptPath",std::string("scripts/ConcertController.lua")),"assign demo");
    Check(ui.SetLuaEntityReference(app,actor,"globe",globe)&&ui.SetLuaEntityReference(app,actor,"spotlight",light),"Inspector assignment command");
    auto& s=*w.GetComponent<LuaScriptComponent>(actor);auto saved=s.properties;
    Check(ui.SetLuaEntityReference(app,actor,"globe"),"clear reference");Check(s.properties["globe"].is_null(),"clear means null");ui.UndoEdit(app);Check(s.properties==saved,"reference Undo");ui.RedoEdit(app);Check(s.properties["globe"].is_null(),"reference Redo");ui.UndoEdit(app);
    World foreign;Check(!ui.SetLuaEntityReference(app,actor,"globe",Object(foreign)),"foreign assignment rejected");
    Check(!ui.SetLuaEntityReference(app,actor,"rotation_speed",globe),"typed assignment validation");
    auto legacy=w.CreateEntity();Check(ui.SetLuaEntityReference(app,actor,"globe",legacy),"legacy target receives persistent identity");auto legacyId=w.GetComponent<SceneIdentityComponent>(legacy)->value;ui.UndoEdit(app);Check(!w.HasComponent<SceneIdentityComponent>(legacy)&&s.properties==saved,"assignment Undo includes identity metadata");ui.RedoEdit(app);Check(w.GetComponent<SceneIdentityComponent>(legacy)->value==legacyId,"Redo restores exact stable identity");ui.UndoEdit(app);w.DestroyEntity(legacy);
    auto sender=Actor(w,ConcertSenderScript(files));w.GetComponent<LuaScriptComponent>(sender)->properties["concert"]=Ref(w,actor);
    const auto senderRef=Ref(w,sender);const auto lightBefore=w.GetComponent<SpotLightComponent>(light)->intensity;
    auto scene=SceneSerializer::CaptureWorld(w,app.GetSceneSettings());World loaded;SceneSettings settings;std::string error;
    Check(SceneSerializer::RestoreWorld(scene,loaded,settings,&error),"scene roundtrip with tagged references");Check(SceneSerializer::CaptureWorld(loaded,settings)==scene,"stable identities roundtrip");
    auto sceneFile=files.path.parent_path()/"roundtrip.scene.json";Check(SceneSerializer::WriteAtomic(sceneFile,scene,&error)&&SceneSerializer::LoadWorld(sceneFile,loaded,settings,&error),"entity references disk Save/Load");Check(SceneSerializer::CaptureWorld(loaded,settings)==scene,"disk references retain exact identities");
    World::CloneMap map;auto runtime=w.CloneData(&map);scripting::LuaRuntime lua;lua.Start(*runtime);lua.Update(.1f);Check(lua.Error(s.scriptPath).empty(),"Concert starts");
    Near(runtime->GetComponent<TransformComponent>(map.at(globe))->rotation.y,0,"Concert waits for start_concert");Near(runtime->GetComponent<SpotLightComponent>(map.at(light))->intensity,lightBefore,"light unchanged before event");
    StartConcert(lua,*runtime,map.at(sender));
    Near(runtime->GetComponent<TransformComponent>(map.at(globe))->rotation.y,.05f,"Concert rotates runtime referenced globe");Near(runtime->GetComponent<SpotLightComponent>(map.at(light))->intensity,100+std::sin(.2f)*80,"Concert event starts specified light pulse");Near(w.GetComponent<TransformComponent>(globe)->rotation.y,0,"Editor world isolated");Near(w.GetComponent<SpotLightComponent>(light)->intensity,lightBefore,"Editor light isolated");
    Check(scripting::ResolveEntityReference(*runtime,s.properties["globe"])==map.at(globe),"CloneData reference remaps by identity");lua.Stop();
    runtime=w.CloneData(&map);lua.Start(*runtime);lua.Update(.1f);Near(runtime->GetComponent<TransformComponent>(map.at(globe))->rotation.y,0,"Stop/repeated Play starts inactive");StartConcert(lua,*runtime,map.at(sender));Near(runtime->GetComponent<TransformComponent>(map.at(globe))->rotation.y,.05f,"repeated Play uses fresh instance and remapped references");lua.Stop();
    lua.Start(loaded);lua.Update(.1f);Check(lua.Error(s.scriptPath).empty(),"loaded scene executes references");auto loadedGlobe=scripting::ResolveEntityReference(loaded,Ref(w,globe));Near(loaded.GetComponent<TransformComponent>(loadedGlobe)->rotation.y,0,"loaded Concert waits for event");StartConcert(lua,loaded,scripting::ResolveEntityReference(loaded,senderRef));Near(loaded.GetComponent<TransformComponent>(loadedGlobe)->rotation.y,.05f,"loaded reference receives start event");lua.Stop();
    Check(ui.DeleteEntity(app,globe),"delete target");Check(!scripting::ResolveEntityReference(w,s.properties["globe"]).IsValid(),"deleted target unresolved");ui.UndoEdit(app);Check(scripting::ResolveEntityReference(w,s.properties["globe"]).IsValid(),"Undo delete recovers persistent reference");
    // Generation reuse cannot resurrect a handle that Lua already received.
    files.Write(R"(return {fields={target={type='entity'}},on_update=function(self)
        if self.target and World.is_alive(self.target) then World.set_position(self.entity,Vec3.new(1,0,0))
        else World.set_position(self.entity,Vec3.new(99,0,0)) end end})");
    World life;auto target=Object(life),owner=Actor(life,files.key);auto targetRef=Ref(life,target);life.GetComponent<LuaScriptComponent>(owner)->properties["target"]=targetRef;lua.Start(life);lua.Update(.01f);Near(life.GetComponent<TransformComponent>(owner)->position.x,1,"reference initially alive");
    life.DestroyEntity(target);auto reuse=Object(life);Check(reuse.index==target.index&&reuse.generation!=target.generation,"fixture reuses slot");lua.Update(.01f);Near(life.GetComponent<TransformComponent>(owner)->position.x,99,"stale handle remains dead");
    Check(lua.ReloadScript(files.key),"reload missing reference");lua.Update(.01f);Near(life.GetComponent<TransformComponent>(owner)->position.x,99,"missing reference resolves nil");
    files.Write("return {fields={target={type='entity',default=123}}}");Check(!lua.ReloadScript(files.key),"bad entity schema rejected atomically");lua.Update(.01f);Near(life.GetComponent<TransformComponent>(owner)->position.x,99,"old program survives bad reload");lua.Stop();
    // Registry validation matches the actual Transform/Light Inspector limits.
    for(auto command:{"World.set_rotation(self.entity,Vec3.new(0,1,2))","World.set_scale(self.entity,Vec3.new(2,3,4))"}){
        files.Write(std::string("return {on_start=function(self) ")+command+" end}");World test;auto e=Actor(test,files.key);lua.Start(test);lua.Update(.01f);Check(lua.Error(files.key).empty(),"valid Transform setter");auto* t=test.GetComponent<TransformComponent>(e);if(t->rotation.y==1)Near(t->rotation.z,2,"rotation radians");else Near(t->scale.z,4,"scale changed");lua.Stop();
    }
    for(auto type:{LightType::Directional,LightType::Point,LightType::Spot}){
        files.Write("return {on_start=function(self) Light.set_intensity(self.entity,7); assert(Light.get_intensity(self.entity)==7) end}");World test;auto e=Actor(test,files.key);AddSceneLight(test,e,{type});lua.Start(test);lua.Update(.01f);Check(lua.Error(files.key).empty(),"all three Light types get/set");Near(GetLightParameters(test,e)->intensity,7,"actual component intensity");lua.Stop();
    }
    for(const char* command:{"World.set_rotation(self.entity,Vec3.new(0/0,0,0))","World.set_scale(self.entity,Vec3.new(math.huge,1,1))","World.set_scale(self.entity,Vec3.new(1000001,1,1))","Light.set_intensity(self.entity,-1)","Light.set_intensity(self.entity,0/0)","Light.set_intensity(self.entity,math.huge)","Light.set_intensity(self.entity,100001)","Light.get_intensity(self.entity)"}){
        files.Write(std::string("return {on_start=function(self) ")+command+" end}");World test;auto e=Actor(test,files.key);if(std::string(command)!="Light.get_intensity(self.entity)")test.AddComponent<PointLightComponent>(e);lua.Start(test);lua.Update(.01f);Check(!lua.Error(files.key).empty(),"invalid setter/missing component isolated");Near(test.GetComponent<TransformComponent>(e)->scale.x,1,"invalid edit atomic");lua.Stop();
    }
    for(Json bad:{Json(23),Json({{"$entity",3}}),Json({{"$entity",""}}),Json({{"$entity","ok"},{"index",3}})}){
        auto document=scene;for(auto& item:document["entities"])if(item["components"].contains("LuaScript"))item["components"]["LuaScript"]["properties"]["globe"]=bad;
        if(bad.is_number()){Check(SceneSerializer::RestoreWorld(document,loaded,settings,&error),"legacy numeric properties still load");lua.Start(loaded);lua.Update(.01f);Check(!lua.Error("scripts/ConcertController.lua").empty(),"numeric entity override rejected by schema");lua.Stop();}
        else Check(!SceneSerializer::RestoreWorld(document,loaded,settings,&error),"malformed reference JSON rejected");
    }
    files.Write("return {fields={target={type='entity'}},on_start=function(self) assert(self.target==nil) end}");World empty;Actor(empty,files.key);lua.Start(empty);lua.Update(.01f);Check(lua.Error(files.key).empty(),"unassigned entity defaults nil");lua.Stop();
    files.Write("return {fields={target={type='entity'}},on_update=function(self) World.set_rotation(self.target,Vec3.new(0,2,0)) end}");World stale;auto st=Object(stale),so=Actor(stale,files.key);stale.GetComponent<LuaScriptComponent>(so)->properties["target"]=Ref(stale,st);lua.Start(stale);lua.Update(.01f);stale.DestroyEntity(st);auto replacement=Object(stale);lua.Update(.01f);Check(lua.Error(files.key).find("Dead/stale/foreign")!=std::string::npos,"stale Transform setter rejected without crash");Near(stale.GetComponent<TransformComponent>(replacement)->rotation.y,0,"stale setter cannot touch replacement");lua.Stop();
    files.Write(R"(return {fields={target={type='entity'},asset={type='asset',default='prefabs/wave_enemy.prefab.json'},speed={type='number',default=1},count={type='integer',default=2},flag={type='bool',default=true},text={type='string',default='hello'},offset={type='vec3',default={1,2,3}}},on_start=function(self)
        assert(self.flag and self.count==2 and self.text=='hello' and self.asset=='prefabs/wave_enemy.prefab.json' and self.offset.z==3)
        World.set_rotation(self.target,Vec3.new(0,self.speed,0)) end})");
    World reload;auto rt=Object(reload),ro=Actor(reload,files.key);reload.GetComponent<LuaScriptComponent>(ro)->properties["target"]=Ref(reload,rt);lua.Start(reload);lua.Update(.01f);Check(lua.Error(files.key).empty(),"entity coexists with all legacy field types");Near(reload.GetComponent<TransformComponent>(rt)->rotation.y,1,"first program default applied");
    files.Write("return {fields={target={type='entity'},speed={type='number',default=4}},on_start=function(self) World.set_rotation(self.target,Vec3.new(0,self.speed,0)) end}");Check(lua.ReloadScript(files.key),"successful behaviour hot reload");lua.Update(.01f);Near(reload.GetComponent<TransformComponent>(rt)->rotation.y,4,"reload preserves reference and uses changed default");lua.Stop();
    auto prefabKey=files.key.substr(0,files.key.size()-8)+"entity.prefab.json";
    {std::ofstream prefab(scripting::SafeAsset(prefabKey,".prefab.json"));prefab<<Json({{"version",1},{"components",ComponentRegistry::Builtins().Capture(reload,ro)}});}
    auto spawned=scripting::SpawnPrefab(reload,prefabKey,{2,0,0});Check(spawned!=ro&&reload.GetComponent<SceneIdentityComponent>(spawned)->value!=reload.GetComponent<SceneIdentityComponent>(ro)->value,"prefab fresh identity with reference properties");lua.Start(reload);lua.Update(.01f);Check(lua.Error(files.key).empty(),"prefab scene-local references resolve safely");lua.Stop();
}
#if defined(ENABLE_DX12)
static void Native(float frameDelta){
    Fixtures files;files.Write("return {fields={target={type='entity',label='Target'}}}");
    Application app;Check(app.Initialize(),"DX12 initialize");ApplicationTestAccess::FrameDelta(app,frameDelta);SyntheticEditorInput input;auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle();glfwHideWindow(window);app.NewScene();auto& w=app.GetWorld();auto& ui=app.GetEditorLayer();ui.ResetLayout();
    auto globe=ui.CreatePrimitive(app,"cube"),light=ui.CreateLight(app,LightType::Spot),actor=ui.CreateEntity(app,"Controller");ui.AddComponent(app,actor,ComponentId("LuaScript"));w.RemoveComponent<TransformComponent>(actor);w.RemoveComponent<TagComponent>(actor);auto& script=*w.GetComponent<LuaScriptComponent>(actor);script.scriptPath=files.key;
    auto frame=[&](int n=3){app.SetFrameLimit(n);Check(app.Run()==0,"native frames");};frame(80);
    auto click=[&](float x,float y){SyntheticEditorInput::Position(x,y);frame();SyntheticEditorInput::Button(0,true);frame(1);SyntheticEditorInput::Button(0,false);frame();};
    auto combo=[&]{auto* p=ImGui::FindWindowByName("Inspector");return ImVec2(p->DC.CursorStartPos.x+25,p->DC.CursorPosPrevLine.y+p->DC.PrevLineSize.y*.5f);};
    auto* inspector=ImGui::FindWindowByName("Inspector");ImGui::SetScrollY(inspector,inspector->ScrollMax.y);frame();
    auto p=combo();
    click(p.x,p.y);auto& popups=ImGui::GetCurrentContext()->OpenPopupStack;Check(!popups.empty(),"Inspector entity dropdown opens");auto* menu=popups.back().Window;
    auto options=w.Entities();auto option=1+std::distance(options.begin(),std::find(options.begin(),options.end(),globe));
    click(menu->DC.CursorStartPos.x+20,menu->DC.CursorStartPos.y+option*ImGui::GetTextLineHeightWithSpacing()+ImGui::GetTextLineHeight()*.5f);Check(scripting::ResolveEntityReference(w,script.properties["target"])==globe,"Inspector dropdown selects named scene entity");
    auto* panel=ImGui::FindWindowByName("Inspector");click(panel->DC.CursorPosPrevLine.x-8,panel->DC.CursorPosPrevLine.y+panel->DC.PrevLineSize.y*.5f);Check(script.properties["target"].is_null(),"Inspector X clears reference");
    ui.UndoEdit(app);Check(scripting::ResolveEntityReference(w,script.properties["target"])==globe,"Inspector assignment Undo");ui.RedoEdit(app);frame();
    auto* hierarchy=ImGui::FindWindowByName("Scene Hierarchy");auto rows=w.Entities();auto row=std::find(rows.begin(),rows.end(),globe)-rows.begin();float hx=hierarchy->WorkRect.Min.x+40,hy=hierarchy->WorkRect.Min.y+ImGui::GetFrameHeightWithSpacing()+ImGui::GetTextLineHeight()*.5f+row*ImGui::GetTextLineHeightWithSpacing();
    p=combo();SyntheticEditorInput::Position(hx,hy);frame();Check(ImGui::GetCurrentContext()->HoveredWindow==hierarchy,"drag origin hits visible Hierarchy");SyntheticEditorInput::Button(0,true);frame(1);SyntheticEditorInput::Position(hx+12,hy+5);frame();if(!ImGui::GetCurrentContext()->DragDropActive){auto& g=*ImGui::GetCurrentContext();auto& io=ImGui::GetIO();std::cerr<<"DRAG expected="<<hx<<","<<hy<<" mouse="<<io.MousePos.x<<","<<io.MousePos.y<<" down="<<io.MouseDown[0]<<" clicked="<<io.MouseClickedPos[0].x<<","<<io.MouseClickedPos[0].y<<" dist="<<io.MouseDragMaxDistanceSqr[0]<<" threshold="<<io.MouseDragThreshold<<" hovered="<<(g.HoveredWindow?g.HoveredWindow->Name:"null")<<" activeWindow="<<(g.ActiveIdWindow?g.ActiveIdWindow->Name:"null")<<" hoveredId="<<g.HoveredId<<" activeId="<<g.ActiveId<<" hierarchyPos="<<hierarchy->Pos.x<<","<<hierarchy->Pos.y<<" work="<<hierarchy->WorkRect.Min.x<<","<<hierarchy->WorkRect.Min.y<<" scroll="<<hierarchy->Scroll.y<<" row="<<row<<" font="<<ImGui::GetFontSize()<<" frame="<<ImGui::GetFrameHeightWithSpacing()<<" line="<<ImGui::GetTextLineHeightWithSpacing()<<'\n';}Check(ImGui::GetCurrentContext()->DragDropActive,"Hierarchy drag starts");SyntheticEditorInput::Position(p.x,p.y);frame();SyntheticEditorInput::Button(0,false);frame();Check(scripting::ResolveEntityReference(w,script.properties["target"])==globe,"Hierarchy payload assigns Inspector reference");
    // Use existing rendering and Runtime World; hold Lua clock deterministically
    // through direct runtime updates while the editor is paused for readback.
    w.AddComponent<TransformComponent>(actor);script.scriptPath="scripts/ConcertController.lua";script.properties=Json::object();ui.SetLuaEntityReference(app,actor,"globe",globe);ui.SetLuaEntityReference(app,actor,"spotlight",light);
    w.GetComponent<TransformComponent>(globe)->position={0,1,5};w.GetComponent<TransformComponent>(globe)->scale={2,1,1};w.GetComponent<TransformComponent>(light)->position={0,3,-1};w.GetComponent<TransformComponent>(light)->rotation={.2f,0,0};auto& spot=*w.GetComponent<SpotLightComponent>(light);spot.range=30;spot.innerAngle=30;spot.outerAngle=60;
    auto sender=Actor(w,ConcertSenderScript(files));w.GetComponent<LuaScriptComponent>(sender)->properties["concert"]=Ref(w,actor);const auto lightBefore=spot.intensity;
    auto settings=app.GetSceneSettings();settings.environment.enabled=false;settings.antiAliasing=AntiAliasingMode::FxaaHigh;settings.lighting.enabled=true;settings.lighting.ambientIntensity=.01f;settings.shadows.enabled=false;Check(app.SetSceneSettings(settings),"render fixture settings");app.SetViewportUnlit(false);ui.SetDiagnosticViewport(400,300);ui.SetGameView(true);ui.ClearSelection();app.SetEditorCameraPose({0,1,-6},0,0);frame();auto authored=SceneSerializer::CaptureWorld(w,app.GetSceneSettings());
    app.SetEditorPlayMode(true);app.SetEditorPaused(true);auto& runtime=app.GetWorld();auto rg=scripting::ResolveEntityReference(runtime,Ref(w,globe)),rl=scripting::ResolveEntityReference(runtime,Ref(w,light));Check(runtime.IsAlive(rl)&&runtime.HasComponent<SpotLightComponent>(rl),"runtime spot identity resolves");auto& lua=app.GetLuaRuntime();lua.Update(.1f);Check(lua.Error(script.scriptPath).empty(),"Concert native starts in Runtime World");Near(runtime.GetComponent<TransformComponent>(rg)->rotation.y,0,"native Concert waits for start_concert");Near(runtime.GetComponent<SpotLightComponent>(rl)->intensity,lightBefore,"native light idle before event");StartConcert(lua,runtime,scripting::ResolveEntityReference(runtime,Ref(w,sender)));Near(runtime.GetComponent<TransformComponent>(rg)->rotation.y,.05f,"native Concert rotation");Near(runtime.GetComponent<SpotLightComponent>(rl)->intensity,100+std::sin(.2f)*80,"native event starts light pulse");
    auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());frame(12);auto beforeTransform=gpu->ReadBackHdrPixels();
    files.Write("return {fields={target={type='entity'}},on_start=function(self) World.set_rotation(self.target,Vec3.new(0,1.57,0));World.set_scale(self.target,Vec3.new(3,1,1)) end}");auto ra=scripting::ResolveEntityReference(runtime,Ref(w,actor));auto* rs=runtime.GetComponent<LuaScriptComponent>(ra);Check(rs!=nullptr,"runtime controller still exists");rs->scriptPath=files.key;rs->properties={{"target",Ref(w,globe)}};Check(lua.ReloadScript(files.key),"reload Transform fixture");lua.Update(.01f);Check(lua.Error(files.key).empty(),"native Transform fixture callbacks healthy");frame(12);auto bright=gpu->ReadBackHdrPixels();Check(beforeTransform.size()==bright.size()&&!bright.empty(),"Transform render readback valid");double geometryDifference=0;for(std::size_t i=0;i<bright.size();i+=4)geometryDifference+=std::abs(beforeTransform[i]-bright[i]);Check(geometryDifference>1,"Lua rotation and scale update rendered mesh");
    files.Write("return {fields={target={type='entity'}},on_start=function(self) Light.set_intensity(self.target,0) end}");rs->properties={{"target",Ref(w,light)}};Check(lua.ReloadScript(files.key),"reload lighting fixture");lua.Update(.01f);Check(lua.Error(files.key).empty(),"native Light fixture callbacks healthy");frame(12);auto dark=gpu->ReadBackHdrPixels();Check(bright.size()==400*300*4&&dark.size()==bright.size(),"HDR fixture readbacks match target dimensions");double difference=0;for(std::size_t i=0;i<bright.size();i+=4)difference+=std::max(0.f,bright[i]-dark[i]);Check(difference>1,"Lua intensity changes actual DX12 HDR rendering");
    app.SetEditorPlayMode(false);frame();Check(SceneSerializer::CaptureWorld(w,app.GetSceneSettings())==authored,"Stop preserves Editor World and references");app.SetEditorPlayMode(true);app.SetEditorPaused(true);auto& again=app.GetWorld();auto againGlobe=scripting::ResolveEntityReference(again,Ref(w,globe));lua.Update(.1f);Near(again.GetComponent<TransformComponent>(againGlobe)->rotation.y,0,"native repeated Play resets Concert activation");StartConcert(lua,again,scripting::ResolveEntityReference(again,Ref(w,sender)));Near(again.GetComponent<TransformComponent>(againGlobe)->rotation.y,.05f,"native repeated Play restores reference and event delivery");app.SetEditorPlayMode(false);Check(SceneSerializer::CaptureWorld(w,app.GetSceneSettings())==authored,"repeated Stop preserves authoring scene");Check(gpu->GetValidationErrorCount()==0,"DX12 validation zero");
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{
#if defined(ENABLE_DX12)
    if(argc>1&&std::string(argv[1])=="--dx12"){for(float fps:{30.f,60.f,144.f})Native(1.f/fps);}else
#endif
    Cpu();std::cout<<"Lua Gameplay API: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
