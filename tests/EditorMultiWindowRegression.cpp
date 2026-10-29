#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#include "EditorTestPreferences.h"
#include "ApplicationTestAccess.h"
#include "SyntheticEditorInput.h"
#include "core/AssetPaths.h"
#include "ecs/components/CameraComponent.h"
#include "ecs/components/LuaScriptComponent.h"
#include "ecs/components/UICanvasComponent.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "scene/SceneSerializer.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "render/backends/vulkan/VkRenderAdapter.h"
#include "platform/GlfwWindow.h"
#include <GLFW/glfw3native.h>
#include <imgui_internal.h>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
using namespace ecs;
namespace editor {struct EditorDocumentTestAccess {static int Tool(const EditorLayer& e){return e.m_GizmoOperation;}};}
struct ShutdownApp {Application* app;~ShutdownApp(){if(app)app->Shutdown();}};
static unsigned checks=0;
static void Check(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
static double Field(Application& app,Entity e,const std::string& key){for(const auto& v:app.GetLuaRuntime().Inspect(e).fields)if(v.name==nlohmann::json(key).dump())return std::stod(v.value);throw std::runtime_error("Lua field missing: "+key);}
static Entity Tagged(World& w,const char* tag){for(auto e:w.Entities())if(auto* t=w.GetComponent<TagComponent>(e);t&&t->name==tag)return e;return {};}
static void Image(const fs::path& path,const std::vector<unsigned char>& pixels,unsigned w,unsigned h){Check(pixels.size()==size_t(w)*h*4,"GPU image size");std::ofstream out(path,std::ios::binary);out<<"P6\n"<<w<<' '<<h<<"\n255\n";for(size_t i=0;i<pixels.size();i+=4)out.write(reinterpret_cast<const char*>(pixels.data()+i),3);Check(bool(out),"GPU image saved");}
static void Vulkan(){
#if defined(ENABLE_VULKAN)
    GlfwWindow window;Check(window.Create(320,240,"Multi-window capability / Vulkan"),"Vulkan OS window");
    VkRenderAdapter gpu;Check(!gpu.SupportsEditorPlatformWindows(),"Vulkan detached viewport capability explicitly unavailable");gpu.EnableEditorPlatformWindows();Check(!gpu.SupportsEditorPlatformWindows(),"Vulkan platform enable safely ignored");Check(!gpu.SupportsGameOutput(),"Vulkan safely rejects unsupported separate output");Check(gpu.Initialize(&window),"Vulkan initialize");gpu.EnableReadback();
    for(int i=0;i<4;++i){window.PollEvents();gpu.BeginFrame();gpu.Clear(.1f,.2f,.3f,1);gpu.EndFrame();gpu.Present();}
    Check(gpu.ReadBackPixels().size()==320*240*4,"Vulkan actual GPU output");Check(!gpu.OpenGameOutput(&window),"Vulkan unsupported output remains rejected");Check(gpu.GetValidationErrorCount()==0,"Vulkan validation zero");gpu.Shutdown();Check(gpu.GetValidationErrorCount()==0,"Vulkan shutdown validation zero");
#else
    throw std::runtime_error("Vulkan is not built in this configuration");
#endif
}
static void Editor(){
    ScopedEditorPreferenceRestore preserve;
    const auto directory=AssetPaths::GetExecutableDirectory()/("multiwindow-"+NewSceneIdentity());Check(fs::create_directory(directory),"owned project parent");
    struct Cleanup{fs::path p;~Cleanup(){ProjectManager::Activate({});std::error_code ec;fs::remove_all(p,ec);}} cleanup{directory};
    auto project=ProjectManager::Create(directory,"WindowLab");std::string error;
    Application app;ShutdownApp shutdown{&app};Check(app.Initialize(Application::StartupMode::ProjectBrowser),"real editor initialize");SyntheticEditorInput editorInput;ApplicationTestAccess::FrameDelta(app,1.f/60);
    Check(app.OpenProject(project->descriptorPath,Application::ProjectDecision::Discard,&error),error);
    auto& editor=app.GetEditorLayer();editor.SetFixedDiagnosticLayout(true);editor.SetDiagnosticViewport(480,270);editor.SetGameView(true);editor.SetGridVisible(false);
    auto frame=[&](unsigned n=4){app.SetFrameLimit(n);Check(app.Run()==0,"native frames");};
    auto* main=static_cast<GlfwWindow*>(app.GetWindow());auto* renderer=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(renderer,"DX12 adapter");
    {GlfwWindow invalid;Check(!invalid.Create(0,0,"Expected invalid fixture"),"failed secondary GLFW create");}frame();Check(IsWindow(glfwGetWin32Window(main->GetGlfwHandle())),"failed creation does not terminate primary GLFW owner");
    auto settings=app.GetSceneSettings();settings.lighting.enabled=false;settings.environment.enabled=false;settings.environment.exposure=0;settings.environment.toneMapping=true;settings.antiAliasing=AntiAliasingMode::FxaaHigh;settings.background.background=EditorBackground::Solid;settings.background.solid={.05f,.1f,.2f};Check(app.SetSceneSettings(settings),"fixed output settings");
    auto cube=editor.CreatePrimitive(app,"cube");app.GetWorld().GetComponent<TransformComponent>(cube)->position={0,0,0};
    auto camera=editor.CreateCamera(app);app.GetWorld().GetComponent<TagComponent>(camera)->name="GameCamera";app.GetWorld().GetComponent<TransformComponent>(camera)->position={0,0,-4};
    auto doc=ui::EmptyDocument();doc.name="WindowHUD";doc.referenceResolution={640,360};ui::Widget button;button.id="Action";button.parent="Root";button.type="Button";button.position={20,20};button.size={100,60};button.color={1,0,0,1};button.hoverColor={0,1,0,1};button.pressedColor={0,0,1,1};doc.widgets.push_back(button);
    Check(UILoader::Save("ui/window.ui.json",doc,&error),error);auto canvas=editor.CreateUICanvas(app,"ui/window.ui.json");app.GetWorld().GetComponent<TagComponent>(canvas)->name="Probe";
    std::ofstream(project->ResolveAsset("scripts/window.lua"))<<R"(return {
      on_start=function(self) self.starts=1;self.ticks=0;self.moves=0;self.fire=0;self.clicks=0
        Camera.set_active(World.find_by_tag('GameCamera')[1]);Input.bind('ProbeMove','W');Input.bind('ProbeFire','MouseLeft');ui.set_input_mode('GameAndUI') end,
      on_update=function(self,dt) self.ticks=self.ticks+1;if Input.was_pressed('ProbeMove') then self.moves=self.moves+1 end;if Input.was_pressed('ProbeFire') then self.fire=self.fire+1 end end,
      on_ui_event=function(self,e) if e.type=='click' then self.clicks=self.clicks+1 end end})";
    auto& script=app.GetWorld().AddComponent<LuaScriptComponent>(canvas);script.scriptPath="scripts/window.lua";script.cameraEnabled=script.inputEnabled=true;
    Check(SceneSerializer::SaveWorld(project->ResolveScene(project->startupScene),app.GetWorld(),&error,&settings),error);
    Check(app.OpenScene(project->startupScene,&error),error);editor.CreateEntity(app,"EditorOnly");editor.ClearSelection();editor.SetGameView(true);
    Check(app.SetEditorCameraPose({7,2,-7},0,0),"different authored editor camera");frame(16);
    const auto authored=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());app.SetStandaloneWindowSize(640,360);
    auto press=[&](ImGuiWindow* w,const char* label){Check(w&&w->Active,"active native editor control");ImGui::ActivateItemByID(ImHashStr(label,0,w->ID));frame(2);};
    auto* toolbar=ImGui::FindWindowByName("Main Toolbar");press(toolbar,"Play mode");Check(!GImGui->OpenPopupStack.empty(),"Play dropdown opens");press(GImGui->OpenPopupStack.back().Window,"New Window");press(toolbar,"Play");frame(20);
    Check(app.IsEditorPlayMode()&&app.IsStandalonePlay(),"Play dropdown starts standalone Runtime World");
    Check(!Tagged(app.GetWorld(),"EditorOnly").IsValid(),"Game Startup map used rather than unsaved editor scene");
    auto* game=dynamic_cast<GlfwWindow*>(app.GetStandaloneGameWindow());Check(game&&game!=main,"distinct native Game window");auto* native=game->GetGlfwHandle();auto hwnd=glfwGetWin32Window(native);Check(IsWindow(hwnd)&&hwnd!=glfwGetWin32Window(main->GetGlfwHandle()),"distinct live HWND");
    Check(app.GetRenderCameraState().entity==Tagged(app.GetWorld(),"GameCamera"),"active Game camera selected");
    Check(app.GetRenderStatistics().visibleSubmeshes>0,"real scene geometry visible in Game camera");
    auto probe=Tagged(app.GetWorld(),"Probe");Check(Field(app,probe,"starts")==1&&app.GetLuaRuntime().InstanceCount()==1,"one Lua instance / one Runtime start");auto instance=app.GetLuaRuntime().InspectionInfo(probe);double ticks=Field(app,probe,"ticks");frame(5);Check(Field(app,probe,"ticks")==ticks+5,"Runtime updates exactly once per frame");Check(app.GetLuaRuntime().InspectionInfo(probe).SameInstance(instance),"same runtime session across frames");
    const auto output=AssetPaths::GetExecutableDirectory()/"diagnostics/editor33";fs::create_directories(output);std::ofstream windows(output/"windows.csv");windows<<"name,hwnd,left,top,right,bottom\n";
    for(auto pair:{std::pair{"Editor",glfwGetWin32Window(main->GetGlfwHandle())},std::pair{"Game",hwnd}}){RECT r{};GetWindowRect(pair.second,&r);windows<<pair.first<<','<<reinterpret_cast<uintptr_t>(pair.second)<<','<<r.left<<','<<r.top<<','<<r.right<<','<<r.bottom<<'\n';}
    Check(renderer->ReadBackGameOutputPixels()==renderer->ReadBackViewportPixels(),"Game backbuffer exactly equals final SDR + Runtime UI source");auto captureTime=fs::file_time_type::clock::now();app.SetFrameCaptureOutput((output/"editor-presented.ppm").string());frame(1);app.SetFrameCaptureOutput({});Check(fs::last_write_time(output/"editor-presented.ppm")>=captureTime,"Editor GPU frame captured before Present");Image(output/"game.ppm",renderer->ReadBackGameOutputPixels(),640,360);
    auto focus=[&](GLFWwindow* w){glfwFocusWindow(w);frame();Check(glfwGetWindowAttrib(w,GLFW_FOCUSED)==GLFW_TRUE,"actual native keyboard focus");};focus(native);
    auto key=[&](int code,bool down){auto scan=MapVirtualKeyW(code,MAPVK_VK_TO_VSC);PostMessageW(hwnd,down?WM_KEYDOWN:WM_KEYUP,code,LPARAM(1|(scan<<16)|(down?0:0xc0000000)));frame(2);};
    key('W',true);key('W',false);Check(Field(app,probe,"moves")==1,"native Game keyboard reaches Lua");
    editor.SetGizmoOperation(2);key('E',true);key('E',false);Check(editor::EditorDocumentTestAccess::Tool(editor)==2,"Game E does not activate Editor Rotate shortcut");
    focus(main->GetGlfwHandle());key('W',true);key('W',false);Check(Field(app,probe,"moves")==1,"unfocused Game keyboard rejected without globally blocking Editor");focus(native);
    auto point=[&](int x,int y){glfwSetCursorPos(native,x,y);SendMessageW(hwnd,WM_MOUSEMOVE,0,MAKELPARAM(x,y));TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT),TME_CANCEL|TME_LEAVE,hwnd,0};TrackMouseEvent(&tracking);MSG msg{};while(PeekMessageW(&msg,hwnd,WM_MOUSELEAVE,WM_MOUSELEAVE,PM_REMOVE)){};frame();auto p=game->PointerPosition();Check(std::abs(p[0]-x)<1&&std::abs(p[1]-y)<1,"actual Game pointer remains at requested client coordinates");};
    auto click=[&](int x,int y){point(x,y);PostMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));frame(2);PostMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(x,y));frame(2);};
    click(40,40);auto pointer=game->PointerPosition();std::cout<<"Game UI diagnostic: clicks="<<Field(app,probe,"clicks")<<" fire="<<Field(app,probe,"fire")<<" pointer="<<pointer[0]<<','<<pointer[1]<<" mode="<<app.GetUIInputMode()<<" focus="<<glfwGetWindowAttrib(native,GLFW_FOCUSED)<<" available="<<ApplicationTestAccess::GameplayHost(app).inputAllowed()<<std::endl;Check(Field(app,probe,"clicks")==1&&Field(app,probe,"fire")==0,"native Game UI click consumed before gameplay");click(600,300);Check(Field(app,probe,"fire")==1,"native Game background click reaches gameplay");
    int monitorCount=0;auto** monitors=glfwGetMonitors(&monitorCount);Check(monitorCount>0,"native monitor inventory");std::ofstream monitorData(output/"game-monitors.csv");monitorData<<"index,work_x,work_y,window_x,window_y,framebuffer_width,framebuffer_height,scale_x,scale_y\n";
    for(int i=0;i<monitorCount;++i){
        int x=0,y=0,w=0,h=0;glfwGetMonitorWorkarea(monitors[i],&x,&y,&w,&h);glfwSetWindowPos(native,x+80,y+80);frame(8);focus(native);int wx=0,wy=0;glfwGetWindowPos(native,&wx,&wy);Check(wx>=x&&wy>=y&&wx<x+w&&wy<y+h,"Game window moved onto enumerated monitor");
        auto clicks=Field(app,probe,"clicks");click(40,40);Check(Field(app,probe,"clicks")==clicks+1&&Field(app,probe,"fire")==1,"Game Runtime UI coordinates remain correct after monitor move");Check(app.GetLuaRuntime().InspectionInfo(probe).SameInstance(instance),"monitor move retains original Runtime instance");
        int fw=0,fh=0;float sx=0,sy=0;glfwGetFramebufferSize(native,&fw,&fh);glfwGetWindowContentScale(native,&sx,&sy);Check(renderer->ReadBackGameOutputPixels()==renderer->ReadBackViewportPixels(),"monitor move Game GPU output matches source");Image(output/("game-monitor-"+std::to_string(i)+".ppm"),renderer->ReadBackGameOutputPixels(),unsigned(fw),unsigned(fh));monitorData<<i<<','<<x<<','<<y<<','<<wx<<','<<wy<<','<<fw<<','<<fh<<','<<sx<<','<<sy<<'\n';
    }
    auto host=ApplicationTestAccess::GameplayHost(app);Check(host.captureCursor(true)&&glfwGetInputMode(native,GLFW_CURSOR)==GLFW_CURSOR_DISABLED,"actual Game cursor capture");Check(glfwGetInputMode(main->GetGlfwHandle(),GLFW_CURSOR)==GLFW_CURSOR_NORMAL,"Editor cursor remains independent");key(VK_TAB,true);key(VK_TAB,false);Check(!app.IsGameplayMouseCaptured()&&glfwGetInputMode(native,GLFW_CURSOR)==GLFW_CURSOR_NORMAL,"Tab releases Game capture");
    Check(host.captureCursor(true),"capture before native focus loss");focus(main->GetGlfwHandle());Check(!app.IsGameplayMouseCaptured(),"focus loss releases Game capture");focus(native);
    for(auto size:{std::pair{320,240},std::pair{800,450},std::pair{640,360}}){glfwSetWindowSize(native,size.first,size.second);frame(8);int w=0,h=0;glfwGetFramebufferSize(native,&w,&h);Check(renderer->GameOutputSize()==std::array<unsigned,2>{unsigned(w),unsigned(h)},"native resize reaches separate swapchain");Check(renderer->ReadBackGameOutputPixels()==renderer->ReadBackViewportPixels(),"resized Game pixels match source");}
    glfwIconifyWindow(native);frame();Check(app.IsStandalonePlay(),"minimize retains one running session");glfwRestoreWindow(native);focus(native);frame(8);Check(renderer->ReadBackGameOutputPixels()==renderer->ReadBackViewportPixels(),"restore resumes valid Game output");
    Check(host.captureCursor(true),"capture before native window close");PostMessageW(hwnd,WM_CLOSE,0,0);frame(6);Check(!app.IsEditorPlayMode()&&!app.IsStandalonePlay()&&!IsWindow(hwnd),"native Close stops Play and destroys HWND");Check(!app.IsGameplayMouseCaptured(),"close releases capture");Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==authored,"Close restores exact authored World");
    for(int i=0;i<2;++i){press(toolbar,"Play");frame(12);Check(app.IsStandalonePlay(),"repeat standalone Play");focus(main->GetGlfwHandle());press(toolbar,"Stop");frame();Check(!app.IsStandalonePlay()&&!app.IsEditorPlayMode(),"native Editor Stop closes Game window");Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==authored,"Stop restores authored World");}
    Check(editor.PersistPreferences(app),editor.GetError());editor::EditorPreferences p;Check(editor::LoadPreferences(editor::PreferencesPath(),p,&error),error);Check(p.gameWindowWidth==640&&p.gameWindowHeight==360&&p.playInNewWindow,"Game size/mode persisted as Editor Preferences");
    Check(renderer->GetValidationErrorCount()==0,"DX12 validation zero");app.Shutdown();shutdown.app=nullptr;
    ProjectManager::Activate({});Application restarted;ShutdownApp shutdownRestart{&restarted};Check(restarted.Initialize(Application::StartupMode::ProjectBrowser),"fresh Application/device initialize");Check(restarted.StandaloneWindowSize()==std::array<int,2>{640,360},"fresh editor restores Game size preferences");Check(restarted.OpenProject(project->descriptorPath,Application::ProjectDecision::Discard,&error),error);Check(restarted.RequestPlayInNewWindow(),"fresh editor standalone request");restarted.SetFrameLimit(16);Check(restarted.Run()==0&&restarted.IsStandalonePlay(),"fresh device renders standalone Game");auto* newGpu=dynamic_cast<Dx12RenderAdapter*>(restarted.GetRenderAdapter());Check(newGpu->ReadBackGameOutputPixels()==newGpu->ReadBackViewportPixels(),"fresh device Game GPU output");restarted.SetEditorPlayMode(false);Check(restarted.OpenProject(project->descriptorPath,Application::ProjectDecision::Discard,&error),error);Check(newGpu->GetValidationErrorCount()==0,"fresh device validation zero");restarted.Shutdown();shutdownRestart.app=nullptr;
}
static void ExistingProject(const fs::path& descriptor){
    ScopedEditorPreferenceRestore preserve;auto project=ProjectManager::Prepare(descriptor);
    auto relative=fs::relative(project->root,AssetPaths::GetExecutableDirectory()/"project-copies");
    Check(!relative.empty()&&!relative.is_absolute()&&*relative.begin()!="..","external project scenario requires an isolated executable-relative copy");
    Application app;ShutdownApp shutdown{&app};Check(app.Initialize(Application::StartupMode::ProjectBrowser),"project editor initialize");SyntheticEditorInput input;ApplicationTestAccess::FrameDelta(app,1.f/60);std::string error;Check(app.OpenProject(project->descriptorPath,Application::ProjectDecision::Discard,&error),error);app.GetEditorLayer().SetDiagnosticViewport(480,270);app.SetStandaloneWindowSize(640,360);
    auto frames=[&](unsigned n=4){app.SetFrameLimit(n);Check(app.Run()==0,"project native frames");};frames(12);auto authored=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());auto identity=app.GetWorld().GetIdentity();
    Check(app.RequestPlayInNewWindow(),app.GetPlayError());frames(90);Check(app.IsStandalonePlay()&&app.GetWorld().GetIdentity()!=identity,"project isolated Runtime World");
    Entity controller;std::string script;for(auto e:app.GetWorld().Entities())if(auto* c=app.GetWorld().GetComponent<LuaScriptComponent>(e);c&&c->scriptPath.ends_with("FirstPersonController.lua")){controller=e;script=c->scriptPath;break;}
    Check(controller.IsValid(),"DOOM67 actual FirstPersonController instance");Check(app.GetLuaRuntime().Error(script).empty(),app.GetLuaRuntime().Error(script));
    auto* game=static_cast<GlfwWindow*>(app.GetStandaloneGameWindow());auto* native=game->GetGlfwHandle();auto hwnd=glfwGetWin32Window(native);auto* main=static_cast<GlfwWindow*>(app.GetWindow());auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());
    glfwFocusWindow(native);frames();Check(glfwGetWindowAttrib(native,GLFW_FOCUSED),"DOOM67 Game focus");glfwSetCursorPos(native,320,180);frames();
    auto click=[&]{PostMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(320,180));frames(1);PostMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(320,180));frames(1);};
    click();Check(app.IsGameplayMouseCaptured(),"DOOM67 native click captures Game cursor");frames(40);double before=Field(app,controller,"weapon_recoil");click();double shot=Field(app,controller,"weapon_recoil");std::cout<<"DOOM67 weapon_recoil before="<<before<<" after native shot="<<shot<<std::endl;Check(shot>before+.1,"DOOM67 actual weapon module fires from native Game mouse");
    Check(app.GetRenderStatistics().visibleSubmeshes>0&&gpu->ReadBackGameOutputPixels()==gpu->ReadBackViewportPixels(),"DOOM67 geometry + final SDR/UI Game GPU output");auto output=AssetPaths::GetExecutableDirectory()/"diagnostics/editor33";fs::create_directories(output);auto captureTime=fs::file_time_type::clock::now();app.SetFrameCaptureOutput((output/"doom67-editor-presented.ppm").string());frames(1);app.SetFrameCaptureOutput({});Check(fs::last_write_time(output/"doom67-editor-presented.ppm")>=captureTime,"DOOM67 Editor GPU frame captured before Present");Image(output/"doom67-game.ppm",gpu->ReadBackGameOutputPixels(),640,360);
    auto key=[&](int code,bool down){auto scan=MapVirtualKeyW(code,MAPVK_VK_TO_VSC);PostMessageW(hwnd,down?WM_KEYDOWN:WM_KEYUP,code,LPARAM(1|(scan<<16)|(down?0:0xc0000000)));frames(2);};key(VK_TAB,true);key(VK_TAB,false);Check(!app.IsGameplayMouseCaptured(),"DOOM67 Tab releases Game cursor");
    glfwFocusWindow(main->GetGlfwHandle());frames(40);Check(glfwGetWindowAttrib(main->GetGlfwHandle(),GLFW_FOCUSED),"DOOM67 return focus to Editor");before=Field(app,controller,"weapon_recoil");click();Check(Field(app,controller,"weapon_recoil")<=before+.001,"DOOM67 unfocused Game cannot fire");
    glfwFocusWindow(native);frames();click();frames(40);click();Check(Field(app,controller,"weapon_recoil")>.1&&app.IsGameplayMouseCaptured(),"DOOM67 focus return restores capture and shooting");
    PostMessageW(hwnd,WM_CLOSE,0,0);frames(6);Check(!app.IsEditorPlayMode()&&!app.IsStandalonePlay()&&!IsWindow(hwnd),"DOOM67 Close terminates Play");Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==authored,"DOOM67 copied project's authored World restored");Check(gpu->GetValidationErrorCount()==0,"DOOM67 DX12 validation zero");app.Shutdown();shutdown.app=nullptr;
}
int main(int argc,char** argv){try{if(argc>2&&std::string(argv[1])=="--project")ExistingProject(fs::u8path(argv[2]));else if(argc>1&&std::string(argv[1])=="--vulkan")Vulkan();else Editor();std::cout<<"EditorMultiWindow: "<<checks<<" checks PASS\n";return 0;}catch(const std::exception& e){std::cerr<<"MULTIWINDOW FAILED: "<<e.what()<<'\n';return 1;}}
