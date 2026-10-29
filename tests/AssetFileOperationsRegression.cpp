#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "editor/AssetCatalog.h"
#include "editor/AssetImport.h"
#include "editor/ScriptEditor.h"
#include "ecs/components/LuaScriptComponent.h"
#include "ecs/components/UICanvasComponent.h"
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "resources/ResourceManager.h"
#include "scripting/Prefab.h"
#include "scene/SceneSerializer.h"
#include <thread>
#include <d3dcompiler.h>
#include <imgui_internal.h>
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>
#include <chrono>
#ifdef _WIN32
#include <Windows.h>
#endif
namespace fs=std::filesystem;
using editor::AssetCatalog;
namespace editor {
struct EditorDocumentTestAccess {
 static void Import(EditorLayer& e,const std::string& source,const std::string& folder){e.m_ImportSource=source;e.m_ImportTargetFolder=folder;e.m_ImportRoot.clear();e.m_ImportPreview.clear();e.m_ImportPopup=true;e.m_EditorError.clear();}
 static void List(EditorLayer& e,bool tree=false){e.m_AssetGrid=false;e.m_AssetTree=tree;e.m_ProjectAssets=false;e.m_AssetCategory=e.m_AssetKindFilter=0;e.m_AssetFilter.clear();e.m_AssetNavigation.projectSearch=false;}
 static ImVec2 Tree(EditorLayer& e,const std::string& key){
  ImGuiWindow* w=nullptr;for(auto* child:GImGui->Windows)if(child->Active&&std::string(child->Name).starts_with("Asset Browser/Directories_"))w=child;
  if(!w)throw std::runtime_error("no native folder tree");
  int row=1,target=-1;auto visit=[&](auto&& self,const std::string& parent)->void{for(const auto& path:e.m_AssetNavigation.children[parent]){if(path==key)target=row;++row;if(e.m_AssetExpanded.contains(path))self(self,path);}};visit(visit,"");
  if(target<0)throw std::runtime_error("tree destination not visible");
  return {w->WorkRect.Min.x+90,w->WorkRect.Min.y+target*(w->FontRefSize+4*ImGui::GetStyle().FontScaleDpi)+w->FontRefSize*.5f};
 }
 static void Expand(EditorLayer& e){e.m_AssetExpanded={"Workflow"};}
 static ImVec2 Card(EditorLayer& e,const std::string& key){
  ImGuiWindow* content=nullptr;for(auto* w:GImGui->Windows)if(w->Active&&std::string(w->Name).starts_with("Asset Browser/Content_"))content=w;
  if(!content)throw std::runtime_error("no native Content window");
  auto* table=ImGui::TableFindByID(content->GetID("ContentItems"));if(!table)throw std::runtime_error("no ContentItems table");
  auto& rows=e.m_AssetNavigation.content[e.m_AssetDirectory];
  auto found=std::find_if(rows.begin(),rows.end(),[&](const auto& r){return r.key==key;});if(found==rows.end())throw std::runtime_error("card absent: "+key);
  const float h=content->FontRefSize+4*ImGui::GetStyle().FontScaleDpi+2*ImGui::GetStyle().CellPadding.y;
  return {table->OuterRect.Min.x+65,table->OuterRect.Min.y+float(found-rows.begin())*h+h*.5f};
 }
};
}
static unsigned checks=0;
static std::string lastError;
static void Check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(std::string(why)+"\n"+lastError);}
static std::string U8(std::u8string_view s){return {reinterpret_cast<const char*>(s.data()),s.size()};}
static void Write(const fs::path& p,const std::string& text){fs::create_directories(p.parent_path());std::ofstream out(p,std::ios::binary);out<<text;Check(bool(out),"fixture write");}
static std::string Read(const fs::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
static std::string Id(){return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());}
struct Fixture {
    fs::path previous=fs::current_path(),base=previous/("asset-files-"+Id());
    Fixture(){fs::create_directories(base/"assets/scripts");fs::create_directories(base/"engine/assets/scripts");fs::current_path(base);}
    ~Fixture(){fs::current_path(previous);std::error_code ec;fs::remove_all(base,ec);}
};
static void Cpu(){
    Fixture fixture;const auto active=AssetPaths::ResolveAssetRoot(),source=fixture.base/"engine/assets";
    const std::string old="scripts/previous.lua";Write(active/old,"return {}\n");Write(source/old,"-- authoring copy\nreturn {}\n");
    AssetCatalog catalog;auto& error=lastError;
    Check(catalog.CreateStarter("Anywhere/Empty.prefab.json","Empty Prefab",&error),"empty prefab anywhere in Assets");
    auto empty=scripting::ReadPrefab("Anywhere/Empty.prefab.json");scripting::ValidatePrefab(empty);
    ecs::World emptyWorld;auto spawned=scripting::SpawnPrefab(emptyWorld,"Anywhere/Empty.prefab.json",{1,2,3});
    Check(emptyWorld.HasComponent<ecs::TransformComponent>(spawned)&&!emptyWorld.HasComponent<ecs::MeshRendererComponent>(spawned),"empty prefab runtime Transform without mesh");
    auto child=scripting::NewChildPrefab("Anywhere/Empty.prefab.json");
    Check(SceneSerializer::WriteAtomic(active/"Anywhere/Child.prefab.json",child,&error,false),"empty child prefab save");
    Check(emptyWorld.IsAlive(scripting::SpawnPrefab(emptyWorld,"Anywhere/Child.prefab.json",{})),"empty inherited prefab runtime spawn");
    for(auto type:{"PostFX","Lens Distortion"}){
        auto key=std::string("Anywhere/")+type+".postfx.json";Check(catalog.CreateStarter(key,type,&error),"paired PostFX creation");
        Check(PostFxLoader::Load(key,{}).success,"created PostFX resolves sibling shader through actual loader");
        auto bytes=Read(active/key);Check(!catalog.CreateStarter(key,type,&error)&&Read(active/key)==bytes,"creation never overwrites existing resources");
    }
    Check(catalog.CreateStarter("Anywhere/New.hlsl","HLSL",&error),"HLSL creation");
    auto sourceCode=Read(active/"Anywhere/New.hlsl");
    ResourceManager shaderResources(nullptr);auto authoredShader=shaderResources.Load<ShaderResource>("Anywhere/New.hlsl");
    Check(authoredShader->IsLoaded()&&!authoredShader->GetData().placeholder,"authored Assets HLSL loads via real ResourceManager");
    Check(AssetPaths::NormalizeShaderKey(active/"Anywhere/New.hlsl")=="Anywhere/New.hlsl","absolute authored shader normalizes to project key");
    Check(!catalog.CreateStarter("dx12/textured.hlsl","HLSL",&error),"engine backend shader namespaces protected");
    for(auto entry:{"VSMain","PSMain"}){ID3DBlob *code=nullptr,*diagnostic=nullptr;
        auto hr=D3DCompile(sourceCode.data(),sourceCode.size(),"starter",nullptr,nullptr,entry,entry[0]=='V'?"vs_5_1":"ps_5_1",0,0,&code,&diagnostic);
        if(code)code->Release();if(diagnostic)diagnostic->Release();Check(SUCCEEDED(hr),"new mesh HLSL compiles native VS/PS");}
    Check(!catalog.CreateStarter("../escape.hlsl","HLSL",&error),"starter traversal blocked");
    (void)catalog.PollExternalChanges(.3f);
    // Native change notifications use wall time. Advancing a synthetic 20 ms
    // every 2 ms can finish debounce before the OS delivers a write burst.
    auto previousPoll=std::chrono::steady_clock::now();
    auto pollReal=[&]{auto now=std::chrono::steady_clock::now();float elapsed=std::chrono::duration<float>(now-previousPoll).count();previousPoll=now;return catalog.PollExternalChanges(elapsed);};
    Write(active/"Anywhere/external.lua","return {}");
    bool notified=false;for(int i=0;i<400&&!notified;++i){std::this_thread::sleep_for(std::chrono::milliseconds(2));notified=pollReal();}
    Check(notified,"native external file watcher debounced notification");
    auto event=[&](const char* label){bool found=false;for(int i=0;i<400&&!found;++i){std::this_thread::sleep_for(std::chrono::milliseconds(2));found=pollReal();}Check(found,label);catalog.Refresh();};
    fs::rename(active/"Anywhere/external.lua",active/"Anywhere/renamed.lua");event("watch external rename");
    fs::remove(active/"Anywhere/renamed.lua");event("watch external delete");
    fs::create_directory(active/"Anywhere/ExternalFolder");event("watch folder create");
    fs::remove(active/"Anywhere/ExternalFolder");event("watch folder delete");
    for(int i=0;i<15;++i)Write(active/("Anywhere/burst"+std::to_string(i)+".lua"),"return {}");event("watch rapid changes");
    bool quiet=true;for(int i=0;i<200;++i){std::this_thread::sleep_for(std::chrono::milliseconds(2));quiet&=!pollReal();}Check(quiet,"no repeated refresh on unchanged frames");
    fs::create_directories(active/"MoveSource/Sub");fs::create_directories(active/"MoveTarget");
    Write(active/"MoveSource/a.lua","return {}");Write(active/"MoveSource/b.lua","return {}");
    auto plan=AssetCatalog::PlanMove({"MoveSource/a.lua","MoveSource/b.lua"},"MoveTarget");
    Check(plan.files.size()==2&&fs::exists(active/"MoveSource/a.lua"),"batch preflight has no mutation");
    Write(active/"MoveSource/a.lua","return {changed=true}");Check(!catalog.ExecuteMove(plan,&error)&&fs::exists(active/"MoveSource/b.lua"),"changed source aborts entire batch");
    plan=AssetCatalog::PlanMove({"MoveSource/a.lua","MoveSource/b.lua"},"MoveTarget");
    Check(catalog.ExecuteMove(plan,&error)&&fs::exists(active/"MoveTarget/a.lua")&&fs::exists(active/"MoveTarget/b.lua"),"batch file move to arbitrary folder");
    Write(active/"MoveSource/Sub/leaf.lua","return {}");
    plan=AssetCatalog::PlanMove({"MoveSource"},"MoveTarget");Check(plan.files.size()==1&&catalog.ExecuteMove(plan,&error)&&fs::exists(active/"MoveTarget/MoveSource/Sub/leaf.lua"),"folder move preserves nested assets");
    auto rejectMove=[&](std::vector<std::string> keys,const std::string& folder){try{(void)AssetCatalog::PlanMove(keys,folder);return false;}catch(...){return true;}};
    Check(rejectMove({"MoveTarget"},"MoveTarget")&&rejectMove({"MoveTarget"},"MoveTarget/MoveSource/Sub"),"folder self/descendant blocked");
    Check(rejectMove({"MoveTarget","MoveTarget/a.lua"},""),"overlapping selection blocked");
    Write(active/"MoveTarget/MoveSource/Sub/owned.hlsl","x");Check(rejectMove({"MoveTarget/MoveSource"},""),"unsupported nested shader blocks entire move");fs::remove(active/"MoveTarget/MoveSource/Sub/owned.hlsl");
    Write(active/"reference.data.json","{\"asset\":\"MoveTarget/a.lua\"}");
    Check(rejectMove({"MoveTarget/a.lua","MoveTarget/b.lua"},""),"DataAsset reference blocks entire batch");fs::remove(active/"reference.data.json");
#ifdef _WIN32
    Write(active/"Rollback/first.lua","return {}");Write(active/"Rollback/Folder/second.lua","return {}");
    plan=AssetCatalog::PlanMove({"Rollback/first.lua","Rollback/Folder"},"MoveTarget");
    HANDLE lockedFolder=CreateFileW((active/"Rollback/Folder").c_str(),FILE_LIST_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    Check(lockedFolder!=INVALID_HANDLE_VALUE,"lock second root for rollback test");
    const bool rolledBack=!catalog.ExecuteMove(plan,&error);CloseHandle(lockedFolder);
    Check(rolledBack&&error.find("rolled back")!=std::string::npos&&fs::exists(active/"Rollback/first.lua")&&!fs::exists(active/"MoveTarget/first.lua"),"actual filesystem failure rolls back completed first move");
#endif
    Write(active/"scripts/gameplay/module.lua","return {value=1}");Write(active/"scripts/module_importer.lua","local M=Script.require('gameplay.module');return {}");
    Check(!AssetCatalog::AuditDependencies("scripts/gameplay/module.lua").confirmed.empty(),"literal import participates in existing dependency audit");
    Check(!catalog.DeleteAssetFile("scripts/gameplay/module.lua",&error)&&!catalog.RenameAssetFile("scripts/gameplay/module.lua","scripts/gameplay/renamed.lua",&error),"imports protect module delete and rename without an Entity attachment");
    fs::remove(active/"scripts/module_importer.lua");Write(active/"scripts/module_importer.lua","local M=Script.require(name);return {}");
    Check(!AssetCatalog::AuditDependencies("scripts/gameplay/module.lua").dynamic.empty()&&!catalog.DeleteAssetFile("scripts/gameplay/module.lua",&error),"dynamic imports require existing explicit risk decision");
    fs::remove(active/"scripts/module_importer.lua");Check(catalog.DeleteAssetFile("scripts/gameplay/module.lua",&error),"unreferenced plain module supports normal file workflow");

    Check(catalog.RenameAssetFile(old,"scripts/renamed.lua",&error),"previous-session Lua can be renamed");
    Check(!fs::exists(active/old)&&Read(source/old)=="-- authoring copy\nreturn {}\n","rename changes active root only");
    catalog.Refresh();Check(std::none_of(catalog.Entries().begin(),catalog.Entries().end(),[&](const auto& row){return row.key==old;}),"old source fallback does not reappear after active rename");
    const auto unicode=U8(u8"игрок контроллер");
    auto key=AssetCatalog::RenameDestination("scripts/renamed.lua",unicode);
    Check(catalog.RenameAssetFile("scripts/renamed.lua",key,&error)&&fs::exists(editor::ResolveEditableLuaScript(key)),"Unicode rename resolves through ScriptEditor");
    for(const auto& name:std::vector<std::string>{"","..","../escape","a/b","a\\b","bad?","CON","nul.txt","trailing.","trailing ","a:",std::string("a\0b",3)}){
        bool rejected=false;try{(void)AssetCatalog::RenameDestination(key,name);}catch(...){rejected=true;}Check(rejected,"invalid Windows filename rejected");
    }
    Write(active/"scripts/collision.lua","keep");Check(!catalog.RenameAssetFile(key,"scripts/collision.lua",&error)&&Read(active/"scripts/collision.lua")=="keep","rename never overwrites");
    Check(!catalog.RenameAssetFile(key,"prefabs/else.lua",&error)&&!catalog.RenameAssetFile(key,"scripts/else.txt",&error),"folder and suffix preserved");
    Write(active/"prefabs/test.prefab.json","{\"version\":1,\"components\":{}}");
    Check(AssetCatalog::RenameDestination("prefabs/test.prefab.json","other.prefab.json")=="prefabs/other.prefab.json","double suffix retained once");
    Check(catalog.RenameAssetFile("prefabs/test.prefab.json","prefabs/other.prefab.json",&error),"unreferenced Prefab rename");
    Check(catalog.DeleteAssetFile("prefabs/other.prefab.json",&error),"unreferenced Prefab deletion");
    for(const auto& ref:std::vector<std::pair<fs::path,std::string>>{{active/"scenes/other.json",key},{active/"prefabs/ref.prefab.json",key},{active/"materials/ref.material.json",key+"#view"},{source/"scenes/source.json",key}}){
        Write(ref.first,nlohmann::json{{"dependency",ref.second}}.dump());auto reasons=AssetCatalog::Dependencies(key);
        Check(!reasons.empty()&&!catalog.DeleteAssetFile(key,&error)&&!catalog.RenameAssetFile(key,"scripts/unused.lua",&error),"project dependencies block delete and rename");
        Check(error.find(AssetPaths::ToUtf8String(fs::weakly_canonical(ref.first)))!=std::string::npos,"dependency filename appears in diagnostic");fs::remove(ref.first);
    }
    Write(active/"scripts/ref.lua","return { file = \""+key+"\" }");Check(!catalog.DeleteAssetFile(key,&error),"Lua text references block mutation");fs::remove(active/"scripts/ref.lua");
    Write(active/"scenes/broken.json","{");Check(!catalog.DeleteAssetFile(key,&error)&&error.find("Invalid dependency JSON")!=std::string::npos,"incomplete audit fails closed");fs::remove(active/"scenes/broken.json");
    Write(active/"textures/user.png","texture");Write(active/"materials/ref.json","{\"texture\":\"textures/user.png#srgb\"}");Check(!catalog.DeleteAssetFile("textures/user.png",&error),"texture subresource references blocked");fs::remove(active/"materials/ref.json");
    Check(catalog.RenameAssetFile("textures/user.png","textures/renamed.png",&error)&&catalog.DeleteAssetFile("textures/renamed.png",&error),"standalone user texture operations");
    Write(active/"textures/user space.png","texture");Write(active/"models/encoded.gltf","{\"images\":[{\"uri\":\"../textures/user%20space.png\"}]}");Check(!catalog.DeleteAssetFile("textures/user space.png",&error),"encoded model dependencies fail closed");fs::remove(active/"models/encoded.gltf");
    Write(active/"environments/user.hdr","HDR");Check(catalog.DeleteAssetFile("environments/user.hdr",&error),"unreferenced user HDR deletion");
    Write(active/"materials/previous.material.json","{\"version\":4}");Check(catalog.RenameMaterial("materials/previous.material.json","materials/old-session.material.json",&error)&&catalog.DeleteMaterial("materials/old-session.material.json",&error),"old material API also supports previous sessions");
    const auto unicodeMaterial=U8(u8"materials/материал.material.json");Check(catalog.CreateMaterial(unicodeMaterial,&error),"Unicode material creation");
    ResourceManager resources(nullptr);auto loaded=resources.Load<MaterialResource>(fs::u8path(unicodeMaterial));Check(loaded&&loaded->IsLoaded()&&!loaded->GetData().placeholder&&loaded->GetKey()==unicodeMaterial,"Unicode material metadata loads with UTF-8 cache key");
    Check(catalog.RenameAssetFile(unicodeMaterial,U8(u8"materials/другой.material.json"),&error)&&catalog.DeleteAssetFile(U8(u8"materials/другой.material.json"),&error),"Unicode material rename/delete");
    Write(active/"materials/stress.material.json","{}");Check(!catalog.DeleteAssetFile("materials/stress.material.json",&error),"built-in asset protected");
    Write(active/"scripts/generated.spv","binary");Check(!catalog.DeleteAssetFile("scripts/generated.spv",&error)&&!catalog.DeleteAssetFile("scripts/missing.lua",&error)&&!catalog.DeleteAssetFile("../outside.lua",&error),"generated/missing/outside files blocked");
#ifdef _WIN32
    const auto path=editor::ResolveEditableLuaScript(key);Check(SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_READONLY)!=0,"set readonly fixture");
    Check(!catalog.DeleteAssetFile(key,&error),"readonly file protected");SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_NORMAL);
