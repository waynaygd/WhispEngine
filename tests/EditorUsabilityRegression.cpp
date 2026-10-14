#include "EditorTestPreferences.h"
#define NOMINMAX
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "editor/AssetCatalog.h"
#include "editor/EditorTheme.h"
#include "resources/loaders/MaterialLoader.h"
#include "resources/loaders/EnvironmentLoader.h"
#include "scene/SceneSerializer.h"
#include <iostream>
#include <fstream>
#include <chrono>
#if defined(ENABLE_DX12)
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#include <GLFW/glfw3.h>
#endif
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void Cpu(){
    editor::ProjectileInput input;
    Check(!input.Update(true,false,true,false,true,false,false),"default diagnostic disabled");
    Check(!input.Update(true,true,true,false,true,false,false),"enabling while held does not shoot");
    input.Update(false,true,true,false,true,false,false);
    Check(input.Update(true,true,true,false,true,false,false),"R physical rising edge");
    Check(!input.Update(true,true,true,false,true,false,false),"no hold repeat");
    for(int condition=0;condition<5;++condition){input.held=false;Check(!input.Update(true,true,condition!=0,condition==1,condition!=2,condition==3,condition==4),"Play/focus/pause/UI/camera gating");Check(!input.Update(true,true,true,false,true,false,false),"blocked key is consumed until release");}
    Check(editor::EditorHotkeysAllowed(false,true,false,false)&&!editor::EditorHotkeysAllowed(true,true,false,false)&&!editor::EditorHotkeysAllowed(false,true,true,false),"Edit F/W/E/R separation");
    editor::EditorPreferences prefs;Check(!prefs.projectileTestInput&&!prefs.backgroundOverride,"preference defaults");prefs.projectileTestInput=true;prefs.backgroundOverride=true;Check(editor::PreferencesFromJson(editor::PreferencesToJson(prefs))==prefs,"diagnostic preference roundtrip");
    Application app;auto& ui=app.GetEditorLayer();auto entity=ui.CreateEntity(app,"Usability");ui.MarkSaved();const auto original=app.GetSceneSettings();auto settings=original;
    ui.BeginSceneTransaction(app,"Background");settings.background.background=EditorBackground::Cubemap;settings.background.cubemap="environments/uniform.hdr";settings.background.skyIntensity=2;Check(app.SetSceneSettings(settings),"set scene sky");ui.FinishTransaction(app);Check(ui.IsSceneDirty(),"background dirty");ui.UndoEdit(app);Check(app.GetSceneSettings().background==original.background,"background Undo");ui.RedoEdit(app);Check(app.GetSceneSettings().background==settings.background,"background Redo");
    auto document=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());ecs::World restored;SceneSettings reopened;std::string error;Check(SceneSerializer::RestoreWorld(document,restored,reopened,&error)&&reopened.background==settings.background,"v4 background roundtrip");
    auto legacy=document;legacy["settings"].erase("background");Check(SceneSerializer::RestoreWorld(legacy,restored,reopened,&error)&&reopened.background==SceneBackground{},"pre-2.2 v4 default background");
    for(int version=1;version<=3;++version){nlohmann::json j={{"version",version},{"entities",nlohmann::json::array()}};auto file=std::filesystem::path("editor22-legacy-"+std::to_string(version)+".json");Check(!std::filesystem::exists(file),"legacy fixture destination unused");Check(SceneSerializer::WriteAtomic(file,j,&error,false),"legacy fixture save");bool loaded=SceneSerializer::LoadWorld(file,restored,reopened,&error);std::filesystem::remove(file);Check(loaded&&reopened.background==SceneBackground{},"v1-v3 background migration");}
    auto invalid=document;invalid["settings"]["background"]["asset"]="../escape.hdr";Check(!SceneSerializer::RestoreWorld(invalid,restored,reopened,&error),"unsafe scene HDR path");
    ui.SetBackgroundOverride(true);ui.GetVisualSettings().skyIntensity=7;Check(ui.BuildVisualFrame(app).settings.skyIntensity==7,"Edit override resolution");app.SetEditorPlayMode(true);Check(ui.BuildVisualFrame(app).settings.skyIntensity==2,"Play uses authored sky");auto runtime=app.GetSceneSettings();runtime.background.skyIntensity=4;Check(app.SetSceneSettings(runtime),"Runtime sky edits");app.SetEditorPlayMode(false);Check(app.GetSceneSettings().background==settings.background&&app.GetWorld().IsAlive(entity),"Stop restores authoring background/world");
    editor::AssetCatalog catalog;catalog.Register({"Future Script","[Lua]","scripts",ecs::AssetKind::None,{".lua"}});catalog.Refresh();auto revision=catalog.Revision(),size=catalog.Entries().size();for(int i=0;i<500;++i){Check(catalog.Entries().size()==size&&catalog.Revision()==revision,"cached browser reads never rescan");}
    const auto id=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());const std::string key="materials/editor22-test-"+id+".json",renamed="materials/editor22-renamed-"+id+".json";
    struct Cleanup {std::vector<std::filesystem::path> paths;~Cleanup(){for(auto& p:paths){std::error_code e;std::filesystem::remove(p,e);}}} cleanup;
    Check(ui.CreateMaterialAsset(app,key,&error),"Create material v4");cleanup.paths.push_back(AssetPaths::ResolveAssetPath(key));auto material=MaterialLoader::Load(key,cleanup.paths.back());Check(material.success&&MaterialLoader::Serialize(material.data)["version"]==4,"created material current schema");Check(!ui.CreateMaterialAsset(app,key,&error)&&!ui.CreateMaterialAsset(app,"../escape.json",&error)&&!ui.DeleteMaterialAsset(app,"materials/stress.material.json",true,&error),"no overwrite/traversal/user-asset deletion");
    Check(ui.AssignAsset(app,entity,ecs::AssetKind::Material,key),"material assignment");Check(app.GetWorld().GetComponent<ecs::MaterialComponent>(entity)->materialPath==key,"assigned material field");ui.UndoEdit(app);Check(app.GetWorld().GetComponent<ecs::MaterialComponent>(entity)==nullptr,"material assignment Undo");ui.RedoEdit(app);Check(app.GetWorld().GetComponent<ecs::MaterialComponent>(entity)->materialPath==key,"material assignment Redo");
    Check(!ui.RenameMaterialAsset(app,key,renamed,true,&error),"authored scene references block rename");ui.UndoEdit(app);Check(!ui.DeleteMaterialAsset(app,key,true,&error),"Redo history references block delete");app.NewScene();
    auto ref=AssetPaths::ResolveAssetOutputPath("scenes/editor22-ref-"+id+".json");cleanup.paths.push_back(ref);Check(SceneSerializer::WriteAtomic(ref,{{"asset",key}},&error),"saved reference fixture");Check(!ui.RenameMaterialAsset(app,key,renamed,true,&error)&&!ui.DeleteMaterialAsset(app,key,true,&error),"saved references prevent dangling assets");std::filesystem::remove(ref);
    Check(ui.RenameMaterialAsset(app,key,renamed,true,&error),"rename unreferenced session asset");cleanup.paths.push_back(AssetPaths::ResolveAssetPath(renamed));Check(!ui.DeleteMaterialAsset(app,renamed,false,&error),"delete requires confirmation");Check(ui.DeleteMaterialAsset(app,renamed,true,&error)&&!std::filesystem::exists(cleanup.paths.back()),"confirmed delete implementation");
    for(int i=0;i<4;++i){ShadowSettings s;ApplyShadowQuality(s,ShadowQuality(i));Check(GetShadowQuality(s)==ShadowQuality(i)&&s.resolution<=2048&&ShadowMapBytes(s)==std::uint64_t(s.resolution)*s.resolution*4,"shadow presets and exact memory");}
    EnvironmentResource source;source.width=512;source.height=256;source.radiance.assign(512*256*4,2);auto env=IblMath::Preprocess(std::move(source),8,2,16);Check(env.sky.resolution==128&&env.environment.resolution==8&&env.sky.subresources.size()==6,"independent visible sky source density");for(float value:env.sky.subresources.front())Check(value==1||value==2,"sky preserves linear radiance");std::cout<<"Source sky preprocessing (512x256 panorama / 128 cube / 8 IBL / 16 samples): "<<env.preprocessMs<<" ms, includes first BRDF LUT initialization\n";
    ImGui::CreateContext();editor::ApplyEditorTheme(1);auto base=ImGui::GetStyle().FramePadding;editor::ApplyEditorTheme(2);Check(ImGui::GetStyle().FramePadding.x==base.x*2&&ImGui::GetStyle().FontScaleDpi==2,"DPI layout/font coherence");editor::ApplyEditorTheme(1);Check(ImGui::GetStyle().FramePadding.x==base.x,"DPI change is not cumulative");ImGui::DestroyContext();
    app.Shutdown();std::cout<<"Editor 2.2 input, background serialization/isolation/Undo, asset cache/CRUD/reference safety, source sky, theme and presets passed\n";
}
#if defined(ENABLE_DX12)
static void Gpu(){
    Application app;Check(app.Initialize(),"DX12 initialization");auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow());glfwHideWindow(window->GetGlfwHandle());auto* renderer=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());
    app.SetupPhysicsStressScene(500);app.SetEditorPlayMode(false);app.SetGpuInstancingEnabled(true);app.GetEditorLayer().SetGridVisible(true);app.GetEditorLayer().SetBackgroundOverride(false);
    auto settings=app.GetSceneSettings();settings.background.background=EditorBackground::Cubemap;settings.background.cubemap="environments/uniform.hdr";settings.background.skyIntensity=.15f;Check(app.SetSceneSettings(settings),"authored HDR sky");app.GetEnvironmentSettings().enabled=true;
    app.SetFrameLimit(250);Check(app.Run()==0,"Edit async HDR rendering");auto edit=renderer->ReadBackViewportPixels();auto diagnostics=renderer->GetEditorVisualDiagnostics();Check(diagnostics.skyReady&&diagnostics.backgroundDraws==1&&diagnostics.gridDraws==1,"Edit scene sky/grid");Check(app.GetRenderStatistics().submitted.drawCalls==1&&app.GetRenderStatistics().submitted.renderedInstances==501,"500 batching");
    const auto id=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());const std::string materialKey="materials/editor22-gpu-"+id+".json";
    std::string error;Check(app.GetEditorLayer().CreateMaterialAsset(app,materialKey,&error),"actual material creation");
    struct RemoveOwned {std::filesystem::path path;~RemoveOwned(){std::error_code e;std::filesystem::remove(path,e);}} owned{AssetPaths::ResolveAssetOutputPath(materialKey)};
    auto material=MaterialLoader::Load(materialKey,owned.path);Check(material.success,"new material loading");material.data.baseColor[0]=.1f;material.data.baseColor[1]=.8f;material.data.baseColor[2]=.3f;material.data.metallic=.3f;material.data.roughness=.7f;Check(MaterialLoader::Save(materialKey,material.data,&error),"existing atomic material save");
    auto selected=app.GetWorld().Entities().at(1);Check(app.GetEditorLayer().AssignAsset(app,selected,ecs::AssetKind::Material,materialKey),"created material assigned to actual scene mesh");app.SetFrameLimit(80);Check(app.Run()==0,"assigned material GPU warmup");Check(renderer->ReadBackViewportPixels()!=edit,"new PBR material changes actual viewport pixels");Check(app.GetRenderStatistics().submitted.renderedInstances==501&&app.GetRenderStatistics().submitted.drawCalls==1,"compatible material surface values remain per-instance in one batch");
    app.SetEditorPlayMode(true);app.SetFrameLimit(30);Check(app.Run()==0,"Play scene sky");auto play=renderer->ReadBackViewportPixels();diagnostics=renderer->GetEditorVisualDiagnostics();Check(diagnostics.skyReady&&diagnostics.backgroundDraws==1&&diagnostics.gridDraws==0,"Play retains HDR sky and suppresses grid");Check(edit.size()==play.size()&&edit.size()>40000,"sky readback size");unsigned equalSky=0;for(std::size_t i=0;i<edit.size()/8;i+=4)if(edit[i]==play[i]&&edit[i+1]==play[i+1]&&edit[i+2]==play[i+2])++equalSky;Check(equalSky>1000,"Edit/Play actual sky colors agree");
    auto changed=app.GetSceneSettings();changed.background.skyIntensity=0;Check(app.SetSceneSettings(changed),"Runtime background mutation");app.SetFrameLimit(10);Check(app.Run()==0,"Runtime sky change GPU");auto black=renderer->ReadBackViewportPixels();Check(black!=play,"visible intensity affects actual GPU output");app.SetEditorPlayMode(false);Check(app.GetSceneSettings().background==settings.background,"Stop restores HDR scene settings");
    glfwSetWindowSize(window->GetGlfwHandle(),960,640);app.SetFrameLimit(20);Check(app.Run()==0&&renderer->GetEditorVisualDiagnostics().skyReady,"sky resize/Stop");// Synthetic ImGui events validate the connected Focus shortcut; no native keyboard claim.
    app.GetEditorLayer().SelectEntity(selected);ImGui::SetWindowFocus("Viewport");app.SetFrameLimit(2);Check(app.Run()==0,"viewport keyboard scope");auto beforeFocus=app.GetCameraPosition();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_F,true);app.SetFrameLimit(2);Check(app.Run()==0,"synthetic Edit F");auto afterFocus=app.GetCameraPosition();Check(beforeFocus.x!=afterFocus.x||beforeFocus.y!=afterFocus.y||beforeFocus.z!=afterFocus.z,"Edit F reaches FocusEntity through real Inspector/viewport routing");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_F,false);app.SetFrameLimit(2);Check(app.Run()==0,"release synthetic F");ImGui::SetWindowFocus("Inspector");ImGui::GetIO().AddKeyEvent(ImGuiKey_F,true);app.SetFrameLimit(2);Check(app.Run()==0,"Inspector captures F");auto blocked=app.GetCameraPosition();Check(blocked.x==afterFocus.x&&blocked.y==afterFocus.y&&blocked.z==afterFocus.z,"Inspector focus blocks world hotkey");ImGui::GetIO().AddKeyEvent(ImGuiKey_F,false);app.SetFrameLimit(2);Check(app.Run()==0,"release captured F");
    Check(renderer->GetValidationErrorCount()==0,"DX12 ERROR/CORRUPTION zero");app.NewScene();Check(app.GetEditorLayer().DeleteMaterialAsset(app,materialKey,true,&error),"delete no longer referenced GPU material");app.Shutdown();std::cout<<"Editor2.2 actual HDR sky Edit/Play readback, 500 batching, runtime isolation, resize and debug validation passed\n";
}
#endif
int main(int argc,char** argv){try{ScopedEditorPreferenceRestore preservePreferences;Cpu();
#if defined(ENABLE_DX12)
    if(argc>1&&std::string(argv[1])=="--dx12")Gpu();
#endif
    return 0;}catch(const std::exception& e){std::cerr<<"EDITOR 2.2 FAILED: "<<e.what()<<'\n';return 1;}}
