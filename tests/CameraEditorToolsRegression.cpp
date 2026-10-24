#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "editor/CameraEditorMath.h"
#include "editor/LightGizmoMath.h"
#include "editor/EditorPreferences.h"
#include "ecs/ComponentRegistry.h"
#include "ecs/components/PlayerComponent.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "scene/SceneSerializer.h"
#include "scripting/Prefab.h"
#include <fstream>
#include <iostream>
#if defined(ENABLE_DX12)
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#endif
using namespace ecs;
static unsigned checks=0;
static void Check(bool value,const std::string& text){if(!value)throw std::runtime_error(text);++checks;}
static void Near(float a,float b,const char* text){Check(std::abs(a-b)<.004f,text);}
static Entity Node(World& w,Vec3 position={}){auto e=w.CreateEntity();w.AddComponent<TransformComponent>(e).position=position;return e;}
static void Cpu(){
    World w;auto parent=Node(w,{1,2,3}),camera=Node(w,{.5f,1,.2f});w.AddComponent<CameraComponent>(camera);Check(w.SetParent(camera,parent,false),"camera hierarchy");
    for(int projection=0;projection<2;++projection)for(float aspect:{.6f,1.f,16.f/9.f,3.f})for(int rotation=0;rotation<16;++rotation){
        auto& p=*w.GetComponent<TransformComponent>(parent);p.rotation={rotation*.07f,rotation*.11f,rotation*.13f};p.scale=rotation%2?Vec3{-2,-2,-2}:Vec3{3,3,3};
        w.GetComponent<TransformComponent>(camera)->rotation={-.5f,rotation*.03f,.3f};auto& c=*w.GetComponent<CameraComponent>(camera);c.projection=CameraProjection(projection);c.fieldOfView=.3f+rotation*.1f;c.nearClip=.2f;c.farClip=20;c.orthographicSize=6;
        auto state=ResolveCameraState(w,camera,aspect);auto frustum=editor::BuildCameraFrustum(state);Check(!frustum.shortened,"ordinary far plane exact");
        for(int j=0;j<8;++j){auto v=frustum.corners[j];float input[4]={v.x,v.y,v.z,1},clip[4]{};for(int row=0;row<4;++row)for(int k=0;k<4;++k)clip[row]+=state.viewProjection[k*4+row]*input[k];
            Near(clip[0]/clip[3],j%4==0||j%4==3?-1.f:1.f,"frustum horizontal NDC");Near(clip[1]/clip[3],j%4<2?-1.f:1.f,"frustum vertical NDC");Near(clip[2]/clip[3],j<4?0.f:1.f,"near/far depth NDC");}
        c.enabled=false;auto disabled=BuildCameraState(ResolveCameraPose(w,camera),c,aspect);auto outline=editor::BuildCameraFrustum(disabled);Near(outline.corners[0].x,frustum.corners[0].x,"disabled camera still visualizable");c.enabled=true;
    }
    auto& c=*w.GetComponent<CameraComponent>(camera);c.farClip=1000000;c.projection=CameraProjection::Perspective;
    for(float fov:{.017453293f,3.1241394f})for(float nearDistance:{.001f,999999.f}){c.fieldOfView=fov;c.nearClip=nearDistance;auto state=ResolveCameraState(w,camera,2);auto f=editor::BuildCameraFrustum(state);Check(f.shortened,"display length explicitly limited");for(auto p:f.corners)Check(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z),"extreme valid frustum finite");Check(c.farClip==1000000,"display cap never edits component");}
    CameraComponent ortho;ortho.projection=CameraProjection::Orthographic;for(float span:{.001f,100000.f}){ortho.orthographicSize=span;auto f=editor::BuildCameraFrustum(BuildCameraState({},ortho,2));Near(f.corners[3].y-f.corners[0].y,span,"orthographic full vertical span");}
    auto stale=camera;w.DestroyEntity(camera);bool failed=false;try{ResolveCameraPose(w,stale);}catch(const std::exception&){failed=true;}Check(failed,"deleted camera rejected");
    editor::EditorPreferences prefs;prefs.cameraGizmos=false;prefs.cameraFrustum=false;prefs.cameraPreview=false;auto decoded=editor::PreferencesFromJson(editor::PreferencesToJson(prefs));Check(!decoded.cameraGizmos&&!decoded.cameraFrustum&&!decoded.cameraPreview,"editor tools preference roundtrip");auto old=editor::PreferencesFromJson(nlohmann::json{{"version",1}});Check(old.cameraGizmos&&old.cameraFrustum&&old.cameraPreview,"old preferences retain defaults");
}
#if defined(ENABLE_DX12)
static double Difference(const std::vector<std::uint8_t>& a,const std::vector<std::uint8_t>& b){Check(!a.empty()&&a.size()==b.size(),"same-sized real readback");double value=0;for(size_t i=0;i<a.size();++i)value+=std::abs(int(a[i])-int(b[i]));return value;}
struct OwnedFiles {
    std::string token="camera_tools_"+NewSceneIdentity();std::filesystem::path root=AssetPaths::ResolveAssetRoot(),scene=root/"scenes"/(token+".json"),prefab=root/"prefabs"/(token+".prefab.json"),projectParent=AssetPaths::GetExecutableDirectory()/token;
    ~OwnedFiles(){std::error_code error;std::filesystem::remove(scene,error);std::filesystem::remove(prefab,error);std::filesystem::remove_all(projectParent,error);}
};
static void Native(){
    OwnedFiles files;Application app;Check(app.Initialize(),"native DX12 initialize");
    {
    SyntheticEditorInput input;glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());app.NewScene();auto& ui=app.GetEditorLayer();ui.SetFixedDiagnosticLayout(true);ui.SetDiagnosticViewport(360,240);ui.SetGameView(false);ui.SetCameraToolsVisible(true,true,true);
    auto frame=[&](unsigned n=8){app.SetFrameLimit(n);Check(app.Run()==0,"native frames");};Check(app.SetEditorCameraPose({0,0,-12},0,0),"independent editor camera");
    auto camera=ui.CreateCamera(app);app.GetWorld().GetComponent<TransformComponent>(camera)->position={0,0,-7};auto cube=ui.CreatePrimitive(app,"cube");app.GetWorld().GetComponent<TransformComponent>(cube)->position={0,0,0};ui.SelectEntity(camera);app.SetViewportUnlit(true);auto sceneSettings=app.GetSceneSettings();sceneSettings.environment.enabled=false;sceneSettings.shadows.enabled=false;Check(app.SetSceneSettings(sceneSettings),"stable preview settings");frame();
    auto* inspector=ImGui::FindWindowByName("Inspector");Check(inspector&&inspector->Active,"actual Inspector");ImGui::SetWindowDock(inspector,0,ImGuiCond_Always);ImGui::SetWindowPos(inspector,{650,50});ImGui::SetWindowSize(inspector,{500,850});frame(20);
    auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(gpu&&gpu->GetCameraPreviewTextureId(),"real preview target from selected entity");Check(ui.GetCameraPreviewRequest().entity==camera,"selected camera preview request");auto original=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());frame(4);Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==original,"live preview leaves all authored data intact");auto editorPose=app.GetCameraPosition();auto main= gpu->ReadBackViewportPixels();auto image=gpu->ReadBackCameraPreviewPixels();Check(!image.empty(),"real preview readback");
    app.GetWorld().GetComponent<TransformComponent>(camera)->position.x=3;frame();auto moved=gpu->ReadBackCameraPreviewPixels();Check(Difference(image,moved)>100,"camera translation changes preview");Check(Difference(main,gpu->ReadBackViewportPixels())==0,"main Scene View unaffected by preview camera movement");
    app.GetWorld().GetComponent<TransformComponent>(camera)->rotation.y=-.4f;frame();auto rotated=gpu->ReadBackCameraPreviewPixels();Check(Difference(moved,rotated)>100,"rotation changes preview");
    auto& c=*app.GetWorld().GetComponent<CameraComponent>(camera);c.fieldOfView=.4f;frame();auto narrow=gpu->ReadBackCameraPreviewPixels();Check(Difference(rotated,narrow)>100,"FOV affects preview");c.farClip=2;frame();auto clipped=gpu->ReadBackCameraPreviewPixels();Check(Difference(narrow,clipped)>100,"far clip affects preview");c.farClip=2000;c.fieldOfView=1.04719755f;c.projection=CameraProjection::Orthographic;c.orthographicSize=8;frame();Check(Difference(clipped,gpu->ReadBackCameraPreviewPixels())>100,"orthographic image");auto wideOrtho=gpu->ReadBackCameraPreviewPixels();c.orthographicSize=4;frame();Check(Difference(wideOrtho,gpu->ReadBackCameraPreviewPixels())>100,"orthographic size affects image");app.GetWorld().GetComponent<TransformComponent>(camera)->position={0,7,0};app.GetWorld().GetComponent<TransformComponent>(camera)->rotation={1.5707963268f,0,0};frame();Check(Difference(wideOrtho,gpu->ReadBackCameraPreviewPixels())>100,"top-down camera image without yaw/pitch assumptions");
    auto request=ui.GetCameraPreviewRequest();ImGui::SetWindowSize(inspector,{400,850});frame();Check(ui.GetCameraPreviewRequest().width!=request.width,"preview resize tracks Inspector");Check(gpu->ReadBackCameraPreviewPixels().size()==size_t(ui.GetCameraPreviewRequest().width)*ui.GetCameraPreviewRequest().height*4,"resized target dimensions");
    auto parent=Node(app.GetWorld());app.GetWorld().GetComponent<TransformComponent>(parent)->rotation={.2f,.3f,.4f};app.GetWorld().GetComponent<TransformComponent>(parent)->scale={2,2,2};Check(app.GetWorld().SetParent(camera,parent,false),"parented camera in preview");frame();Check(gpu->GetCameraPreviewTextureId()!=0,"parent/roll preview");
    auto other=ui.DuplicateEntity(app,camera);ui.SelectEntity(other);frame();Check(ui.GetCameraPreviewRequest().entity==other,"selection switches preview");Check(ui.DeleteEntity(app,other),ui.GetError());frame();Check(gpu->GetCameraPreviewTextureId()==0,"deleted selected camera releases target");ui.SelectEntity(camera);frame();app.GetWorld().GetComponent<CameraComponent>(camera)->enabled=false;frame();Check(gpu->GetCameraPreviewTextureId()==0,"disabled preview releases target");app.GetWorld().GetComponent<CameraComponent>(camera)->enabled=true;
    Check(app.GetWorld().SetParent(camera,{},false),"unparent camera");app.GetWorld().GetComponent<TransformComponent>(camera)->position={0,0,-7};app.GetWorld().GetComponent<TransformComponent>(camera)->rotation={};app.GetWorld().GetComponent<CameraComponent>(camera)->projection=CameraProjection::Perspective;ui.SelectEntity(camera);frame();
    auto* viewport=ImGui::FindWindowByName("Viewport");Check(viewport&&viewport->Active,"Scene View");auto overlayVertices=viewport->DrawList->VtxBuffer.Size;ui.SetCameraToolsVisible(false,false,false);frame();Check(viewport->DrawList->VtxBuffer.Size<overlayVertices,"camera geometry is editor overlay");Check(gpu->GetCameraPreviewTextureId()==0,"hidden preview frees target");ui.SetCameraToolsVisible(true,true,true);ui.ClearSelection();frame();auto bounds=ui.GetViewportBounds();auto view=editor::GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),360.f/240,.1f,2000);auto screen=editor::ProjectGizmo(app.GetWorld().WorldTransform(camera).position,view);Check(screen.has_value(),"camera gizmo projects");SyntheticEditorInput::Position(bounds[0]+(*screen)[0]*(bounds[2]-bounds[0]),bounds[1]+(*screen)[1]*(bounds[3]-bounds[1]));frame(2);SyntheticEditorInput::Button(0,true);frame(1);SyntheticEditorInput::Button(0,false);frame(2);Check(ui.GetSelectedEntity()==camera,"camera gizmo picking selects real Entity");frame();
    Near(app.GetCameraPosition().x,editorPose.x,"preview never moves editor camera X");Near(app.GetCameraPosition().z,editorPose.z,"preview never moves editor camera Z");Check(!app.GetActiveCamera().IsValid(),"preview does not select gameplay camera");
    std::string error;Check(app.SaveSceneAs("scenes/"+files.token+".json",false,&error),error);auto authored=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());World playerPrefab;auto playerRoot=Node(playerPrefab);playerPrefab.AddComponent<PlayerComponent>(playerRoot);playerPrefab.GetComponent<TransformComponent>(playerRoot)->rotation={.1f,.2f,.3f};auto playerCamera=Node(playerPrefab,{0,1.5f,-7});playerPrefab.AddComponent<CameraComponent>(playerCamera);Check(playerPrefab.SetParent(playerCamera,playerRoot,false),"FPS Player Prefab child camera fixture");Check(SceneSerializer::WriteAtomic(files.prefab,scripting::CapturePrefab(playerPrefab,playerRoot),&error,false),error);
    Check(ui.OpenDocument(app,"prefabs/"+files.token+".prefab.json"),ui.GetError());Entity prefabCamera;for(auto entity:app.GetWorld().Entities())if(app.GetWorld().HasComponent<CameraComponent>(entity))prefabCamera=entity;Check(app.GetWorld().Parent(prefabCamera).IsValid(),"preview child camera inside Player Prefab");ui.SelectEntity(prefabCamera);frame();Check(ui.GetCameraPreviewRequest().entity.world==app.GetWorld().GetIdentity(),"Prefab preview uses current World");ui.BackToSceneDocument(app);frame();ui.SelectEntity(camera);frame();
    app.SetEditorPlayMode(true);app.SetEditorPaused(true);frame();auto active=app.GetActiveCamera();Check(gpu->GetCameraPreviewTextureId()==0&&active.IsValid(),"Play releases editor preview and selects game camera");ui.SetCameraToolsVisible(false,false,false);frame();Check(app.GetActiveCamera()==active,"preview visibility never changes active game camera");app.SetEditorPlayMode(false);ui.SetCameraToolsVisible(true,true,true);ui.SelectEntity(camera);frame();Check(gpu->GetCameraPreviewTextureId()!=0,"Stop permits preview again");Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==authored,"preview lifecycle never changes authoring data");
    struct LateRelease {Dx12RenderAdapter* renderer;unsigned calls=0;} late{gpu};ImGuiContextHook hook;hook.Type=ImGuiContextHookType_EndFramePre;hook.UserData=&late;
    hook.Callback=[](ImGuiContext*,ImGuiContextHook* h){auto& state=*static_cast<LateRelease*>(h->UserData);if(!state.calls&&state.renderer->GetCameraPreviewTextureId()){state.renderer->ReleaseCameraPreview();++state.calls;}};
    auto hookId=ImGui::AddContextHook(ImGui::GetCurrentContext(),&hook);frame(1);ImGui::RemoveContextHook(ImGui::GetCurrentContext(),hookId);Check(late.calls==1&&gpu->GetCameraPreviewTextureId()==0,"release after ImGui records SRV is safely deferred through current frame fence");frame(8);Check(gpu->GetCameraPreviewTextureId()!=0,"preview recovers after late close");
    for(int cycle=0;cycle<8;++cycle){ui.SetCameraToolsVisible(true,true,false);frame(2);Check(gpu->GetCameraPreviewTextureId()==0,"closed preview target released repeatedly");ui.SetCameraToolsVisible(true,true,true);frame(4);Check(gpu->GetCameraPreviewTextureId()!=0,"reopen reuses bounded descriptor capacity");}
    Check(std::filesystem::create_directory(files.projectParent),"owned project directory");auto project=ProjectManager::Create(files.projectParent,"Preview");Check(app.OpenProject(project->descriptorPath,Application::ProjectDecision::Discard,&error),error);frame();Check(gpu->GetCameraPreviewTextureId()==0&&!ui.GetCameraPreviewRequest().entity.IsValid(),"project switch releases previous World preview");Check(app.CloseProject(Application::ProjectDecision::Discard,&error),error);Check(gpu->GetValidationErrorCount()==0,"zero DX12 validation errors across preview lifecycle");
    }
    app.Shutdown();Application restarted;Check(restarted.Initialize(),"editor restart initializes clean device");Check(restarted.GetRenderAdapter()->GetCameraPreviewTextureId()==0,"new device initially owns no preview resource");restarted.SetFrameLimit(4);Check(restarted.Run()==0,"restart frames");auto restored=restarted.GetEditorLayer().GetCameraPreviewRequest().entity;Check(!restored.IsValid()||restarted.GetWorld().IsAlive(restored),"restored preview selection uses live new World handles");Check(dynamic_cast<Dx12RenderAdapter*>(restarted.GetRenderAdapter())->GetValidationErrorCount()==0,"restart preview has zero validation errors");restarted.Shutdown();
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{
#if defined(ENABLE_DX12)
    if(argc>1&&std::string(argv[1])=="--dx12")Native();else
#endif
    Cpu();std::cout<<"Camera Editor Tools: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Camera Editor Tools failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