#endif
    Write(fixture.base/"outside/script.lua","outside");std::error_code ec;fs::create_directory_symlink(fixture.base/"outside",active/"scripts/link",ec);
    if(!ec)Check(!catalog.DeleteAssetFile("scripts/link/script.lua",&error)&&Read(fixture.base/"outside/script.lua")=="outside","symlink escape rejected");else std::cout<<"SKIP symlink creation: "<<ec.message()<<'\n';
    if(!ec)fs::remove(active/"scripts/link");
    Application app;auto& ui=app.GetEditorLayer();auto entity=ui.CreateEntity(app,"Keep entity");
    Check(ui.AssignAsset(app,entity,ecs::AssetKind::LuaScript,key),"assign script fixture");
    Check(!ui.DeleteAssetFile(app,key,true,&error)&&error.find("Current scene")!=std::string::npos,"current LuaScriptComponent reference protected");
    ui.UndoEdit(app);Check(!ui.DeleteAssetFile(app,key,true,&error),"Redo reference protected");app.NewScene();
    ui.SelectAsset(key,"Lua Script");Check(!ui.DeleteAssetFile(app,key,false,&error)&&ui.SelectedAsset()==key&&fs::exists(active/fs::u8path(key)),"unconfirmed Delete preserves file and selection");
    app.SetEditorPlayMode(true);Check(!ui.RenameAssetFile(app,key,"play",true,&error)&&!ui.DeleteAssetFile(app,key,true,&error),"Play file operations blocked");app.SetEditorPlayMode(false);
    Check(ui.RenameAssetFile(app,key,"final",true,&error)&&ui.SelectedAsset()=="scripts/final.lua","rename updates selected path");
    Check(ui.DeleteAssetFile(app,"scripts/final.lua",true,&error)&&ui.SelectedAsset().empty(),"delete clears selection");
    Check(fs::exists(source/old)&&!fs::exists(active/"scripts/final.lua"),"authoring source never deleted");app.Shutdown();
    fs::create_directory(fixture.base/"switch-projects");auto switched=ProjectManager::Create(fixture.base/"switch-projects","Other");ProjectManager::Activate(switched);
    Check(catalog.PollExternalChanges(.01f),"project switch invalidates and rebinds watcher");
    Write(switched->ResolveAsset("external.lua"),"return {}");event("new project watcher receives create");
    Check(std::any_of(catalog.Entries().begin(),catalog.Entries().end(),[](const auto& row){return row.key=="external.lua";}),"new project catalog uses new root");
    ProjectManager::Activate({});Check(catalog.PollExternalChanges(.01f),"watcher returns to prior root");catalog.Refresh();
    Check(std::none_of(catalog.Entries().begin(),catalog.Entries().end(),[](const auto& row){return row.key=="external.lua";}),"old project entries are not retained");
}
static void PrefabDependencies(){
    const auto wave=Read(AssetPaths::ResolveAssetRoot()/"scripts/wave_controller.lua");Check(wave.find("self.enemy_prefab")!=std::string::npos,"real Wave controller fixture");
    Fixture fixture;const auto active=AssetPaths::ResolveAssetRoot(),source=fixture.base/"engine/assets";AssetCatalog catalog;auto& error=lastError;
    const std::string key="prefabs/entity_entity-123.prefab.json",renamed="prefabs/renamed.prefab.json",enemy="prefabs/wave_enemy.prefab.json";
    Write(active/key,"{\"version\":1,\"components\":{}}");Write(source/key,"{\"authoring\":true}");Write(active/enemy,"{\"version\":1,\"components\":{}}");
    Write(active/"scripts/wave_controller.lua",wave);Write(source/"scripts/wave_controller.lua",wave);
    auto audit=AssetCatalog::AuditDependencies(key);Check(audit.confirmed.empty()&&audit.incomplete.empty()&&!audit.dynamic.empty()&&AssetCatalog::Dependencies(key).empty(),"unrelated prefab has warnings, not hard dependencies");
    Check(!catalog.RenameAssetFile(key,renamed,&error)&&error.find("explicit confirmation")!=std::string::npos,"normal Rename confirmation does not imply dynamic consent");
    Check(catalog.RenameAssetFile(key,renamed,&error,true)&&Read(source/key)=="{\"authoring\":true}","explicit dynamic Rename changes only active root");
    Check(!catalog.DeleteAssetFile(renamed,&error)&&fs::exists(active/renamed),"Delete dynamic risk requires separate consent");
    Check(catalog.DeleteAssetFile(renamed,&error,true),"explicit dynamic Delete permitted for unrelated prefab");
    audit=AssetCatalog::AuditDependencies(enemy);Check(!audit.confirmed.empty()&&!catalog.DeleteAssetFile(enemy,&error,true)&&!catalog.RenameAssetFile(enemy,"prefabs/broken.prefab.json",&error,true),"actual Lua default remains protected even with dynamic consent");
    Check(Read(active/"scripts/wave_controller.lua")==wave&&Read(source/"scripts/wave_controller.lua")==wave,"dependency audit never edits Lua source");
    Write(active/key,"{\"version\":1,\"components\":{}}");
    for(const auto& path:{active/"scenes/override.json",active/"prefabs/override.prefab.json",source/"scenes/override.json"}){
        Write(path,nlohmann::json{{"components",{{"LuaScript",{{"scriptPath","scripts/wave_controller.lua"},{"properties",{{"enemy_prefab",key}}}}}}}}.dump());
        Check(!AssetCatalog::AuditDependencies(key).confirmed.empty()&&!catalog.DeleteAssetFile(key,&error,true)&&!catalog.RenameAssetFile(key,renamed,&error,true),"saved scene/prefab/source overrides remain hard blockers");fs::remove(path);
    }
    Application app;app.NewScene();auto& ui=app.GetEditorLayer();auto e=ui.CreateEntity(app,"Spawner");Check(ui.AssignAsset(app,e,ecs::AssetKind::LuaScript,"scripts/wave_controller.lua"),"assign Wave controller");
    ui.RecordEdit(app,"Prefab override");app.GetWorld().GetComponent<ecs::LuaScriptComponent>(e)->properties["enemy_prefab"]=key;ui.FinishTransaction(app);
    Check(!ui.DeleteAssetFile(app,key,true,&error,true),"unsaved Inspector override protected");ui.UndoEdit(app);Check(!ui.DeleteAssetFile(app,key,true,&error,true),"Redo prefab override protected");app.NewScene();
    Check(!ui.RenameAssetFile(app,key,"final",true,&error),"editor command requires separate dynamic consent");Check(ui.RenameAssetFile(app,key,"final",true,&error,true),"editor command forwards explicit risk consent");Check(ui.DeleteAssetFile(app,"prefabs/final.prefab.json",true,&error,true),"editor Delete forwards explicit risk consent");
    Write(active/key,"{}");
    for(const auto& root:{active,source})Write(root/"scripts/wave_controller.lua","--[[ World.spawn_prefab(unknown); '"+key+"' ]]\nreturn { on_start=function(self) World.spawn_prefab('prefabs/' .. 'wave_enemy.prefab.json', Vec3.new(0,0,0)) end }");
    audit=AssetCatalog::AuditDependencies(key);Check(audit.confirmed.empty()&&audit.dynamic.empty()&&audit.incomplete.empty(),"static unrelated spawn and comments do not create dynamic blockers");Check(catalog.DeleteAssetFile(key,&error),"unrelated static-spawn prefab needs no override");
    Check(!AssetCatalog::AuditDependencies(enemy).confirmed.empty()&&!catalog.DeleteAssetFile(enemy,&error,true),"constant Lua concatenation resolves actual asset");
    Write(active/key,"{}");Write(active/"scripts/computed.lua","local spawn = World.spawn_prefab; return {on_start=function(self) spawn(self.custom_name, Vec3.new(0,0,0)) end}");Check(!AssetCatalog::AuditDependencies(key).dynamic.empty(),"aliased dynamic spawn remains visible as risk");
    Write(active/"scripts/computed.lua","return {on_start=function(self) World['spawn_prefab'](self.custom_name, Vec3.new(0,0,0)) end}");Check(!AssetCatalog::AuditDependencies(key).dynamic.empty(),"table-index dynamic spawn remains visible as risk");
    Write(active/"scripts/direct.lua","return {fields={asset={type='asset', default=[=["+key+"]=]}}}");Check(!catalog.DeleteAssetFile(key,&error,true),"long Lua literal/default is a confirmed dependency");fs::remove(active/"scripts/direct.lua");
    Write(active/"scripts/escaped.lua","return {asset=\"prefabs\\x2fentity_entity-123.prefab.json\"}");Check(!catalog.RenameAssetFile(key,renamed,&error,true),"escaped Lua path remains protected");fs::remove(active/"scripts/escaped.lua");
    Write(active/"scenes/invalid.json","{");Check(!catalog.DeleteAssetFile(key,&error,true),"dynamic consent cannot bypass incomplete JSON audit");fs::remove(active/"scenes/invalid.json");
    Write(active/"scripts/broken.lua","return {bad='unterminated");Check(!catalog.DeleteAssetFile(key,&error,true)&&error.find("broken.lua")!=std::string::npos,"incomplete Lua audit stays protected and names its source");fs::remove(active/"scripts/broken.lua");
    Check(catalog.DeleteAssetFile(key,&error,true)&&Read(source/key)=="{\"authoring\":true}","explicit risk leaves source and unrelated files intact");
}
static void Gpu(){
    Check(editor::SavePreferences(editor::PreferencesPath(),editor::EditorPreferences{},&lastError),"isolated default preferences for native keyboard fixture");
    Application app;Check(app.Initialize(),"DX12 initialize");auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow());glfwHideWindow(window->GetGlfwHandle());SyntheticEditorInput synthetic;
    auto& ui=app.GetEditorLayer();auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());app.NewScene();auto entity=ui.CreateEntity(app,"Do not delete from Assets");
    const auto directory="scripts/asset-file-ui-"+Id();auto key=ui.CreateLuaScriptAsset(app,directory);
    struct Cleanup{fs::path path;~Cleanup(){std::error_code ec;fs::remove_all(path,ec);}} cleanup{AssetPaths::ResolveAssetRoot()/directory};
    app.SetFrameLimit(8);app.Run();auto* browser=ImGui::FindWindowByName("Asset Browser");Check(browser,"browser window");
    ImGui::SetWindowDock(browser,0,ImGuiCond_Always);ImGui::SetWindowPos(browser,ImVec2(20,260),ImGuiCond_Always);ImGui::SetWindowSize(browser,ImVec2(650,360),ImGuiCond_Always);ImGui::SetWindowFocus("Asset Browser");app.Run();app.SetFrameLimit(1);
    auto keypress=[&](ImGuiKey key){SyntheticEditorInput::Key(key,true);app.Run();SyntheticEditorInput::Key(key,false);app.Run();app.Run();};
    keypress(ImGuiKey_F2);auto* rename=ImGui::FindWindowByName("Rename Asset");Check(rename&&rename->Active,"F2 opens Rename Asset");
    Check(GImGui->InputTextState.HasSelection(),"rename stem is automatically selected");
    SyntheticEditorInput::Queue([](ImGuiIO& io){io.AddInputCharactersUTF8("player_controller");});app.Run();keypress(ImGuiKey_Enter);
    const auto renamed=directory+"/player_controller.lua";Check(ui.SelectedAsset()==renamed&&fs::exists(editor::ResolveEditableLuaScript(renamed))&&!fs::exists(AssetPaths::ResolveAssetRoot()/fs::u8path(key)),"Enter renames Lua with suffix and selection");
    fs::create_directories("../../diagnostics/asset-files");
    auto click=[&](float x,float y,int button=0){SyntheticEditorInput::Position(x,y);app.Run();SyntheticEditorInput::Button(button,true);app.Run();SyntheticEditorInput::Button(button,false);app.Run();app.Run();};
    ImGuiWindow* content=nullptr;for(auto* child:browser->DC.ChildWindows)if(std::string(child->Name).find("/Content_")!=std::string::npos)content=child;Check(content,"asset content child");
    click(content->WorkRect.Min.x+70,content->WorkRect.Min.y+ImGui::GetTextLineHeight()*.5f,1);
    Check(!GImGui->OpenPopupStack.empty(),"asset right-click actions open");gpu->SaveFrameImage("../../diagnostics/asset-files/context-menu.ppm");
    auto* menu=GImGui->OpenPopupStack.back().Window;Check(menu,"asset context window");ImGui::ActivateItemByID(ImHashStr("Rename...",0,menu->ID));app.Run();app.Run();
    Check(ImGui::FindWindowByName("Rename Asset")->Active,"context Rename uses same dialog as F2");gpu->SaveFrameImage("../../diagnostics/asset-files/rename.ppm");keypress(ImGuiKey_Escape);
    ImGui::SetWindowFocus("Asset Browser");app.Run();keypress(ImGuiKey_Delete);auto* deletion=ImGui::FindWindowByName("Delete Asset?");Check(deletion&&deletion->Active&&app.GetWorld().IsAlive(entity),"browser Delete confirms and never deletes Entity");
    keypress(ImGuiKey_Escape);Check(fs::exists(editor::ResolveEditableLuaScript(renamed))&&ui.SelectedAsset()==renamed,"Escape cancels Delete");
    ImGui::SetWindowFocus("Asset Browser");app.Run();keypress(ImGuiKey_F2);keypress(ImGuiKey_Escape);Check(ui.SelectedAsset()==renamed,"Escape cancels Rename");
    ImGui::ActivateItemByID(ImHashStr("##search",0,browser->ID));app.Run();app.Run();Check(ImGui::GetIO().WantTextInput,"search owns text input");keypress(ImGuiKey_Delete);keypress(ImGuiKey_F2);Check(!ImGui::FindWindowByName("Rename Asset")->Active&&!ImGui::FindWindowByName("Delete Asset?")->Active&&app.GetWorld().IsAlive(entity),"text input blocks file/entity shortcuts");keypress(ImGuiKey_Escape);
    ImGui::SetWindowFocus("Scene Hierarchy");app.Run();keypress(ImGuiKey_F2);Check(!ImGui::FindWindowByName("Rename Asset")->Active,"Hierarchy F2 never renames selected asset");
    // Hierarchy F2 starts its own inline Entity rename. Exit that text field
    // before testing the distinct Delete command, rather than deleting text.
    keypress(ImGuiKey_Escape);ImGui::SetWindowFocus("Scene Hierarchy");app.Run();keypress(ImGuiKey_Delete);Check(!app.GetWorld().IsAlive(entity)&&fs::exists(editor::ResolveEditableLuaScript(renamed)),"Hierarchy Delete retains entity behavior");
    ui.UndoEdit(app);app.SetEditorPlayMode(true);ImGui::SetWindowFocus("Asset Browser");app.Run();keypress(ImGuiKey_Delete);Check(!ImGui::FindWindowByName("Delete Asset?")->Active&&fs::exists(editor::ResolveEditableLuaScript(renamed)),"Play shortcut blocked");app.SetEditorPlayMode(false);app.Run();
    ImGui::SetWindowFocus("Asset Browser");app.Run();keypress(ImGuiKey_Delete);
    Write(editor::ResolveEditableLuaScript(renamed),"-- changed externally\nreturn {}\n");
    auto confirmDelete=[&]{auto* modal=ImGui::FindWindowByName("Delete Asset?");Check(modal&&modal->Active,"Delete confirmation active");ImGui::ActivateItemByID(ImHashStr("Delete",0,modal->ID));app.Run();app.Run();};
    confirmDelete();Check(fs::exists(editor::ResolveEditableLuaScript(renamed))&&ui.GetError().find("File changed")!=std::string::npos,"changed file is rechecked at Delete confirmation");keypress(ImGuiKey_Escape);
    ImGui::SetWindowFocus("Asset Browser");app.Run();keypress(ImGuiKey_Delete);
    Cleanup lateReference{AssetPaths::ResolveAssetRoot()/("scenes/asset-file-late-"+Id()+".json")};Write(lateReference.path,nlohmann::json{{"script",renamed}}.dump());
    confirmDelete();Check(fs::exists(editor::ResolveEditableLuaScript(renamed))&&ui.GetError().find("asset-file-late-")!=std::string::npos,"new dependency appearing after dialog blocks Delete");keypress(ImGuiKey_Escape);fs::remove(lateReference.path);
    ImGui::SetWindowFocus("Asset Browser");app.Run();keypress(ImGuiKey_Delete);gpu->SaveFrameImage("../../diagnostics/asset-files/delete-confirm.ppm");auto entityCount=app.GetWorld().GetAliveCount();confirmDelete();
    Check(!fs::exists(AssetPaths::ResolveAssetRoot()/fs::u8path(renamed))&&ui.SelectedAsset().empty()&&app.GetWorld().GetAliveCount()==entityCount,"Delete confirmation removes file, clears UI and preserves entities");
    const auto prefab="prefabs/asset-file-risk-"+Id()+".prefab.json";Cleanup prefabCleanup{AssetPaths::ResolveAssetRoot()/prefab};Write(prefabCleanup.path,"{}");
    Check(AssetCatalog::AuditDependencies(prefab).confirmed.empty()&&!AssetCatalog::AuditDependencies(prefab).dynamic.empty(),"real Wave controller produces dynamic warning for unrelated prefab");
    ui.SelectAsset(prefab,"Prefab");ImGui::SetWindowFocus("Asset Browser");app.Run();keypress(ImGuiKey_Delete);
    confirmDelete();Check(fs::exists(prefabCleanup.path)&&ImGui::FindWindowByName("Delete Asset?")->Active,"ordinary Delete button cannot bypass dynamic checkbox");
    auto* risk=ImGui::FindWindowByName("Delete Asset?");click(risk->WorkRect.Min.x+8,risk->DC.CursorPosPrevLine.y-ImGui::GetStyle().ItemSpacing.y-ImGui::GetFrameHeight()*.5f);confirmDelete();
    Check(!fs::exists(prefabCleanup.path),"dynamic checkbox enables explicit Delete in real modal");
    const auto again="prefabs/asset-file-risk-again-"+Id()+".prefab.json";Cleanup againCleanup{AssetPaths::ResolveAssetRoot()/again};Write(againCleanup.path,"{}");ui.SelectAsset(again,"Prefab");ImGui::SetWindowFocus("Asset Browser");app.Run();keypress(ImGuiKey_Delete);confirmDelete();Check(fs::exists(againCleanup.path),"dynamic consent resets for each operation");keypress(ImGuiKey_Escape);
    const auto uiFolder="ui/asset-files-"+Id();Cleanup uiCleanup{AssetPaths::ResolveAssetRoot()/uiFolder};
    const auto uiKey=uiFolder+"/Canvas-entity-author.ui.json";fs::create_directories(uiCleanup.path);Check(UILoader::Save(uiKey,ui::EmptyDocument(),&lastError),"authoring Canvas fixture");
    Check(ui.OpenDocument(app,uiKey),"open authoring Canvas document");app.SetFrameLimit(8);app.Run();const auto document=ui.ActiveDocument();
    ui.SwitchWorkspace(editor::EditorWorkspace::AssetManagement);app.Run();ui.SelectAsset(uiKey,"UI Document");ImGui::SetWindowFocus("Asset Browser");app.Run();app.SetFrameLimit(1);keypress(ImGuiKey_F2);
    rename=ImGui::FindWindowByName("Rename Asset");Check(rename&&rename->Active,"UI document real Rename dialog");SyntheticEditorInput::Queue([](ImGuiIO& io){io.AddInputCharactersUTF8("Canvas Renamed");});app.Run();keypress(ImGuiKey_Enter);
    const auto renamedUI=uiFolder+"/Canvas Renamed.ui.json";Check(fs::exists(uiCleanup.path/"Canvas Renamed.ui.json")&&!fs::exists(uiCleanup.path/"Canvas-entity-author.ui.json"),"UI rename preserves complete suffix");
    Check(ui.ActiveDocument()==document&&std::any_of(ui.Documents().begin(),ui.Documents().end(),[&](auto& d){return d.id==document&&d.key==renamedUI;}),"rename retains active Document identity with new key");
    auto draft=ui.GetUIDraft(app,renamedUI);draft.name="Unsaved renamed Canvas";Check(ui.EditUIDocument(app,renamedUI,draft),"edit renamed UI draft");Check(!ui.RenameAssetFile(app,renamedUI,"blocked",true,&lastError)&&ui.GetUIDraft(app,renamedUI).name==draft.name,"dirty UI rename blocked without losing draft");
    Check(ui.SaveUIDocument(app,renamedUI),"save renamed UI");Check(ui.UndoUIDocument(app,renamedUI)&&ui.UndoUIDocument(app,renamedUI,true),"UI history remains usable at renamed key");
    auto canvas=ui.CreateEntity(app,"Canvas reference");app.GetWorld().AddComponent<ecs::UICanvasComponent>(canvas).document=renamedUI;Check(!ui.RenameAssetFile(app,renamedUI,"referenced",true,&lastError)&&lastError.find("Current scene")!=std::string::npos,"referenced Canvas names concrete migration blocker");
    const auto style=uiFolder+"/Custom.uistyle.json";Check(ui::SaveStyles(style,{},&lastError),"owned user style");Check(ui.RenameAssetFile(app,style,"Styled",true,&lastError)&&fs::exists(uiCleanup.path/"Styled.uistyle.json"),"user UI Style rename preserves complete suffix");
    // Drive the real batch-import modal only against an isolated project.
    Cleanup importCleanup{fs::current_path()/("asset-import-ui-"+Id())};fs::create_directories(importCleanup.path);auto project=ProjectManager::Create(importCleanup.path,"ImportFixture");
    Check(app.OpenProject(project->descriptorPath,Application::ProjectDecision::Discard,&lastError),"open isolated import Project");
    const auto first=importCleanup.path/"first.png",second=importCleanup.path/"second.jpg";
    fs::copy_file(AssetPaths::ResolveAssetPath("@engine/textures/pbr_base.png"),first);fs::copy_file(AssetPaths::ResolveAssetPath("@engine/textures/pbr_control.jpg"),second);
    editor::EditorDocumentTestAccess::Import(ui,AssetPaths::ToUtf8String(first)+"\n"+AssetPaths::ToUtf8String(second),"textures");app.SetFrameLimit(3);app.Run();auto* import=ImGui::FindWindowByName("Import Asset");Check(import&&import->Active,"native Import Asset modal");ImGui::ActivateItemByID(import->GetID("Import (no overwrite)"));app.Run();
    Check(fs::is_regular_file(project->ResolveAsset("textures/first.png"))&&fs::is_regular_file(project->ResolveAsset("textures/second.jpg")),"batch PNG/JPEG publishes direct files");Check(!fs::exists(project->ResolveAsset("textures/first"))&&!fs::exists(project->ResolveAsset("textures/second")),"no filename package folders");
    editor::ImportPlan repeat;Check(editor::AssetImport::Scan(first,"textures/first.png",repeat,&lastError,{},true),"rescan standalone texture");auto original=Read(project->ResolveAsset("textures/first.png"));Check(!editor::AssetImport::Publish(project->AssetRoot(),repeat,&lastError)&&Read(project->ResolveAsset("textures/first.png"))==original,"repeat import collision leaves original bytes intact");

    { // Dedicated 3.1 native interactions in the isolated import project.
    fs::create_directories("diagnostics/asset-files");
    auto capture=[&](const char* path){SyntheticEditorInput::Position(-100,-100);for(int i=0;i<6;++i)app.Run();Check(gpu->SaveFrameImage(path),"settled native GPU capture");};
    ImGui::ActivateItemByID(import->GetID("Cancel"));app.Run();app.Run();
    const auto obj1=importCleanup.path/"one.obj",obj2=importCleanup.path/"two.obj";
    fs::copy_file(AssetPaths::ResolveAssetPath("@engine/models/validation_triangle.obj"),obj1);fs::copy_file(obj1,obj2);
    const auto missing=importCleanup.path/"missing.obj";
    editor::EditorDocumentTestAccess::Import(ui,AssetPaths::ToUtf8String(obj1)+"\n"+AssetPaths::ToUtf8String(obj2)+"\n"+AssetPaths::ToUtf8String(missing),"FreeDestination");app.Run();app.Run();
    ImGui::ActivateItemByID(import->GetID("Import (no overwrite)"));app.Run();app.Run();
    Check(fs::exists(project->ResolveAsset("FreeDestination/one/one.obj"))&&fs::exists(project->ResolveAsset("FreeDestination/two/two.obj"))&&!fs::exists(project->ResolveAsset("FreeDestination/missing")),"native model batch imports successes and reports failed source");
    Check(!ui.GetError().empty(),"partial import reports failure");
    capture("diagnostics/asset-files/import-partial.ppm");
    fs::copy_file(obj1,missing);ImGui::ActivateItemByID(import->GetID("Retry failed"));app.Run();app.Run();
    Check(fs::exists(project->ResolveAsset("FreeDestination/missing/missing.obj"))&&ui.GetError().empty(),"native Retry failed preserves prior successes");
    Check(app.GetResourceManager()->Load<MeshResource>("FreeDestination/one/one.obj")->IsLoaded(),"free destination mesh loads through ResourceManager");
    ImGui::ActivateItemByID(import->GetID("Cancel"));app.Run();app.Run();
    fs::create_directories(project->ResolveAsset("Workflow/Destination"));
    fs::create_directories(project->ResolveAsset("Workflow/Folder"));
    Write(project->ResolveAsset("Workflow/a.lua"),"return {}");Write(project->ResolveAsset("Workflow/b.lua"),"return {}");Write(project->ResolveAsset("Workflow/Folder/leaf.lua"),"return {}");
    ui.NavigateAssetFolder("Workflow");editor::EditorDocumentTestAccess::List(ui);
    app.SetFrameLimit(1);for(int i=0;i<80;++i){app.Run();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    browser=ImGui::FindWindowByName("Asset Browser");ImGui::SetWindowDock(browser,0,ImGuiCond_Always);ImGui::SetWindowPos(browser,{5,150},ImGuiCond_Always);ImGui::SetWindowSize(browser,{900,500},ImGuiCond_Always);ImGui::SetWindowFocus("Asset Browser");app.Run();app.Run();
    auto press=[&](const char* label,ImGuiWindow* w){std::cout<<"UI action: "<<label<<" in "<<(w?w->Name:"<missing>")<<std::endl;if(!w||!w->Active)throw std::runtime_error(std::string("Native action window inactive: ")+label+" in "+(w?w->Name:"<missing>"));Check(true,"native action window active");ImGui::ActivateItemByID(w->GetID(label));app.Run();app.Run();};
    auto point=[&](const std::string& key){return editor::EditorDocumentTestAccess::Card(ui,key);};
    auto cardClick=[&](const std::string& key,int button=0){auto p=point(key);click(p.x,p.y,button);};
    auto drag=[&](const std::string& key,ImVec2 target){auto p=point(key);SyntheticEditorInput::Position(p.x,p.y);app.Run();SyntheticEditorInput::Button(0,true);app.Run();SyntheticEditorInput::Position(p.x+12,p.y);app.Run();Check(ImGui::GetDragDropPayload()!=nullptr,"native source emitted drag payload");SyntheticEditorInput::Position(target.x,target.y);app.Run();app.Run();SyntheticEditorInput::Button(0,false);app.Run();app.Run();};
    cardClick("Workflow/a.lua");SyntheticEditorInput::Key(ImGuiMod_Ctrl,true);app.Run();cardClick("Workflow/b.lua");SyntheticEditorInput::Key(ImGuiMod_Ctrl,false);app.Run();
    Check(ui.SelectedAssets().size()==2,"native Ctrl multi-selection");
    drag("Workflow/a.lua",point("Workflow/Destination"));auto* move=ImGui::FindWindowByName("Move Resources");Check(move&&move->Active,"native batch move modal");capture("diagnostics/asset-files/batch-move.ppm");
    press("Cancel",move);Check(fs::exists(project->ResolveAsset("Workflow/a.lua"))&&!fs::exists(project->ResolveAsset("Workflow/Destination/a.lua")),"batch Cancel leaves all sources intact");
    drag("Workflow/a.lua",point("Workflow/Destination"));press("Move all",move);
    Check(fs::exists(project->ResolveAsset("Workflow/Destination/a.lua"))&&fs::exists(project->ResolveAsset("Workflow/Destination/b.lua")),"native batch drag confirmed");
    ui.NavigateAssetFolder("Workflow");app.Run();app.Run();ui.SelectAsset("","");
    drag("Workflow/Folder",point("Workflow/Destination"));press("Move all",move);
    Check(fs::exists(project->ResolveAsset("Workflow/Destination/Folder/leaf.lua")),"native folder card drag preserves nested file");
    ui.NavigateAssetFolder("Workflow");app.Run();app.Run();
    Write(project->ResolveAsset("Workflow/c.lua"),"return {}");Write(project->ResolveAsset("Workflow/d.lua"),"return {}");
    auto settle=[&]{for(int i=0;i<70;++i){app.Run();std::this_thread::sleep_for(std::chrono::milliseconds(5));}};settle();
    cardClick("Workflow/c.lua");drag("Workflow/c.lua",point("Workflow/Destination"));press("Move all",move);
    Check(fs::exists(project->ResolveAsset("Workflow/Destination/c.lua")),"native single file to folder card");
    ui.NavigateAssetFolder("Workflow");editor::EditorDocumentTestAccess::List(ui,true);editor::EditorDocumentTestAccess::Expand(ui);app.Run();app.Run();
    cardClick("Workflow/d.lua");drag("Workflow/d.lua",editor::EditorDocumentTestAccess::Tree(ui,"Workflow/Destination"));press("Move all",move);
    Check(fs::exists(project->ResolveAsset("Workflow/Destination/d.lua")),"native file to folder tree");
    ui.NavigateAssetFolder("Workflow/Destination");ui.SelectAsset("Workflow/Destination/c.lua","Lua Script");app.Run();
    Write(project->ResolveAsset("Workflow/Destination/external.lua"),"return {}");settle();Check(ui.SelectedAsset()=="Workflow/Destination/c.lua"&&ui.AssetFolder()=="Workflow/Destination","external refresh preserves selection and folder");
    fs::rename(project->ResolveAsset("Workflow/Destination/external.lua"),project->ResolveAsset("Workflow/Destination/external-renamed.lua"));settle();(void)point("Workflow/Destination/external-renamed.lua");
    fs::remove(project->ResolveAsset("Workflow/Destination/external-renamed.lua"));settle();
    ui.NavigateAssetFolder("Workflow");editor::EditorDocumentTestAccess::List(ui);app.Run();app.Run();
    auto create=[&](const char* type){
      press("Create",browser);auto* popup=GImGui->OpenPopupStack.back().Window;press("Create Asset",popup);
      Check(GImGui->OpenPopupStack.size()>=2,"Create Asset submenu opened");press(type,GImGui->OpenPopupStack.back().Window);
      Check(ui.GetError().empty(),"native create reports success");return ui.SelectedAsset();
    };
    const auto hlsl=create("HLSL");Check(fs::exists(project->ResolveAsset(hlsl)),"native Create HLSL");
    const auto fx=create("PostFX");Check(PostFxLoader::Load(fx,{}).success,"native Create PostFX produces valid pair");
    const auto prefab=create("Empty Prefab");Check(fs::exists(project->ResolveAsset(prefab)),"native Create Empty Prefab");
    cardClick(prefab,1);press("Open Prefab Editor",GImGui->OpenPopupStack.back().Window);
    Check(app.IsPrefabEditing()&&app.GetWorld().GetAliveCount()==1,"native Empty Prefab Editor opens real single entity World");
    auto root=ui.GetSelectedEntity();auto* inspector=ImGui::FindWindowByName("Inspector");Check(inspector&&inspector->Active,"native prefab Inspector");
    ImGui::SetWindowDock(inspector,0,ImGuiCond_Always);ImGui::SetWindowPos(inspector,{650,50},ImGuiCond_Always);ImGui::SetWindowSize(inspector,{570,550},ImGuiCond_Always);ImGui::SetWindowFocus("Inspector");app.Run();app.Run();
    capture("diagnostics/asset-files/empty-prefab.ppm");
    int index=int(root.index),generation=int(root.generation),axis=0;
    auto numeric=ImHashData(&index,sizeof(index),inspector->ID);numeric=ImHashData(&generation,sizeof(generation),numeric);numeric=ImHashStr("Transform",0,numeric);numeric=ImHashStr("Properties",0,numeric);numeric=ImHashStr("position",0,numeric);numeric=ImHashData(&axis,sizeof(axis),numeric);numeric=ImHashStr("##axis",0,numeric);
    ImGui::ActivateItemByID(numeric);GImGui->NavNextActivateFlags=ImGuiActivateFlags_PreferInput;app.Run();app.Run();
    Check(ImGui::GetIO().WantTextInput,"actual Transform X field accepts text input");
    SyntheticEditorInput::Queue([](ImGuiIO& io){io.AddInputCharactersUTF8("2.5");});app.Run();keypress(ImGuiKey_Enter);
    Check(std::abs(app.GetWorld().GetComponent<ecs::TransformComponent>(root)->position.x-2.5f)<.0001f,"actual Inspector edited Empty Prefab Transform");
    ImGui::SetWindowFocus("Inspector");SyntheticEditorInput::Key(ImGuiMod_Ctrl,true);app.Run();keypress(ImGuiKey_Z);SyntheticEditorInput::Key(ImGuiMod_Ctrl,false);app.Run();
    Check(app.GetWorld().GetComponent<ecs::TransformComponent>(root)->position.x==0,"native prefab Undo");
    SyntheticEditorInput::Key(ImGuiMod_Ctrl,true);app.Run();keypress(ImGuiKey_Y);SyntheticEditorInput::Key(ImGuiMod_Ctrl,false);app.Run();Check(app.GetWorld().GetComponent<ecs::TransformComponent>(root)->position.x==2.5f,"native prefab Redo");
    press("Save",ImGui::FindWindowByName("Main Toolbar"));Check(!ui.IsSceneDirty(),"native Save Prefab toolbar");
    Check(scripting::ReadPrefab(prefab).at("entities").at(0).at("components").at("Transform").at("position").at(0)==2.5f,"saved Transform reloads from prefab");
    press("Workspace",ImGui::FindWindowByName("Main Toolbar"));press("Return to Scene",GImGui->OpenPopupStack.back().Window);
    Check(!app.IsPrefabEditing(),"native Return to Scene after save");ui.NavigateAssetFolder("Workflow");ImGui::SetWindowFocus("Asset Browser");app.Run();app.Run();
    cardClick(prefab,1);press("Create Child Prefab",GImGui->OpenPopupStack.back().Window);press("Create Child",ImGui::FindWindowByName("Create Child Prefab"));
    const auto childKey=prefab.substr(0,prefab.size()-12)+"-Child.prefab.json";
    Check(app.IsPrefabEditing()&&fs::exists(project->ResolveAsset(childKey)),"native Create Child Prefab outside legacy prefabs folder");
    Check(app.GetWorld().GetComponent<ecs::TransformComponent>(ui.GetSelectedEntity())->position.x==2.5f,"native child reload inherits saved parent Transform");
    ImGui::SetWindowFocus("Inspector");capture("diagnostics/asset-files/child-prefab.ppm");
    }
    Check(gpu->GetValidationErrorCount()==0,"DX12 validation clean");app.Shutdown();ProjectManager::Activate({});
}
int main(int argc,char**){try{ScopedEditorPreferenceRestore preserve;Cpu();PrefabDependencies();if(argc>1)Gpu();std::cout<<"AssetFileOperationsRegression: "<<checks<<" checks PASS\n";return 0;}catch(const std::exception& e){std::cerr<<"ASSET FILE FAILED: "<<e.what()<<'\n';return 1;}}
