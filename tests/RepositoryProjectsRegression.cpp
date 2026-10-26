#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "scene/SceneSerializer.h"
#include "ecs/components/SceneIdentityComponent.h"
#include <fstream>
#include <iostream>
#if defined(ENABLE_DX12)
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#endif
namespace fs=std::filesystem;
static unsigned checks=0;
static void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);++checks;}
static std::string Bytes(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static fs::path Locate(const std::string& relative){
    for(auto base:{AssetPaths::GetExecutableDirectory(),fs::current_path()})for(;!base.empty();){auto file=base/"test_projects"/relative;if(fs::is_regular_file(file))return file;auto parent=base.parent_path();if(parent==base)break;base=parent;}
    throw std::runtime_error("Cannot locate standalone test project: "+relative);
}
struct Files {
    fs::path root=AssetPaths::GetExecutableDirectory()/("repository-projects-"+ecs::NewSceneIdentity());
    std::vector<fs::path> descriptors;
    std::map<fs::path,std::string> originals;
    std::shared_ptr<const ProjectContext> previous=ProjectManager::Active();
    Files(){
        Check(fs::create_directory(root),"owned fixture root");
        for(auto relative:{"interactive_room/InteractiveRoom.whispproject","legacy_workspace/LegacyWorkspace.whispproject"}){
            auto source=Locate(relative),target=root/source.parent_path().filename();fs::create_directory(target);
            for(const auto& entry:fs::recursive_directory_iterator(source.parent_path()))if(entry.is_regular_file()){
                const auto extension=entry.path().extension();if(extension==".wmesh"||extension==".wtex")continue;
                auto file=target/fs::relative(entry.path(),source.parent_path());fs::create_directories(file.parent_path());fs::copy_file(entry.path(),file);
                originals[entry.path()]=Bytes(entry.path());Check(Bytes(file)==originals.at(entry.path()),"portable copy bytes");
            }
            // Each project is moved independently, never opened using its sibling's path.
            auto relocated=root/(source.parent_path().filename().string()+"-moved");
            Check(relocated.lexically_normal().parent_path()==root,"move stays within owned root");fs::rename(target,relocated);descriptors.push_back(relocated/source.filename());
        }
    }
    void Verify(){for(const auto& [path,data]:originals)Check(Bytes(path)==data,"source project bytes unchanged: "+AssetPaths::ToUtf8String(path));}
    ~Files(){ProjectManager::Activate(previous);std::error_code e;fs::remove_all(root,e);}
};
static void CheckReferences(const nlohmann::json& value,const ProjectContext& project){
    if(value.is_object()||value.is_array()){for(const auto& child:value)CheckReferences(child,project);return;}
    if(!value.is_string())return;auto key=value.get<std::string>();
    for(auto prefix:{"models/","materials/","textures/","scripts/","prefabs/","ui/","environments/","postfx/"})if(key.starts_with(prefix)){Check(fs::is_regular_file(project.ResolveAsset(key)),"project-local dependency: "+key);break;}
}
static void Cpu(Files& files){
    for(const auto& descriptor:files.descriptors){auto project=ProjectManager::Prepare(descriptor);ProjectManager::Activate(project);Check(project->AssetRoot()==project->root/"Assets","project assets resolve locally after move");
        for(auto folder:{project->SceneRoot(),project->AssetRoot()})for(const auto& entry:fs::recursive_directory_iterator(folder))if(entry.is_regular_file()&&entry.path().extension()==".json"){
            std::ifstream f(entry.path());nlohmann::json document;f>>document;CheckReferences(document,*project);
        }
        ecs::World world;SceneSettings settings;std::string error;Check(SceneSerializer::LoadWorld(project->ResolveScene(project->startupScene),world,settings,&error),error);Check(world.GetAliveCount()>0,"startup World populated");auto before=SceneSerializer::CaptureWorld(world,settings);
        auto roundtrip=files.root/(project->name+"-roundtrip.json");Check(SceneSerializer::SaveWorld(roundtrip,world,&error,&settings),error);ecs::World loaded;SceneSettings loadedSettings;Check(SceneSerializer::LoadWorld(roundtrip,loaded,loadedSettings,&error),error);Check(SceneSerializer::CaptureWorld(loaded,loadedSettings)==before,"scene serialization preserved");Check(fs::is_regular_file(project->ResolveScene(project->maps.gameStartupMap)),"Game Startup Map resolves locally");
    }
    files.Verify();
}
#if defined(ENABLE_DX12)
static void Native(Files& files){
    Application app;Check(app.Initialize(Application::StartupMode::ProjectBrowser),"DX12 project browser initializes without demo load");SyntheticEditorInput input;glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());auto& ui=app.GetEditorLayer();ui.SetFixedDiagnosticLayout(true);ui.SetDiagnosticViewport(360,240);auto frame=[&](unsigned n=6){app.SetFrameLimit(n);Check(app.Run()==0,"real editor frames");};std::string error;ecs::Entity previousHandle;
    for(const auto& descriptor:files.descriptors){
        Check(app.OpenProject(descriptor,Application::ProjectDecision::Discard,&error),error);frame(12);Check(app.GetSceneKey()==app.GetProject()->startupScene,"Editor Startup Map opened");Check(!app.GetWorld().IsAlive(previousHandle),"project switch rejects old handle");auto before=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());
        auto camera=ui.CreateCamera(app);ui.SetCameraToolsVisible(true,true,true);ui.SelectEntity(camera);frame(12);
        auto* inspector=ImGui::FindWindowByName("Inspector");Check(inspector&&inspector->Active,"actual Inspector");ImGui::SetWindowDock(inspector,0,ImGuiCond_Always);ImGui::SetWindowPos(inspector,{650,50});ImGui::SetWindowSize(inspector,{500,850});frame(20);
        auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(gpu&&gpu->GetCameraPreviewTextureId()!=0,"Camera Preview in relocated project");ui.UndoEdit(app);frame();Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==before,"camera editor Undo preserves project scene");
        app.SetEditorPlayMode(true);Check(app.IsEditorPlayMode(),app.GetPlayError());frame(8);app.SetEditorPlayMode(false);frame();Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==before,"Play Stop restores authored scene");
        Check(app.PlayFromGameStartupMap(),app.GetPlayError());frame(8);app.SetEditorPlayMode(false);Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==before,"Game Startup Map Play Stop restores authored scene");previousHandle=app.GetWorld().Entities().front();
        Check(gpu->GetValidationErrorCount()==0,"zero DX12 validation errors");
    }
    Check(app.CloseProject(Application::ProjectDecision::Discard,&error),error);app.Shutdown();files.Verify();
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{Files files;
#if defined(ENABLE_DX12)
    if(argc>1&&std::string(argv[1])=="--dx12")Native(files);else
#endif
    Cpu(files);std::cout<<"Repository Projects: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Repository Projects failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
