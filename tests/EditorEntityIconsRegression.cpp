#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "core/Application.h"
#include "editor/EntityIcons.h"
#include "editor/LightGizmoMath.h"
#include "editor/EditorTheme.h"
#include "ecs/components/LuaScriptComponent.h"
#include <iostream>
#include <fstream>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#if defined(ENABLE_DX12)
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#endif
using namespace ecs;
using namespace editor;
static unsigned checks=0;
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);++checks;}
static void Cpu(){
    Application app;app.NewScene();auto& ui=app.GetEditorLayer();auto& world=app.GetWorld();
    auto empty=ui.CreateEntity(app,"Director");Check(HasEntityIcon(world,empty),"Empty fallback icon");
    world.AddComponent<LuaScriptComponent>(empty);Check(HasEntityIcon(world,empty),"Lua uses fallback icon without extra ECS components");
    Check(EntityIconLabel(world,empty)=="Director","Tag tooltip");
    Check(ui.RenameEntity(app,empty,"EscapeDirector")&&EntityIconLabel(world,empty)=="EscapeDirector","rename updates tooltip");
    world.GetComponent<TagComponent>(empty)->name.clear();Check(EntityIconLabel(world,empty)=="Entity #"+std::to_string(empty.index),"empty Tag fallback");
    world.RemoveComponent<TagComponent>(empty);Check(EntityIconLabel(world,empty)=="Entity #"+std::to_string(empty.index),"missing Tag fallback");
    auto bare=world.CreateEntity();Check(!HasEntityIcon(world,bare),"Transform required");
    auto mesh=ui.CreatePrimitive(app,"cube");Check(!HasEntityIcon(world,mesh),"MeshRenderer excluded");
    for(auto type:{LightType::Directional,LightType::Point,LightType::Spot})Check(!HasEntityIcon(world,ui.CreateLight(app,type)),"special light excluded");
    Check(!HasEntityIcon(world,ui.CreateFog(app)),"Fog excluded");Check(!HasEntityIcon(world,ui.CreatePlayerStart(app)),"PlayerStart excluded");
    auto second=ui.CreateEntity(app,"Second");
    std::vector<EntityIconCandidate> candidates{{second,{.5f,.5f,10}},{empty,{.5f,.5f,10}},{mesh,{.5f,.5f,5}}};
    std::sort(candidates.begin(),candidates.end(),NearerEntityIcon);Check(candidates[0].entity==mesh&&candidates[1].entity==empty,"nearest depth then lowest index stable priority");
    const auto camera=GizmoCamera({0,0,0},0,0,1.f,1.5f,.1f,100.f);
    Check(!ProjectGizmo({0,0,-1},camera)&&!ProjectGizmo({100,0,1},camera)&&!ProjectGizmo({0,0,101},camera),"behind camera and outside frustum culled");
    Check(ProjectGizmo({0,0,1},camera)&&ProjectGizmo({0,0,90},camera),"near and distant origins project");
    ui.SelectViewportEntity(app,empty,false,false);ui.SelectViewportEntity(app,second,true,false);Check(ui.GetSelectedEntities().size()==2,"Ctrl adds fallback");
    ui.SelectViewportEntity(app,second,true,false);Check(ui.GetSelectedEntity()==empty&&ui.GetSelectedEntities().size()==1,"Ctrl toggles fallback");
    ui.SelectViewportEntity(app,second,false,true);Check(ui.GetSelectedEntity()==second&&ui.GetSelectedEntities().size()==2,"Shift adds Active");
    Check(ui.DeleteEntity(app,empty),"delete fallback");Check(!HasEntityIcon(world,empty)&&EntityIconLabel(world,empty).empty(),"deleted handle has no icon or tooltip");
    auto recycled=ui.CreateEntity(app,"Recycled");Check(recycled!=empty&&!HasEntityIcon(world,empty),"generation prevents stale icon");
    World other;auto foreign=other.CreateEntity();other.AddComponent<TransformComponent>(foreign);Check(!HasEntityIcon(world,foreign)&&EntityIconLabel(world,foreign).empty(),"World identity safety");
}
#if defined(ENABLE_DX12)
static void Gpu(){
    Application app;Check(app.Initialize(),"DX12 init");SyntheticEditorInput input;
    auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle();glfwHideWindow(window);
    SyntheticEditorInput::Queue([](ImGuiIO& io){io.AddFocusEvent(true);});
    auto* ctx=ImGui::GetCurrentContext();std::vector<std::string> errors;
    struct Restore {ImGuiContext* ctx;ImGuiErrorCallback callback;void* data;~Restore(){ctx->ErrorCallback=callback;ctx->ErrorCallbackUserData=data;}} restore{ctx,ctx->ErrorCallback,ctx->ErrorCallbackUserData};
    ctx->ErrorCallback=[](ImGuiContext*,void* data,const char* message){static_cast<std::vector<std::string>*>(data)->emplace_back(message);};ctx->ErrorCallbackUserData=&errors;
    ImGui::GetIO().Fonts->TexMinWidth=ImGui::GetIO().Fonts->TexMinHeight=1024;
    app.NewScene();auto& ui=app.GetEditorLayer();auto& world=app.GetWorld();ui.SetGameView(false);ui.SetGridVisible(false);
    auto a=ui.CreateEntity(app,"EscapeDirector"),b=ui.CreateEntity(app,"ConcertDirector");
    world.GetComponent<TransformComponent>(a)->position={-2,0,5};world.GetComponent<TransformComponent>(b)->position={2,0,5};
    auto& script=world.AddComponent<LuaScriptComponent>(b);script.scriptPath="scripts/FirstPersonController.lua";script.enabled=false;
    app.SetEditorCameraPose({0,0,-7},0,0);ui.ClearSelection();
    auto frame=[&](unsigned n=3){app.SetFrameLimit(n);Check(app.Run()==0,"UI frames");if(!errors.empty())throw std::runtime_error("ImGui diagnostic: "+errors.front());};frame(60);
    auto point=[&](ImVec2 p){SyntheticEditorInput::Position(p.x,p.y);frame();};
    auto click=[&](ImVec2 p,bool ctrl=false,bool shift=false){SyntheticEditorInput::Key(ImGuiMod_Ctrl,ctrl);SyntheticEditorInput::Key(ImGuiMod_Shift,shift);point(p);SyntheticEditorInput::Button(0,true);frame(1);SyntheticEditorInput::Button(0,false);frame();SyntheticEditorInput::Key(ImGuiMod_Ctrl,false);SyntheticEditorInput::Key(ImGuiMod_Shift,false);frame();};
    auto key=[&](ImGuiKey k){SyntheticEditorInput::Key(k,true);frame();SyntheticEditorInput::Key(k,false);frame();};
    auto screen=[&](Vec3 p){auto bounds=ui.GetViewportBounds();auto camera=GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),(bounds[2]-bounds[0])/(bounds[3]-bounds[1]),app.GetCameraNearPlane(),app.GetCameraFarPlane());auto q=ProjectGizmo(p,camera);Check(bool(q),"icon projects");return ImVec2(bounds[0]+(*q)[0]*(bounds[2]-bounds[0]),bounds[1]+(*q)[1]*(bounds[3]-bounds[1]));};
    // Inspect the live tooltip window rather than matching text in Hierarchy. CPU tests verify exact labels.
    auto tooltipMatches=[&](const std::string& label){frame(1);ImGuiWindow* tooltip=nullptr;for(auto* candidate:ctx->Windows)if((candidate->Flags&ImGuiWindowFlags_Tooltip)&&candidate->Active&&!candidate->Hidden&&candidate->LastFrameActive==ctx->FrameCount)tooltip=candidate;if(label.empty())return tooltip==nullptr;if(!tooltip)return false;auto size=ImGui::GetFont()->CalcTextSizeA(tooltip->FontRefSize,FLT_MAX,0,label.c_str());return std::abs(tooltip->ContentSize.x-std::ceil(size.x))<1.f;};
    auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());
    auto capture=[&](const char* name){int w,h;glfwGetFramebufferSize(window,&w,&h);auto bytes=gpu->ReadBackFramePixels();std::filesystem::create_directories("entity-icon-captures");std::ofstream image(std::filesystem::path("entity-icon-captures")/name,std::ios::binary);image<<"P6\n"<<w<<' '<<h<<"\n255\n";for(std::size_t i=0;i<bytes.size();i+=4)image.write(reinterpret_cast<const char*>(bytes.data()+i),3);Check(image.good(),"editor capture saved");};
    auto changed=[&](const auto& x,const auto& y,ImVec2 p){int w,h;glfwGetFramebufferSize(window,&w,&h);unsigned result=0;for(int yy=int(p.y)-12;yy<=int(p.y)+12;++yy)for(int xx=int(p.x)-12;xx<=int(p.x)+12;++xx)if(xx>=0&&xx<w&&yy>=0&&yy<h){auto i=(yy*w+xx)*4;result+=x[i]!=y[i]||x[i+1]!=y[i+1]||x[i+2]!=y[i+2];}return result;};
    auto pa=screen({-2,0,5}),pb=screen({2,0,5});point(pa);
    Check(tooltipMatches("EscapeDirector"),"hover tooltip without selection");
    capture("hover.ppm");
    Check(ui.GetSelectedEntities().empty(),"hover does not select");
    ui.RenameEntity(app,a,"RenamedDirector");frame();Check(tooltipMatches("RenamedDirector"),"live renamed tooltip");
    world.GetComponent<TagComponent>(a)->name.clear();frame();Check(tooltipMatches("Entity #"+std::to_string(a.index)),"UI fallback tooltip");
    point({ui.GetViewportBounds()[0]+40,ui.GetViewportBounds()[1]+70});auto icons=gpu->ReadBackFramePixels();auto scene=gpu->ReadBackViewportPixels();
    ui.SetGameView(true);frame();auto game=gpu->ReadBackFramePixels();Check(changed(icons,game,pa)>40&&changed(icons,game,pb)>40,"Empty and Lua icons visible; Game View hides both");Check(scene==gpu->ReadBackViewportPixels(),"icons do not enter game render target");
    ui.SetGameView(false);frame();click(pa);Check(ui.GetSelectedEntity()==a,"Empty icon pick");click(pb,true);Check(ui.GetSelectedEntities().size()==2&&ui.GetSelectedEntity()==b,"Lua icon Ctrl add");click(pb,true);Check(ui.GetSelectedEntities().size()==1&&ui.GetSelectedEntity()==a,"Ctrl toggle icon");click(pb,false,true);Check(ui.GetSelectedEntities().size()==2&&ui.GetSelectedEntity()==b,"Shift add icon");
    // Identical screen origins: nearest wins; equal depths use the same stable ID
    // priority for the label and click, regardless of previous selection.
    auto overlap=ui.CreateEntity(app,"NearDirector");world.GetComponent<TransformComponent>(overlap)->position={0,0,2};
    world.GetComponent<TransformComponent>(a)->position={0,0,6};ui.ClearSelection();frame();auto center=screen({0,0,2});point(center);Check(tooltipMatches("NearDirector"),"overlap hover chooses nearest");click(center);Check(ui.GetSelectedEntity()==overlap,"overlap picking matches tooltip");
    world.GetComponent<TransformComponent>(overlap)->position={0,0,6};const auto lowerId=a.index<overlap.index?a:overlap;world.GetComponent<TagComponent>(lowerId)->name="LowerId";ui.ClearSelection();frame();point(center);Check(tooltipMatches("LowerId"),"equal depth tooltip chooses lowest ID");click(center);Check(ui.GetSelectedEntity()==lowerId,"equal depth pick deterministic");
    auto fog=ui.CreateFog(app);world.GetComponent<TransformComponent>(fog)->position={0,0,6};ui.ClearSelection();frame();click(center);Check(ui.GetSelectedEntity()==fog,"specialized Fog icon has picking priority");ui.DeleteEntity(app,fog);ui.DeleteEntity(app,overlap);
    world.GetComponent<TagComponent>(a)->name="LowerId";world.GetComponent<TransformComponent>(a)->position={-2,0,5};ui.ClearSelection();frame();pa=screen({-2,0,5});point(pa);Check(tooltipMatches("LowerId"),"tooltip before deletion");click(pa);ui.DeleteEntity(app,a);frame();Check(tooltipMatches(""),"deleted Entity leaves no tooltip");
    ui.UndoEdit(app);a=ui.GetSelectedEntity();Check(world.IsAlive(a),"Undo restores current-generation Entity");ui.ClearSelection();frame();pa=screen({-2,0,5});click(pa);Check(ui.GetSelectedEntity()==a,"restored icon selectable after Undo");
    // Play swaps Worlds; no fallback icon or tooltip is submitted there.
    ui.ClearSelection();point({ui.GetViewportBounds()[0]+40,ui.GetViewportBounds()[1]+70});frame();icons=gpu->ReadBackFramePixels();
    app.SetEditorPlayMode(true);frame();auto play=gpu->ReadBackFramePixels();Check(changed(icons,play,pa)>40,"Play hides fallback icon");point(pa);Check(tooltipMatches(""),"Play hides fallback tooltip");app.SetEditorPlayMode(false);frame();click(pa);Check(ui.GetSelectedEntity()==a,"Stop restores editor World icon");
    const auto dock=ImGui::FindWindowByName("Viewport")->DockId;int oldW,oldH;glfwGetWindowSize(window,&oldW,&oldH);const float oldDpi=ImGui::GetStyle().FontScaleDpi;
    for(float dpi:{1.f,1.5f,2.f}){
        key(ImGuiKey_F11);Check(ui.IsViewportMaximized(),"F11 enters");ApplyEditorTheme(dpi);glfwSetWindowSize(window,1100,740);frame(8);
        auto p=screen({2,0,5});ui.ClearSelection();point({p.x+8*dpi,p.y});Check(tooltipMatches("ConcertDirector"),"DPI-scaled hover hitbox in F11");click({p.x+8*dpi,p.y});Check(ui.GetSelectedEntity()==b,"DPI-scaled F11 picking");
        auto* viewport=ImGui::FindWindowByName("Viewport##Maximized");Check(!viewport->ScrollbarX&&!viewport->ScrollbarY,"overlay has no layout scrollbars");
        const auto bounds=ui.GetViewportBounds();Check(std::abs(bounds[2]-bounds[0]-ImGui::GetMainViewport()->Size.x)<1&&std::abs(bounds[3]-bounds[1]-ImGui::GetMainViewport()->Size.y)<1,"F11 bounds follow resize");
        key(ImGuiKey_F11);Check(!ui.IsViewportMaximized()&&ImGui::FindWindowByName("Viewport")->DockId==dock,"F11 restores docking node");
    }
    ApplyEditorTheme(oldDpi);glfwSetWindowSize(window,oldW,oldH);frame();
    key(ImGuiKey_F11);ui.ClearSelection();frame();auto pivot=screen({2,0,5});click(pivot);Check(ui.GetSelectedEntity()==b,"icon selection before gizmo manipulation");
    ui.SetSnapping(false);ui.SetGizmoOperation(0);frame();float grab=0;
    for(float dx:{15.f,20.f,25.f,30.f,40.f,50.f}){point({pivot.x+dx,pivot.y});if(ImGuizmo::IsOver(ImGuizmo::TRANSLATE_X)){grab=dx;break;}}
    Check(grab>0,"fallback selection exposes Move handle");auto original=*world.GetComponent<TransformComponent>(b);auto history=ui.UndoCount();
    SyntheticEditorInput::Button(0,true);frame(1);point({pivot.x+grab+35,pivot.y});point({pivot.x+grab+60,pivot.y});SyntheticEditorInput::Button(0,false);frame();
    Check(std::abs(world.GetComponent<TransformComponent>(b)->position.x-original.position.x)>.1f&&ui.UndoCount()==history+1,"Move fallback Entity records one Undo");ui.UndoEdit(app);frame();Check(ui.GetSelectedEntity()==b&&std::abs(world.GetComponent<TransformComponent>(b)->position.x-original.position.x)<.001f,"Move Undo retains fallback selection");
    ui.SetGizmoOperation(1);frame();float ring=0;
    for(float r=20;r<140;r+=5){point({pivot.x+r*.7071068f,pivot.y-r*.7071068f});if(ImGuizmo::IsOver(ImGuizmo::ROTATE_Z)){ring=r;break;}}
    Check(ring>0,"fallback selection exposes Rotate ring");SyntheticEditorInput::Button(0,true);frame(1);point({pivot.x+ring*.258819f,pivot.y-ring*.965926f});SyntheticEditorInput::Button(0,false);frame();
    Check(std::abs(world.GetComponent<TransformComponent>(b)->rotation.z)>.01f,"Rotate fallback Entity");ui.UndoEdit(app);frame();
    ui.SetGizmoOperation(2);frame();point(pivot);Check(ImGuizmo::IsOver(ImGuizmo::SCALE),"Scale handle wins over fallback icon");SyntheticEditorInput::Button(0,true);frame(1);point({pivot.x+45,pivot.y-20});SyntheticEditorInput::Button(0,false);frame();
    Check(std::abs(world.GetComponent<TransformComponent>(b)->scale.x-original.scale.x)>.01f,"Scale fallback Entity");ui.UndoEdit(app);frame();key(ImGuiKey_F11);
    Check(gpu->GetValidationErrorCount()==0,"DX12 validation zero");
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{
#if defined(ENABLE_DX12)
    if(argc>1&&std::string(argv[1])=="--dx12")Gpu();else
#endif
    Cpu();std::cout<<"Editor Entity Icons: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
