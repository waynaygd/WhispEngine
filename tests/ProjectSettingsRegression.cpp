#define NOMINMAX
#include "EditorTestPreferences.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "scene/SceneSerializer.h"
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
#ifdef _WIN32
#ifdef APIENTRY
#undef APIENTRY
#endif
#include <Windows.h>
#endif
namespace fs=std::filesystem;
using Json=nlohmann::json;
static unsigned checks=0;
static void Check(bool value,const char* message,std::source_location line=std::source_location::current()){
    if(!value)throw std::runtime_error(std::string(message)+" at "+std::to_string(line.line()));++checks;
}
static Json Read(const fs::path& path){std::ifstream in(path);Json j;in>>j;return j;}
static void Write(const fs::path& path,const Json& j){std::ofstream out(path);out<<j.dump(2);Check(bool(out),"write owned fixture");}
struct Fixture {
    fs::path root=fs::current_path()/("project-settings-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture(){Check(fs::create_directory(root),"unique owned settings fixture");}
    ~Fixture(){ProjectManager::Activate({});std::error_code error;fs::remove_all(root,error);}
};
static void Cpu(){
    Fixture fixture;auto a=ProjectManager::Create(fixture.root,"A"),b=ProjectManager::Create(fixture.root,"B");
    Check(!Read(a->descriptorPath).contains("settings"),"MVP descriptor has no new settings");
    Application app;std::string error;using D=Application::ProjectDecision;
    Check(app.OpenProject(a->descriptorPath,D::Discard,&error),"open old descriptor");
    Check(app.GetProject()->settings.antiAliasing==AntiAliasingMode::Off,"old project defaults to Off");
    auto descriptor=Read(a->descriptorPath);descriptor["vendor"]={{"token",42}};
    descriptor["settings"]={{"future",{{"keep",true}}},{"rendering",{{"vendorOption","keep"}}}};
    Write(a->descriptorPath,descriptor);Check(app.OpenProject(a->descriptorPath,D::Discard,&error),"unknown project fields accepted");
    const auto original=app.GetProject();ProjectSettings high{AntiAliasingMode::FxaaHigh},low{AntiAliasingMode::FxaaLow};
    Check(app.SaveProjectSettings("Scenes/Main.json",high,&error),"save project settings");
    Check(app.GetProject()!=original&&original->settings.antiAliasing==AntiAliasingMode::Off,"published context replaced without mutating old snapshot");
    auto disk=Read(a->descriptorPath);Check(disk["vendor"]==descriptor["vendor"]&&disk["settings"]["future"]==descriptor["settings"]["future"]&&disk["settings"]["rendering"]["vendorOption"]=="keep","unknown descriptor fields preserved");
    Check(ProjectManager::Prepare(a->descriptorPath)->settings==high,"project settings reload");
    app.NewScene();Check(!app.GetSceneSettings().antiAliasingOverride&&app.GetEffectiveAntiAliasing()==AntiAliasingMode::FxaaHigh,"new project scene inherits default");
    const auto scene=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());const bool dirty=app.GetEditorLayer().IsSceneDirty();
    Check(app.SaveProjectSettings("Scenes/Main.json",low,&error),"change project default");
    Check(app.GetEffectiveAntiAliasing()==AntiAliasingMode::FxaaLow&&SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==scene&&app.GetEditorLayer().IsSceneDirty()==dirty,"derived default applies without baking or dirtying scene");
    Check(app.SaveSceneAs("Scenes/Inherited.json",false,&error),"save inherited scene");
    Check(Read(a->ResolveScene("Scenes/Inherited.json"))["settings"]["antiAliasingOverride"]==false,"inheritance marker serialized in schema v4");
    auto override=app.GetSceneSettings();override.antiAliasingOverride=true;override.antiAliasing=AntiAliasingMode::FxaaHigh;
    Check(app.SetSceneSettings(override,&error)&&app.SaveSceneAs("Scenes/Override.json",false,&error),"save scene override");
    Check(app.SaveProjectSettings("Scenes/Inherited.json",{},&error)&&app.GetEffectiveAntiAliasing()==AntiAliasingMode::FxaaHigh,"override wins over changed project default");
    Check(app.OpenScene("Scenes/Inherited.json",&error)&&app.GetEffectiveAntiAliasing()==AntiAliasingMode::Off&&!app.GetSceneSettings().antiAliasingOverride,"inherited scene reload uses current project default");
    Check(app.OpenScene("Scenes/Override.json",&error)&&app.GetEffectiveAntiAliasing()==AntiAliasingMode::FxaaHigh&&app.GetSceneSettings().antiAliasingOverride,"override survives disk roundtrip");
    Write(a->ResolveScene("Scenes/OldOff.json"),{{"version",4},{"entities",Json::array()}});
    Write(a->ResolveScene("Scenes/OldLow.json"),{{"version",4},{"settings",{{"antiAliasing",1}}},{"entities",Json::array()}});
    Check(app.SaveProjectSettings("Scenes/Inherited.json",high,&error),"set high default");
    Check(app.OpenScene("Scenes/OldOff.json",&error)&&app.GetSceneSettings().antiAliasingOverride&&app.GetEffectiveAntiAliasing()==AntiAliasingMode::Off,"old scene without settings retains Off");
    Check(app.OpenScene("Scenes/OldLow.json",&error)&&app.GetEffectiveAntiAliasing()==AntiAliasingMode::FxaaLow,"old explicit scene AA unchanged");
    Check(app.OpenScene("Scenes/Inherited.json",&error),"open inheritance for Play");
    const auto authored=app.GetSceneSettings();app.SetEditorPlayMode(true);
    Check(app.SaveProjectSettings("Scenes/Inherited.json",low,&error)&&app.GetEffectiveAntiAliasing()==AntiAliasingMode::FxaaLow,"project default can update during Play without modifying authored scene");
    override=authored;override.antiAliasingOverride=true;override.antiAliasing=AntiAliasingMode::Off;app.SetSceneSettings(override);app.SetEditorPaused(true);
    Check(app.SaveAuthoringSceneForTransition(&error)&&Read(a->ResolveScene("Scenes/Inherited.json"))["settings"]["antiAliasingOverride"]==false,"Save from Pause uses authoring inheritance instead of runtime override");
    app.SetEditorPlayMode(false);Check(app.GetSceneSettings()==authored&&app.GetEffectiveAntiAliasing()==AntiAliasingMode::FxaaLow,"Stop restores authored AA source and inherits current project");
    Check(app.SaveProjectSettings("Scenes/Inherited.json",high,&error),"restore A default");
    Check(app.OpenProject(b->descriptorPath,D::Discard,&error)&&app.GetEffectiveAntiAliasing()==AntiAliasingMode::Off,"switch B without A defaults leaking");
    Check(app.OpenProject(a->descriptorPath,D::Discard,&error)&&app.GetEffectiveAntiAliasing()==AntiAliasingMode::FxaaHigh,"switch A reloads default and startup inheritance");
    const auto active=app.GetProject();const auto beforeDisk=Read(a->descriptorPath);const auto beforeScene=app.GetSceneSettings();
    Check(!app.SaveProjectSettings("Scenes/Missing.json",low,&error)&&app.GetProject()==active&&Read(a->descriptorPath)==beforeDisk,"bad startup rejects entire settings update");
    Write(a->ResolveScene("Scenes/Corrupt.json"),{{"version",999},{"entities",Json::array()}});
    Check(!app.SaveProjectSettings("Scenes/Corrupt.json",low,&error)&&app.GetProject()==active,"invalid scene preflight preserves active settings");
    Check(!app.SaveProjectSettings(active->startupScene,{AntiAliasingMode(3)},&error)&&app.GetProject()==active&&Read(a->descriptorPath)==beforeDisk,"unsupported native AA rejects atomically");
#ifdef _WIN32
    HANDLE lock=CreateFileW(a->descriptorPath.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(lock!=INVALID_HANDLE_VALUE,"lock descriptor against replacement");const bool saved=app.SaveProjectSettings(active->startupScene,low,&error);CloseHandle(lock);
    Check(!saved&&app.GetProject()==active&&Read(a->descriptorPath)==beforeDisk&&app.GetSceneSettings()==beforeScene,"atomic write failure leaves disk and active scene/project unchanged");
    for(const auto& entry:fs::directory_iterator(a->root))Check(entry.path().filename().string().find(".tmp-")==std::string::npos,"failed descriptor write retires temporary file");
#endif
    auto external=beforeDisk;external["vendor"]["external"]=true;Write(a->descriptorPath,external);
    Check(!app.SaveProjectSettings(active->startupScene,low,&error)&&app.GetProject()==active&&Read(a->descriptorPath)==external,"external descriptor change cannot be overwritten");Write(a->descriptorPath,beforeDisk);
    auto marker=app.GetEditorLayer().CreateEntity(app,"Preserved");
    for(const auto& bad:std::vector<Json>{nullptr,false,"High",1.5,-1,3,4294967296ull}){
        auto invalid=beforeDisk;invalid["settings"]["rendering"]["antiAliasing"]=bad;Write(a->root/"bad.whispproject",invalid);
        Check(!app.OpenProject(a->root/"bad.whispproject",D::Discard,&error)&&app.GetProject()==active&&app.GetWorld().IsAlive(marker)&&app.GetEffectiveAntiAliasing()==AntiAliasingMode::FxaaHigh,"invalid typed AA descriptor preserves current project");
    }
    for(const auto& bad:std::vector<Json>{nullptr,false,Json::array()}){
        auto invalid=beforeDisk;invalid["settings"]=bad;Write(a->root/"bad.whispproject",invalid);Check(!app.OpenProject(a->root/"bad.whispproject",D::Discard,&error)&&app.GetProject()==active,"settings block must be object");
        invalid=beforeDisk;invalid["settings"]["rendering"]=bad;Write(a->root/"bad.whispproject",invalid);Check(!app.OpenProject(a->root/"bad.whispproject",D::Discard,&error)&&app.GetProject()==active,"rendering block must be object");
    }
    auto malformed=scene;malformed["settings"]["antiAliasingOverride"]=0;ecs::World untouched;SceneSettings untouchedSettings;
    Check(!SceneSerializer::RestoreWorld(malformed,untouched,untouchedSettings,&error),"scene inheritance flag strictly boolean");
    Check(app.CloseProject(D::Discard,&error),"close to browser");app.NewScene();
    Check(!app.GetProject()&&app.GetSceneSettings().antiAliasingOverride&&app.GetEffectiveAntiAliasing()==AntiAliasingMode::Off,"Legacy new scenes keep original Off and explicit semantics");
    auto legacy=app.GetSceneSettings();legacy.antiAliasing=AntiAliasingMode::FxaaHigh;app.SetSceneSettings(legacy);
    Check(app.GetEffectiveAntiAliasing()==AntiAliasingMode::FxaaHigh&&!app.SaveProjectSettings("Scenes/Main.json",low,&error),"Legacy AA independent and no fake project settings");
    Check(!SceneSettingsToJson(legacy).contains("antiAliasingOverride")&&SceneSettingsFromJson(SceneSettingsToJson(legacy))==legacy,"Legacy serialization unchanged");
}
#ifdef ENABLE_DX12
static void Native(){
    Fixture fixture;auto a=ProjectManager::Create(fixture.root,"A"),b=ProjectManager::Create(fixture.root,"B");std::string error;using D=Application::ProjectDecision;
    Application app;Check(app.Initialize(Application::StartupMode::ProjectBrowser),"native initialize");
    auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow());glfwHideWindow(window->GetGlfwHandle());SyntheticEditorInput input;
    auto& ui=app.GetEditorLayer();ui.ResetLayout();Check(app.OpenProject(a->descriptorPath,D::Discard,&error),"native open project");
    Check(app.SaveProjectSettings(a->startupScene,{AntiAliasingMode::FxaaHigh},&error),"native high default");
    auto cube=ui.CreatePrimitive(app,"cube");app.GetWorld().GetComponent<ecs::TransformComponent>(cube)->position={0,0,4};
    Check(app.SaveSceneAs("Scenes/Alternative.json",false,&error),"native saved alternative startup candidate");
    ui.SetDiagnosticViewport(320,240);ui.ShowSceneBackground();ui.ShowProjectSettings(app);
    auto frame=[&](unsigned count=3){app.SetFrameLimit(count);Check(app.Run()==0,"native frames");};frame(12);
    auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());
    Check(gpu->GetAntiAliasingDiagnostics().mode==AntiAliasingMode::FxaaHigh&&gpu->GetAntiAliasingDiagnostics().intermediateBytes>0,"project default reaches actual DX12 post-process target");
    auto settingsWindow=[&]{auto* w=ImGui::FindWindowByName("Project Settings");Check(w&&w->Active,"dedicated Project Settings window active");return w;};
    auto child=[&](const char* part){for(auto* w:settingsWindow()->DC.ChildWindows)if(std::string(w->Name).find(part)!=std::string::npos)return w;throw std::runtime_error("settings child missing");};
    auto activate=[&](ImGuiID id){ImGui::ActivateItemByID(id);frame();};
    auto category=[&](const char* name){activate(child("Categories")->GetID(name));};
    auto rootButton=[&](const char* name){activate(settingsWindow()->GetID(name));};
    Check(!ImGui::FindWindowByName("Project Browser"),"Project Settings does not reopen Project Browser");
    activate(child("Project details")->GetID("Use current scene"));Check(ui.ProjectSettingsDirty()&&app.GetProject()->startupScene=="Scenes/Main.json","startup draft does not apply before Save");
    rootButton("Save");Check(app.GetProject()->startupScene=="Scenes/Alternative.json"&&!ui.ProjectSettingsDirty(),"UI startup Save uses validated descriptor update");
    const fs::path output="../../diagnostics/project-settings";fs::create_directories(output);
    app.SetFrameCaptureOutput((output/"general.ppm").string());frame();app.SetFrameCaptureOutput({});
    category("Rendering");const auto scene=app.GetSceneSettings();const auto history=ui.UndoCount();
    activate(child("Project details")->GetID("FXAA Low"));Check(ui.ProjectSettingsDirty()&&ui.HasProjectEdits()&&app.GetProject()->settings.antiAliasing==AntiAliasingMode::FxaaHigh,"rendering draft stays unpublished");
    Check((settingsWindow()->Flags&ImGuiWindowFlags_UnsavedDocument)!=0,"unsaved project indication");rootButton("Save");
    Check(app.GetProject()->settings.antiAliasing==AntiAliasingMode::FxaaLow&&!ui.ProjectSettingsDirty()&&gpu->GetAntiAliasingDiagnostics().mode==AntiAliasingMode::FxaaLow,"UI Save applies AA to actual DX12 renderer");
    Check(app.GetSceneSettings()==scene&&ui.UndoCount()==history&&!ui.IsSceneDirty(),"project save does not alter scene/history");
    app.SetFrameCaptureOutput((output/"rendering.ppm").string());frame();app.SetFrameCaptureOutput({});
    activate(child("Project details")->GetID("Off"));rootButton("Cancel");Check(!ui.ProjectSettingsDirty()&&app.GetEffectiveAntiAliasing()==AntiAliasingMode::FxaaLow,"UI Cancel discards without applying");
    ui.ShowSceneBackground();frame();auto* sceneWindow=ImGui::FindWindowByName("Scene Settings");Check(sceneWindow&&sceneWindow->Active,"Scene Settings active");
    activate(sceneWindow->GetID("Scene Override##AntiAliasing"));Check(app.GetSceneSettings().antiAliasingOverride&&app.GetEffectiveAntiAliasing()==AntiAliasingMode::FxaaLow,"scene UI enables an explicit override without visual jump");
    ui.UndoEdit(app);frame();Check(!app.GetSceneSettings().antiAliasingOverride,"scene inheritance change Undo");ui.RedoEdit(app);frame();Check(app.GetSceneSettings().antiAliasingOverride,"scene inheritance change Redo");ui.UndoEdit(app);frame();
    ui.ShowProjectSettings(app);frame();category("Rendering");activate(child("Project details")->GetID("FXAA High"));
    ui.RequestProjectOpen(app,AssetPaths::ToUtf8String(b->descriptorPath));frame();auto* popup=ImGui::FindWindowByName("Unsaved project changes");Check(popup&&popup->Active,"switch detects unsaved project draft");activate(popup->GetID("Cancel"));
    Check(app.GetProject()->root==a->root&&ui.ProjectSettingsDirty(),"switch Cancel preserves project draft");
    ui.RequestProjectOpen(app,AssetPaths::ToUtf8String(b->descriptorPath));frame();popup=ImGui::FindWindowByName("Unsaved project changes");activate(popup->GetID("Discard"));
    Check(app.GetProject()->root==b->root&&!ui.ProjectSettingsDirty()&&app.GetEffectiveAntiAliasing()==AntiAliasingMode::Off,"switch Discard clears draft without leaking AA");
    Check(ProjectManager::Prepare(a->descriptorPath)->settings.antiAliasing==AntiAliasingMode::FxaaLow,"discard did not persist pending high mode");
    Check(app.OpenProject(a->descriptorPath,D::Discard,&error),"reopen A");ui.ShowProjectSettings(app);frame();category("Rendering");activate(child("Project details")->GetID("FXAA High"));
    ui.RequestProjectOpen(app,AssetPaths::ToUtf8String(b->descriptorPath));frame();popup=ImGui::FindWindowByName("Unsaved project changes");activate(popup->GetID("Save and continue"));
    Check(app.GetProject()->root==b->root&&ProjectManager::Prepare(a->descriptorPath)->settings.antiAliasing==AntiAliasingMode::FxaaHigh,"switch Save persists draft then opens B");
    Check(gpu->GetValidationErrorCount()==0,"DX12 validation ERROR/CORRUPTION zero");app.Shutdown();
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{
#ifdef ENABLE_DX12
    if(argc>1&&std::string(argv[1])=="--dx12")Native();else
#endif
    Cpu();std::cout<<"Project Settings: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
