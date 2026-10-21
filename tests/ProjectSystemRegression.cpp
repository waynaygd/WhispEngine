#define NOMINMAX
#include "EditorTestPreferences.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "resources/ResourceManager.h"
#include "resources/loaders/MaterialLoader.h"
#include "editor/AssetImport.h"
#include "editor/AssetCatalog.h"
#include "scene/SceneSerializer.h"
#include "ecs/PlayerSpawn.h"
#include "ecs/components/FogComponent.h"
#include "scripting/Prefab.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <source_location>
#ifdef ENABLE_DX12
#include "SyntheticEditorInput.h"
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <imgui_internal.h>
#include <GLFW/glfw3.h>
#endif
namespace fs=std::filesystem;
void Check(bool ok,const std::string& text,std::source_location loc=std::source_location::current()){
    if(!ok)throw std::runtime_error(std::string(text)+" at "+std::to_string(loc.line()));
}
struct Fixture{
    fs::path root=fs::current_path()/("project-system-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture(){fs::create_directory(root);}
    ~Fixture(){ProjectManager::Activate({});std::error_code ec;fs::remove_all(root,ec);}
};
void Text(const fs::path& p,const std::string& text){std::ofstream f(p,std::ios::binary);f<<text;Check(bool(f),"write fixture");}
void Script(const ProjectContext& p,int marker){
    Text(p.ResolveAsset("scripts/controller.lua"),"return {on_start=function(self) local p=World.get_position(self.entity); p.x="+std::to_string(marker)+"; World.set_position(self.entity,p) end}");
}
int main(int argc,char** argv){try{
    ScopedEditorPreferenceRestore preferences;Fixture fixture;
    auto a=ProjectManager::Create(fixture.root,"HospitalDemo"),b=ProjectManager::Create(fixture.root,"WhispTest");
    Script(*a,11);Script(*b,22);
    Text(fixture.root/"hospital.obj","v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    Text(fixture.root/"test.obj","v 0 0 0\nv 3 0 0\nv 0 2 0\nf 1 2 3\n");
    std::string error;editor::ImportPlan plan;
    Check(editor::AssetImport::Scan(fixture.root/"hospital.obj","models/building",plan,&error),error);
    Check(editor::AssetImport::Publish(a->AssetRoot(),plan,&error),error);
    Check(editor::AssetImport::Scan(fixture.root/"test.obj","models/building",plan,&error),error);
    Check(editor::AssetImport::Publish(b->AssetRoot(),plan,&error),error);
    // Both projects own the exact same texture key with different pixel data.
    const auto bitmap=AssetPaths::ResolveAssetPath("textures/validation_checker.bmp");
    Check(!bitmap.empty(),"legacy fixture texture");
    fs::copy_file(bitmap,a->ResolveAsset("textures/wall.bmp"));
    fs::copy_file(bitmap,b->ResolveAsset("textures/wall.bmp"));
    {std::fstream f(b->ResolveAsset("textures/wall.bmp"),std::ios::binary|std::ios::in|std::ios::out);f.seekp(54);char rgb[3]={1,2,3};f.write(rgb,3);}
    Application app;auto& ui=app.GetEditorLayer();
    using D=Application::ProjectDecision;
    Check(app.OpenProject(a->descriptorPath,D::Discard,&error),error);
    Check(editor::AssetImport::ProjectRoot()==a->AssetRoot(),"import active project root");
    MaterialResource wallMaterial;wallMaterial.name="Portable Wall";wallMaterial.placeholder=false;
    wallMaterial.shaderPath="dx12/textured.hlsl";wallMaterial.texturePath="textures/wall.bmp";
    Check(MaterialLoader::Save("materials/wall.material.json",wallMaterial,&error),error);
    auto e=ui.CreateEntity(app,"Persistent");auto fog=ui.CreateFog(app);auto light=ui.CreateLight(app,LightType::Point);auto start=ui.CreatePlayerStart(app);
    app.GetWorld().AddComponent<ecs::MaterialComponent>(e).materialPath="materials/wall.material.json";
    app.GetWorld().GetComponent<ecs::PlayerStartComponent>(start)->controllerScript="scripts/controller.lua";
    app.GetWorld().GetComponent<ecs::FogComponent>(fog)->density=.123f;
    auto script=ui.CreateEntity(app,"Script");app.GetWorld().AddComponent<ecs::LuaScriptComponent>(script).scriptPath="scripts/controller.lua";
    Check(ui.IsSceneDirty(),"dirty scene");Check(app.SaveCurrentScene(&error),error);
    const auto count=app.GetWorld().GetAliveCount();
    Check(app.SetStartupScene(app.GetSceneKey(),&error),error);
    Check(app.SaveSceneAs("Scenes/Another.json",false,&error),error);
    Check(!app.SaveSceneAs("Scenes/Another.json",false,&error),"overwrite protection");
    Check(!app.SaveSceneAs("../outside.json",true,&error),"scene traversal");
    Check(!app.OpenScene("Scenes/Absent.json",&error)&&app.GetWorld().IsAlive(e),"failed scene preserves world");
    Text(a->ResolveAsset("scripts/unused.lua"),"return {}\n");
    Check(editor::AssetImport::NewFolder(a->AssetRoot(),"scripts","nested",&error),error);
    Check(ui.RenameAssetFile(app,"scripts/unused.lua","renamed",true,&error),error);
    Check(ui.MoveAssetFile(app,"scripts/renamed.lua","scripts/nested/renamed.lua",true,&error),error);
    Check(ui.DeleteAssetFile(app,"scripts/nested/renamed.lua",true,&error),error);
    Text(a->ResolveAsset("scripts/disk_only.lua"),"return {}\n");
    ecs::World diskScene;auto diskEntity=diskScene.CreateEntity();diskScene.AddComponent<ecs::TransformComponent>(diskEntity);
    diskScene.AddComponent<ecs::LuaScriptComponent>(diskEntity).scriptPath="scripts/disk_only.lua";
    Check(SceneSerializer::SaveWorld(a->ResolveScene("Scenes/DiskReference.json"),diskScene,&error),error);
    Check(!ui.DeleteAssetFile(app,"scripts/disk_only.lua",true,&error),"saved project scene blocks deletion outside Assets");
    editor::AssetCatalog catalog;catalog.Refresh();bool hospital=false,foreign=false;
    for(const auto& entry:catalog.Entries()){hospital|=entry.key=="models/building/hospital.obj";foreign|=entry.key=="models/building/test.obj";}
    Check(hospital&&!foreign,"browser indexes only active project content");
    auto prefab=ui.CreatePrefabAsset(app,"prefabs");Check(!prefab.empty(),"project prefab create");
    ui.MarkDirty();const auto selection=ui.GetSelectedEntity();
    Check(!app.OpenProject(b->descriptorPath,D::Cancel,&error),"cancel");
    Check(app.GetProject()->root==a->root&&ui.GetSelectedEntity()==selection&&ui.IsSceneDirty(),"cancel preserves complete state");
    Text(fixture.root/"broken.whispproject","{broken");
    Check(!app.OpenProject(fixture.root/"broken.whispproject",D::Discard,&error),"bad descriptor");
    Check(app.GetWorld().IsAlive(e)&&ui.IsSceneDirty(),"failed preflight preserves current scene");
    auto missing=ProjectManager::Create(fixture.root,"MissingStartup");fs::remove(missing->ResolveScene(missing->startupScene));
    Check(!app.OpenProject(missing->descriptorPath,D::Discard,&error)&&app.GetWorld().IsAlive(e),"missing startup preserves current scene");
    auto corrupt=ProjectManager::Create(fixture.root,"CorruptStartup");Text(corrupt->ResolveScene(corrupt->startupScene),"{broken");
    Check(!app.OpenProject(corrupt->descriptorPath,D::Discard,&error)&&app.GetWorld().IsAlive(e),"corrupt startup preserves current scene");
    Check(app.OpenProject(b->descriptorPath,D::Save,&error),error);
    Check(!app.GetWorld().IsAlive(e)&&ui.GetSelectedEntities().empty()&&ui.UndoCount()==0,"world and editor handle reset");
    Check(!AssetPaths::ResolveAssetPath("scripts/FirstPersonController.lua").has_filename(),"no legacy content fallback");
    Check(app.OpenProject(a->descriptorPath,D::Discard,&error),error);
    Check(app.GetWorld().GetAliveCount()==count,"scene reopen component count");
    bool fogFound=false;app.GetWorld().ForEach<ecs::FogComponent>([&](auto,const auto& f){fogFound=f.density==.123f;});Check(fogFound,"fog roundtrip");
    auto runLua=[&](int expected){app.SetEditorPlayMode(true);app.GetLuaRuntime().Update(.016f);
        Check(app.GetLuaRuntime().InstanceCount()>=2,"player spawn and script instances");bool found=false;
        app.GetWorld().ForEach<ecs::PlayerComponent,ecs::TransformComponent>([&](auto,const auto&,const auto& t){found=t.position.x==expected;});Check(found,"project controller executes");};
    runLua(11);
    Script(*a,33);Check(app.GetLuaRuntime().ReloadScript("scripts/controller.lua"),"project Lua hot reload");app.GetLuaRuntime().Update(.016f);
    bool reloaded=false;app.GetWorld().ForEach<ecs::PlayerComponent,ecs::TransformComponent>([&](auto,const auto&,const auto& t){reloaded=t.position.x==33;});Check(reloaded,"reloaded project controller executes");
    app.SetEditorPaused(true);
    Check(!app.OpenProject(fixture.root/"broken.whispproject",D::Discard,&error)&&app.IsEditorPaused(),"failed switch preserves Pause");
    Check(app.OpenProject(b->descriptorPath,D::Discard,&error),error);
    Check(!app.IsEditorPlayMode()&&app.GetLuaRuntime().InstanceCount()==0,"Pause switch retires Lua");
    auto bs=ui.CreatePlayerStart(app);app.GetWorld().GetComponent<ecs::PlayerStartComponent>(bs)->controllerScript="scripts/controller.lua";
    auto be=ui.CreateEntity(app,"Script");app.GetWorld().AddComponent<ecs::LuaScriptComponent>(be).scriptPath="scripts/controller.lua";
    runLua(22);Check(app.OpenProject(a->descriptorPath,D::Discard,&error),error);Check(!app.IsEditorPlayMode(),"Play switch stops runtime");
    Check(app.CloseProject(D::Discard,&error),error);
    auto moved=fixture.root/fs::u8path("Перенос Hospital Demo");fs::rename(a->root,moved);
    Check(app.OpenProject(moved/"HospitalDemo.whispproject",D::Discard,&error),error);
    Check(!AssetPaths::ResolveAssetPath("scripts/controller.lua").empty(),"portable script reference");
    Check(scripting::SpawnPrefab(app.GetWorld(),prefab,{}).IsValid(),"portable prefab reference");
    // Exercise the existing ResourceManager, not a mock asset implementation.
    ResourceManager resources(nullptr);
    auto portableMaterial=resources.Load<MaterialResource>("materials/wall.material.json");
    Check(portableMaterial->IsLoaded()&&portableMaterial->GetData().texturePath=="textures/wall.bmp","material texture reference survives project move");
    auto ta=resources.Load<TextureResource>("textures/wall.bmp");Check(ta->IsLoaded(),"A texture loaded");auto pixels=ta->GetData().textureData.pixels;
    resources.DrainProjectLoads();resources.ClearAll();Check(app.OpenProject(b->descriptorPath,D::Discard,&error),error);
    auto tb=resources.Load<TextureResource>("textures/wall.bmp");Check(tb!=ta&&tb->GetData().textureData.pixels!=pixels,"same key uses B texture");
    Check(!resources.Load<TextureResource>("textures/missing.png")->IsLoaded(),"missing texture reports failure");
    Check(app.CloseProject(D::Discard,&error),error);Check(!app.GetProject()&&app.IsProjectBrowserActive(),"close returns to browser without selecting Legacy Workspace");
    Check(!AssetPaths::ResolveAssetPath("scripts/FirstPersonController.lua").empty(),"legacy resolution restored");
    // Recent descriptors/names persist in editor preferences, outside each game.
    Check(ui.RecentProjects().size()==3,"recent projects includes moved descriptor");
    editor::EditorPreferences prefs;prefs.recentProjects=ui.RecentProjects();
    Check(editor::SavePreferences(fixture.root/"preferences.json",prefs,&error),error);
    editor::EditorPreferences loaded;Check(editor::LoadPreferences(fixture.root/"preferences.json",loaded,&error)&&loaded.recentProjects==prefs.recentProjects,"recent projects disk roundtrip");
#ifdef ENABLE_DX12
    if(argc>1&&std::string(argv[1])=="--dx12"){
        Check(app.Initialize(Application::StartupMode::ProjectBrowser),"DX12 startup application initialize");
        auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow());glfwHideWindow(window->GetGlfwHandle());
        auto* dx=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(dx!=nullptr,"DX12 adapter");
        SyntheticEditorInput input;ui.SetFixedDiagnosticLayout(true);app.SetFrameLimit(2);
        Check(app.GetWorld().GetAliveCount()==0&&app.GetSceneKey().empty(),"startup does not load legacy scene");
        Check(!ui.HasProjectEdits(),"fresh Project Browser has no authored changes");
        auto browserOnly=[&]{
            Check(app.Run()==0,"Project Browser ImGui frames");
            auto* browser=ImGui::FindWindowByName("Project Browser");Check(browser&&browser->Active,"Project Browser rendered");
            Check(!browser->HasCloseButton,"startup browser cannot close into legacy editor");
            for(const char* name:{"Viewport","Scene Hierarchy","Asset Browser","Scene Settings","Inspector","Main Toolbar"}){
                auto* panel=ImGui::FindWindowByName(name);Check(!panel||!panel->Active,"startup has no editor panels behind browser");
            }
            Check(ui.GetViewportPixelWidth()==0&&!ui.OwnsViewportKeyboard(),"startup owns input without a viewport");
        };
        browserOnly();
        ui.RequestProjectOpen(app,AssetPaths::ToUtf8String(moved/"HospitalDemo.whispproject"));app.Run();
        Check(app.GetProject()&&app.GetProject()->root==moved,"fresh startup opens Project without unsaved modal");
        Check(app.CloseProject(D::Discard,&error),error);browserOnly();
        const auto startupFrame=ImGui::GetFrameCount();
        glfwSetWindowShouldClose(window->GetGlfwHandle(),GLFW_TRUE);Check(app.Run()==0,"startup window close exits normally");
        Check(ImGui::GetFrameCount()==startupFrame&&window->ShouldClose(),"startup window close needs no workspace confirmation");
        glfwSetWindowShouldClose(window->GetGlfwHandle(),GLFW_FALSE);browserOnly();
        app.SetEditorPlayMode(true);Check(!app.IsEditorPlayMode(),"startup does not accept Play");
        SyntheticEditorInput::Key(ImGuiKey_F11,true);app.Run();SyntheticEditorInput::Key(ImGuiKey_F11,false);browserOnly();
        Check(!app.OpenProject(fixture.root/"broken.whispproject",D::Discard,&error)&&app.IsProjectBrowserActive()&&app.GetWorld().GetAliveCount()==0,"failed startup open preserves browser");
        auto* chooser=ImGui::FindWindowByName("Project Browser")->DC.ChildWindows.front();
        ImGui::ActivateItemByID(chooser->GetID("Legacy Workspace"));app.Run();
        Check(!app.IsProjectBrowserActive()&&!app.GetProject()&&app.GetWorld().GetAliveCount()>0,"explicit Legacy Workspace loads existing scene");
        Check(!ui.HasProjectEdits(),"opening Legacy Workspace does not create authored changes");
        ui.RequestProjectOpen(app,AssetPaths::ToUtf8String(moved/"HospitalDemo.whispproject"));app.Run();
        Check(app.GetProject()&&app.GetProject()->root==moved,"clean Legacy opens Project without unsaved modal");
        Check(app.CloseProject(D::Discard,&error),error);browserOnly();
        Check(app.OpenProject(moved/"HospitalDemo.whispproject",D::Discard,&error),error);
        Check(!app.IsProjectBrowserActive(),"explicit project open bypasses startup screen");
        auto* rm=app.GetResourceManager();auto realA=rm->Load<TextureResource>("textures/wall.bmp");
        auto primitive=ui.CreatePrimitive(app,"cube");Check(primitive.IsValid(),"project built-in cube creation");
        app.GetWorld().GetComponent<ecs::TransformComponent>(primitive)->position={0,0,4};
        app.GetWorld().GetComponent<ecs::MaterialComponent>(primitive)->texturePath="textures/wall.bmp";
        Check(app.SaveCurrentScene(&error),error);app.SetFrameLimit(30);app.Run();
        Check(ImGui::FindWindowByName("Viewport")&&ImGui::FindWindowByName("Viewport")->Active,"project opens full editor viewport");
        Check(!ImGui::FindWindowByName("Project Browser")->Active,"successful project open dismisses startup browser");
        Check(rm->Load<MeshResource>("@engine/models/primitives/cube.obj")->IsLoaded(),"built-in engine mesh stays available");
        Check(app.GetRenderStatistics().submitted.drawCalls>0,"project scene DX12 draw calls");
        Check(realA->GetData().gpuHandle.IsValid(),"A wall texture uploaded to GPU");
        Check(!AssetPaths::ResolveAssetOutputPath("@engine/materials/default_primitive.material.json").has_filename(),"engine content writes rejected");
        ui.MarkDirty();const auto selected=ui.GetSelectedEntity();
        ui.RequestProjectOpen(app,AssetPaths::ToUtf8String(b->descriptorPath));app.SetFrameLimit(2);app.Run();
        auto* popup=ImGui::FindWindowByName("Unsaved project changes");Check(popup&&popup->Active,"Save Discard Cancel popup rendered");
        ImGui::ActivateItemByID(popup->GetID("Cancel"));app.Run();
        Check(app.GetProject()->root==moved&&ui.GetSelectedEntity()==selected&&ui.IsSceneDirty(),"UI Cancel preserves project and edits");
        ui.RequestProjectOpen(app,AssetPaths::ToUtf8String(b->descriptorPath));app.Run();
        popup=ImGui::FindWindowByName("Unsaved project changes");Check(popup&&popup->Active,"switch confirmation reopened");
        ImGui::ActivateItemByID(popup->GetID("Discard"));app.Run();
        Check(app.GetProject()->root==b->root,"UI Discard opens B");
        Check(!realA->GetData().gpuHandle.IsValid(),"old project GPU handle retired");
        auto realB=rm->Load<TextureResource>("textures/wall.bmp");
        Check(realB!=realA&&realB->GetData().textureData.pixels!=realA->GetData().textureData.pixels,"application resource cache isolation");
        Check(app.OpenProject(moved/"HospitalDemo.whispproject",D::Discard,&error),error);
        auto savedDuringPlay=ui.CreateEntity(app,"Saved before switching from Pause");
        app.SetEditorPlayMode(true);app.SetEditorPaused(true);
        ui.RequestProjectOpen(app,AssetPaths::ToUtf8String(b->descriptorPath));app.Run();
        popup=ImGui::FindWindowByName("Unsaved project changes");Check(popup&&popup->Active,"Pause exposes authoring edits in confirmation");
        ImGui::ActivateItemByID(popup->GetID("Save and continue"));app.Run();
        Check(app.GetProject()->root==b->root&&!app.IsEditorPlayMode(),"UI Save from Pause opens B");
        Check(app.OpenProject(moved/"HospitalDemo.whispproject",D::Discard,&error),error);
        bool savedName=false;app.GetWorld().ForEach<ecs::TagComponent>([&](auto,const auto& tag){savedName|=tag.name=="Saved before switching from Pause";});
        Check(savedName,"Save from Pause persisted authoring entities");
        fs::copy_file(AssetPaths::ResolveEngineAssetRoot()/"scripts/FirstPersonController.lua",app.GetProject()->ResolveAsset("scripts/FirstPersonController.lua"));
        app.GetEditorWorld().ForEach<ecs::PlayerStartComponent>([](auto,auto& marker){marker.controllerScript="scripts/FirstPersonController.lua";});
        app.SetEditorPlayMode(true);app.GetLuaRuntime().Update(.016f);
        Check(app.GetLuaRuntime().Error("scripts/FirstPersonController.lua").empty()&&app.GetLuaRuntime().HasCameraOwner(),"real project-local FPS controller owns runtime camera");
        app.SetEditorPlayMode(false);
        for(int i=0;i<4;++i){
            Check(app.OpenProject(moved/"HospitalDemo.whispproject",D::Discard,&error),error);
            app.SetFrameLimit(8);app.Run();app.SetEditorPlayMode(true);if(i%2)app.SetEditorPaused(true);
            Check(app.OpenProject(b->descriptorPath,D::Discard,&error),error);app.Run();
            Check(!app.IsEditorPlayMode()&&!app.IsViewportMouseCaptured(),"DX12 switch releases Play/Pause/cursor");
        }
        Check(dx->GetValidationErrorCount()==0,"DX12 validation ERROR/CORRUPTION zero");
        ui.MarkDirty();glfwSetWindowShouldClose(window->GetGlfwHandle(),GLFW_TRUE);app.SetFrameLimit(2);app.Run();
        popup=ImGui::FindWindowByName("Unsaved project changes");Check(popup&&popup->Active,"dirty project window close prompts");
        ImGui::ActivateItemByID(popup->GetID("Cancel"));app.Run();
        Check(!window->ShouldClose()&&!ui.ProjectExitConfirmed()&&ui.IsSceneDirty(),"window close Cancel preserves editor");
        glfwSetWindowShouldClose(window->GetGlfwHandle(),GLFW_TRUE);app.Run();
        popup=ImGui::FindWindowByName("Unsaved project changes");const auto exitFrame=ImGui::GetFrameCount();
        ImGui::ActivateItemByID(popup->GetID("Discard"));app.SetFrameLimit(30);app.Run();
        Check(ui.ProjectExitConfirmed()&&ImGui::GetFrameCount()<=exitFrame+2,"confirmed window close exits Run immediately");
        app.Shutdown();std::cout<<"Project system DX12/UI integration passed\n";
    }
#endif
    std::cout<<"Project system CPU integration passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
