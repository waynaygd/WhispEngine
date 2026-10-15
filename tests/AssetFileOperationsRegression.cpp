#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "editor/AssetCatalog.h"
#include "editor/ScriptEditor.h"
#include "ecs/components/LuaScriptComponent.h"
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "resources/ResourceManager.h"
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
}
static void Gpu(){
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
    auto* menu=GImGui->OpenPopupStack.back().Window;Check(menu,"asset context window");click(menu->WorkRect.Min.x+40,menu->WorkRect.Min.y+2*ImGui::GetTextLineHeightWithSpacing()+ImGui::GetTextLineHeight()*.5f);
    Check(ImGui::FindWindowByName("Rename Asset")->Active,"context Rename uses same dialog as F2");gpu->SaveFrameImage("../../diagnostics/asset-files/rename.ppm");keypress(ImGuiKey_Escape);
    ImGui::SetWindowFocus("Asset Browser");app.Run();keypress(ImGuiKey_Delete);auto* deletion=ImGui::FindWindowByName("Delete Asset?");Check(deletion&&deletion->Active&&app.GetWorld().IsAlive(entity),"browser Delete confirms and never deletes Entity");
    keypress(ImGuiKey_Escape);Check(fs::exists(editor::ResolveEditableLuaScript(renamed))&&ui.SelectedAsset()==renamed,"Escape cancels Delete");
    ImGui::SetWindowFocus("Asset Browser");app.Run();keypress(ImGuiKey_F2);keypress(ImGuiKey_Escape);Check(ui.SelectedAsset()==renamed,"Escape cancels Rename");
    click(browser->WorkRect.Min.x+30,browser->WorkRect.Min.y+ImGui::GetFrameHeight()*.5f);Check(ImGui::GetIO().WantTextInput,"search owns text input");keypress(ImGuiKey_Delete);keypress(ImGuiKey_F2);Check(!ImGui::FindWindowByName("Rename Asset")->Active&&!ImGui::FindWindowByName("Delete Asset?")->Active&&app.GetWorld().IsAlive(entity),"text input blocks file/entity shortcuts");keypress(ImGuiKey_Escape);
    ImGui::SetWindowFocus("Scene Hierarchy");app.Run();keypress(ImGuiKey_F2);Check(!ImGui::FindWindowByName("Rename Asset")->Active,"Hierarchy F2 never renames selected asset");keypress(ImGuiKey_Delete);Check(!app.GetWorld().IsAlive(entity)&&fs::exists(editor::ResolveEditableLuaScript(renamed)),"Hierarchy Delete retains entity behavior");
    ui.UndoEdit(app);app.SetEditorPlayMode(true);ImGui::SetWindowFocus("Asset Browser");app.Run();keypress(ImGuiKey_Delete);Check(!ImGui::FindWindowByName("Delete Asset?")->Active&&fs::exists(editor::ResolveEditableLuaScript(renamed)),"Play shortcut blocked");app.SetEditorPlayMode(false);
    ImGui::SetWindowFocus("Asset Browser");app.Run();keypress(ImGuiKey_Delete);
    Write(editor::ResolveEditableLuaScript(renamed),"-- changed externally\nreturn {}\n");
    auto confirmDelete=[&]{auto* modal=ImGui::FindWindowByName("Delete Asset?");Check(modal&&modal->Active,"Delete confirmation active");click(modal->WorkRect.Min.x+20,modal->DC.CursorPosPrevLine.y+ImGui::GetFrameHeight()*.5f);};
    confirmDelete();Check(fs::exists(editor::ResolveEditableLuaScript(renamed))&&ui.GetError().find("File changed")!=std::string::npos,"changed file is rechecked at Delete confirmation");keypress(ImGuiKey_Escape);
    ImGui::SetWindowFocus("Asset Browser");app.Run();keypress(ImGuiKey_Delete);
    Cleanup lateReference{AssetPaths::ResolveAssetRoot()/("scenes/asset-file-late-"+Id()+".json")};Write(lateReference.path,nlohmann::json{{"script",renamed}}.dump());
    confirmDelete();Check(fs::exists(editor::ResolveEditableLuaScript(renamed))&&ui.GetError().find("asset-file-late-")!=std::string::npos,"new dependency appearing after dialog blocks Delete");keypress(ImGuiKey_Escape);fs::remove(lateReference.path);
    ImGui::SetWindowFocus("Asset Browser");app.Run();keypress(ImGuiKey_Delete);gpu->SaveFrameImage("../../diagnostics/asset-files/delete-confirm.ppm");auto entityCount=app.GetWorld().GetAliveCount();confirmDelete();
    Check(!fs::exists(AssetPaths::ResolveAssetRoot()/fs::u8path(renamed))&&ui.SelectedAsset().empty()&&app.GetWorld().GetAliveCount()==entityCount,"Delete confirmation removes file, clears UI and preserves entities");Check(gpu->GetValidationErrorCount()==0,"DX12 validation clean");app.Shutdown();
}
int main(int argc,char**){try{ScopedEditorPreferenceRestore preserve;Cpu();if(argc>1)Gpu();std::cout<<"AssetFileOperationsRegression: "<<checks<<" checks PASS\n";return 0;}catch(const std::exception& e){std::cerr<<"ASSET FILE FAILED: "<<e.what()<<'\n';return 1;}}
