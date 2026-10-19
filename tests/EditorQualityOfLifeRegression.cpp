#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "scene/SceneSerializer.h"
#include "resources/ResourceManager.h"
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "editor/LightGizmoMath.h"
#include "ecs/LightGather.h"
#include "ecs/components/PlayerStartComponent.h"
#include "ecs/components/FogComponent.h"
#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <iostream>
#include <chrono>
namespace fs=std::filesystem;
using namespace ecs;using namespace editor;
static unsigned checks=0;
static void Check(bool value,const std::string& label){if(!value)throw std::runtime_error(label);++checks;}
static void Near(float a,float b,const char* label){Check(std::abs(a-b)<.001f,label);}
static std::string Utf8(const char8_t* text){return reinterpret_cast<const char*>(text);}
struct Fixture {
 fs::path root=fs::current_path()/("editor-qol-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 Fixture(){fs::create_directory(root);}
 ~Fixture(){ProjectManager::Activate({});std::error_code ec;fs::remove_all(root,ec);}
};
int main(){ScopedEditorPreferenceRestore restore;Fixture fixture;try{
 auto defaults=PreferencesToJson(EditorPreferences{});defaults.erase("bookmarks");defaults.erase("selectionFilters");
 Check(PreferencesFromJson(defaults).selectionFilters==SelectionFilters{},"old preferences enable all filters");
 auto project=ProjectManager::Create(fixture.root,Utf8(u8"\u041f\u0440\u043e\u0435\u043a\u0442"));const auto descriptor=project->descriptorPath;
 const auto key=Utf8(u8"Scenes/\u041a\u0430\u043c\u0435\u0440\u044b.json");std::string error;
 {
 Application app;Check(app.Initialize(Application::StartupMode::ProjectBrowser),"initialize editor");
 auto* native=dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle();glfwHideWindow(native);SyntheticEditorInput input;
 Check(app.OpenProject(descriptor,Application::ProjectDecision::Discard,&error),error);
 auto& ui=app.GetEditorLayer();auto& world=app.GetWorld();auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());
 ui.GetSelectionFilters()={};ui.SetLightGizmosVisible(true);ui.GetVisualSettings().enabled=true;ui.SetGameView(false);ui.SetSnapping(false);ui.SetGridVisible(false);ui.SetGizmoOperation(0);
 auto frame=[&](unsigned n=3){app.SetFrameLimit(n);Check(app.Run()==0,"editor frames");};
 auto point=[&](ImVec2 p){SyntheticEditorInput::Position(p.x,p.y);frame();};
 auto click=[&](ImVec2 p){point(p);SyntheticEditorInput::Button(0,true);frame(1);SyntheticEditorInput::Button(0,false);frame();};
 auto screen=[&](Vec3 p){auto b=ui.GetViewportBounds();auto c=GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),(b[2]-b[0])/(b[3]-b[1]),app.GetCameraNearPlane(),app.GetCameraFarPlane());auto q=ProjectGizmo(p,c);Check(bool(q),"target projects");return ImVec2(b[0]+(*q)[0]*(b[2]-b[0]),b[1]+(*q)[1]*(b[3]-b[1]));};
 std::vector<Entity> cubes;for(int i=0;i<3;++i){auto e=ui.CreatePrimitive(app,"cube");world.GetComponent<TransformComponent>(e)->position={float(i*2-2),0,5};auto& material=*world.GetComponent<MaterialComponent>(e);material.overrides.doubleSided=true;material.tint[0]=i==0?1.f:.15f;material.tint[1]=i==1?1.f:.15f;material.tint[2]=i==2?1.f:.15f;cubes.push_back(e);}
 auto floor=ui.CreatePrimitive(app,"plane");world.GetComponent<TransformComponent>(floor)->position={0,-1,5};world.GetComponent<TransformComponent>(floor)->scale={20,1,20};
 auto light=ui.CreateLight(app,LightType::Point);world.GetComponent<TransformComponent>(light)->position={-3,2,5};
 auto empty=ui.CreateEntity(app,Utf8(u8"\u0420\u0443\u0441\u0441\u043a\u043e\u0435 \u0438\u043c\u044f"));world.GetComponent<TransformComponent>(empty)->position={3,2,5};
 auto start=ui.CreatePlayerStart(app);world.GetComponent<TransformComponent>(start)->position={-5,0,7};
 auto fog=ui.CreateFog(app);world.GetComponent<FogComponent>(fog)->enabled=false;world.GetComponent<TransformComponent>(fog)->position={5,0,7};
 auto physical=ui.CreateEntity(app,"Trigger");world.AddComponent<ColliderComponent>(physical).isTrigger=true;
 Check(app.SaveSceneAs(key,false,&error),error);auto original=SceneSerializer::CaptureWorld(world,app.GetSceneSettings());
 std::array<Vec3,3> poses={Vec3{0,2,-8},Vec3{3,3,-6},Vec3{-3,4,-10}};
 for(int i=0;i<3;++i){Check(app.SetEditorCameraPose(poses[i],i*.1f,-i*.1f),"set view");Check(ui.SaveBookmark(app,i),"save camera bookmark");}
 Check(ui.RenameBookmark(app,0,Utf8(u8"\u041e\u0431\u0449\u0438\u0439 \u0432\u0438\u0434")),"rename Cyrillic bookmark");
 for(int i=0;i<3;++i){Check(ui.GoToBookmark(app,i),"restore bookmark");Near(app.GetCameraPosition().x,poses[i].x,"bookmark position");Near(app.GetCameraYaw(),i*.1f,"bookmark orientation");}
 Check(ui.SaveBookmark(app,8)&&ui.DeleteBookmark(app,8)&&!ui.GoToBookmark(app,8),"ninth slot and delete");
 Check(!ui.SaveBookmark(app,9)&&!ui.RenameBookmark(app,0,""),"invalid bookmark operations rejected");
 Check(SceneSerializer::CaptureWorld(world,app.GetSceneSettings())==original&&!ui.IsSceneDirty(),"bookmarks do not alter World or dirty scene");
 EditorPreferences prefs;Check(LoadPreferences(PreferencesPath(),prefs,&error),error);Check(PreferencesFromJson(PreferencesToJson(prefs))==prefs,"bookmark preference roundtrip");
 auto corrupted=PreferencesToJson(prefs);auto identity=corrupted["bookmarks"].begin().key();corrupted["bookmarks"][identity][1]["pitch"]=999;
 auto recovered=PreferencesFromJson(corrupted);Check(recovered.bookmarks.at(identity)[0].has_value()&&!recovered.bookmarks.at(identity)[1],"invalid bookmark isolated from valid preferences");
 app.SetEditorCameraPose({8,3,-9},.2f,-.1f);frame(20);ImGui::FocusWindow(ImGui::FindWindowByName("Viewport"));frame();
 SyntheticEditorInput::Key(ImGuiMod_Ctrl,true);SyntheticEditorInput::Key(ImGuiMod_Shift,true);SyntheticEditorInput::Key(ImGuiKey_4,true);frame(1);
 SyntheticEditorInput::Key(ImGuiKey_4,false);SyntheticEditorInput::Key(ImGuiMod_Shift,false);SyntheticEditorInput::Key(ImGuiMod_Ctrl,false);frame();
 Check(ui.GetSceneBookmarks(app)&&(*ui.GetSceneBookmarks(app))[3].has_value(),"Ctrl Shift 4 saves bookmark");
 app.SetEditorCameraPose({0,0,0},0,0);SyntheticEditorInput::Key(ImGuiMod_Ctrl,true);SyntheticEditorInput::Key(ImGuiKey_4,true);frame(1);
 SyntheticEditorInput::Key(ImGuiKey_4,false);SyntheticEditorInput::Key(ImGuiMod_Ctrl,false);frame();Near(app.GetCameraPosition().x,8,"Ctrl 4 restores camera");
 ImGui::ActivateItemByID(ImGui::FindWindowByName("Viewport")->GetID("View"));frame();
 auto* bookmarkParent=ImGui::GetCurrentContext()->OpenPopupStack.back().Window;ImGui::ActivateItemByID(bookmarkParent->GetID("Bookmarks"));frame();
 auto* bookmarkList=ImGui::GetCurrentContext()->OpenPopupStack.back().Window;Check(bookmarkList!=bookmarkParent,"Bookmarks submenu opens");
 const auto bookmarkLabel="1. "+(*ui.GetSceneBookmarks(app))[0]->name+"###Bookmark0";ImGui::ActivateItemByID(bookmarkList->GetID(bookmarkLabel.c_str()));frame();
 auto* bookmarkMenu=ImGui::GetCurrentContext()->OpenPopupStack.back().Window;Check(bookmarkMenu!=bookmarkList,"saved bookmark actions open");
 const int bookmarkSlot=0;const auto nameId=ImHashStr("Name",0,ImHashData(&bookmarkSlot,sizeof(bookmarkSlot),bookmarkMenu->IDStack.back()));
 ImGui::ActivateItemByID(nameId);frame(1);SyntheticEditorInput::Key(ImGuiMod_Ctrl,true);SyntheticEditorInput::Key(ImGuiKey_A,true);frame(1);
 SyntheticEditorInput::Key(ImGuiKey_A,false);SyntheticEditorInput::Key(ImGuiMod_Ctrl,false);frame(1);
 SyntheticEditorInput::Queue([](ImGuiIO& io){io.AddInputCharactersUTF8(Utf8(u8"\u041d\u043e\u0432\u044b\u0439 \u0432\u0438\u0434").c_str());});frame(1);
 SyntheticEditorInput::Key(ImGuiKey_Enter,true);frame(1);SyntheticEditorInput::Key(ImGuiKey_Enter,false);frame();
 Check((*ui.GetSceneBookmarks(app))[0]->name==Utf8(u8"\u041d\u043e\u0432\u044b\u0439 \u0432\u0438\u0434"),"bookmark name edited through InputText");
 Check(ui.RenameBookmark(app,0,Utf8(u8"\u041e\u0431\u0449\u0438\u0439 \u0432\u0438\u0434")),"restore bookmark label");
 ImGui::ActivateItemByID(bookmarkMenu->GetID("Go to Bookmark"));frame();Near(app.GetCameraPosition().z,poses[0].z,"menu Go to Bookmark restores view");
 app.SetEditorCameraPose({0,1,-8},0,0);frame();ui.ClearSelection();
 auto selected=[&]{ui.SelectViewportEntity(app,cubes[0],false,false);ui.SelectViewportEntity(app,cubes[1],true,false);ui.SelectViewportEntity(app,cubes[2],true,false);};selected();
 app.SetViewportUnlit(true);frame();const auto fullViewPixels=gpu->ReadBackViewportPixels();
 ImGui::ActivateItemByID(ImGui::FindWindowByName("Viewport")->GetID("View"));frame();
 auto* tools=ImGui::GetCurrentContext()->OpenPopupStack.empty()?nullptr:ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
 Check(tools&&tools->Active,"Viewport View menu opens");ImGui::ActivateItemByID(tools->GetID("Isolate Selected"));frame();
 Check(ui.IsIsolationActive()&&ui.GetSelectedEntities()==cubes,"menu isolation keeps multi-selection");frame();
 Check(app.GetRenderStatistics().visibleObjects==3,"only isolated meshes rendered");
 Check(gpu->ReadBackViewportPixels()!=fullViewPixels,"isolation changes actual GPU scene pixels");
 Check(!ui.EditorEntityVisible(app,floor)&&!ui.EditorEntityVisible(app,light),"nonisolated objects hidden");
 Check(SceneSerializer::CaptureWorld(world,app.GetSceneSettings())==original&&!ui.IsSceneDirty(),"isolation does not mutate or dirty scene");
 const auto floorBefore=*world.GetComponent<TransformComponent>(floor);auto count=ui.UndoCount();
 Check(ui.BeginGroupTransform(app,false),"isolated group transform begins");TransformComponent pivot;pivot.position={0,1,5};
 Check(ui.UpdateGroupTransform(app,TransformMatrix(pivot)),"isolated group transform updates");ui.EndGroupTransform(app);
 Check(ui.UndoCount()==count+1,"isolated group creates one Undo");Near(world.GetComponent<TransformComponent>(floor)->position.y,floorBefore.position.y,"hidden object untouched");
 ui.UndoEdit(app);for(auto e:cubes)Near(world.GetComponent<TransformComponent>(e)->position.y,0,"isolated Undo");
 ui.RedoEdit(app);for(auto e:cubes)Near(world.GetComponent<TransformComponent>(e)->position.y,1,"isolated Redo");ui.UndoEdit(app);
 ui.SelectHierarchyEntity(app,light,false,false,world.Entities());Check(ui.GetSelectedEntity()==light&&ui.IsolationEntities()==cubes&&!ui.EditorEntityVisible(app,light),"Hierarchy selection does not redefine isolation");
 selected();auto hiddenHit=ui.RaycastPlacement(app,{7,10,5},{0,-1,0},30);Check(!hiddenHit,"hidden floor cannot intercept Surface Snap");
 auto visibleHit=ui.RaycastPlacement(app,{0,10,5},{0,-1,0},30);Check(visibleHit&&visibleHit->entity==cubes[1],"visible isolated mesh remains a snap surface");
 auto preview=ui.UpdatePlacementPreview(app,"@engine/models/primitives/cube.obj",.5f,.5f);Check(preview.ready,"Placement Preview stays available in isolation");ui.CancelPlacement();
 Check(ui.BeginDuplicateDrag(app).IsValid(),"Alt drag duplication begins in isolation");const auto copies=ui.GetSelectedEntities();
 for(auto e:copies)Check(ui.EditorEntityVisible(app,e),"new authoring copies remain visible");ui.FinishTransaction(app);ui.UndoEdit(app);frame();ui.RedoEdit(app);frame();
 for(auto e:ui.GetSelectedEntities())Check(ui.EditorEntityVisible(app,e),"duplicate Redo visible in isolation");ui.UndoEdit(app);selected();frame();
 fs::create_directories("diagnostics/editor-qol");Check(gpu->SaveFrameImage("diagnostics/editor-qol/isolation.ppm"),"isolation screenshot");
 ui.ExitIsolation();frame();app.SetViewportUnlit(false);frame();Check(!ui.IsIsolationActive()&&app.GetRenderStatistics().visibleObjects==4,"exit restores complete mesh view");
 Check(ui.RaycastPlacement(app,{7,10,5},{0,-1,0},30)->entity==floor,"exit restores floor Surface Snap");
 auto lightsBefore=GatherSceneLights(world,app.GetSceneSettings().lighting,LightCamera{}).lights;
 ui.SelectHierarchyEntity(app,light,false,false,world.Entities());ui.GetSelectionFilters().lights=false;
 Check(ui.GetSelectedEntity()==light&&!ui.CanViewportSelect(app,light),"filter keeps selection but blocks light picking");
 ui.ClearSelection();frame();click(screen(world.GetComponent<TransformComponent>(light)->position));Check(ui.GetSelectedEntity()!=light,"disabled light marker cannot be clicked");
 Check(GatherSceneLights(world,app.GetSceneSettings().lighting,LightCamera{}).lights.size()==lightsBefore.size(),"light filter leaves lighting active");
 ui.SelectHierarchyEntity(app,light,false,false,world.Entities());Check(ui.GetSelectedEntity()==light,"disabled category remains selectable in Hierarchy");
 ui.GetSelectionFilters().meshes=false;Check(!ui.PickViewportMesh(app,{-2,0,-5},{0,0,1},30).IsValid(),"mesh filter applied to actual ray picking");
 Check(ui.RaycastPlacement(app,{-2,0,-5},{0,0,1},30).has_value(),"selection filters do not block Surface Snap");
 ui.GetSelectionFilters().other=false;ui.GetSelectionFilters().physics=false;ui.GetSelectionFilters().playerStarts=false;ui.GetSelectionFilters().fog=false;
 Check(!ui.CanViewportSelect(app,empty)&&!ui.CanViewportSelect(app,physical)&&!ui.CanViewportSelect(app,start)&&!ui.CanViewportSelect(app,fog),"real component filters cover other editor markers");
 ui.SelectHierarchyEntity(app,cubes[1],false,false,world.Entities());frame();auto pivotScreen=screen(world.GetComponent<TransformComponent>(cubes[1])->position);float grab=0;
 for(float dx:{15.f,20.f,25.f,30.f,40.f,50.f}){point({pivotScreen.x+dx,pivotScreen.y});if(ImGuizmo::IsOver(ImGuizmo::TRANSLATE_X)){grab=dx;break;}}
 Check(grab>0,"disabled mesh category keeps selected Gizmo available");const auto beforeDrag=*world.GetComponent<TransformComponent>(cubes[1]);count=ui.UndoCount();
 SyntheticEditorInput::Button(0,true);frame(1);point({pivotScreen.x+grab+25,pivotScreen.y});point({pivotScreen.x+grab+50,pivotScreen.y});SyntheticEditorInput::Button(0,false);frame();
 Check(std::abs(world.GetComponent<TransformComponent>(cubes[1])->position.x-beforeDrag.position.x)>.1f&&ui.UndoCount()==count+1,"actual Gizmo drag works with mesh picking disabled");ui.UndoEdit(app);frame();
 selected();frame();Check(ImGui::FindWindowByName("Viewport")->Active,"filtered selection retains Viewport");
 ImGui::ActivateItemByID(ImGui::FindWindowByName("Viewport")->GetID("View"));frame();
 auto* filterParent=ImGui::GetCurrentContext()->OpenPopupStack.back().Window;ImGui::ActivateItemByID(filterParent->GetID("Selection Filters"));frame();
 auto* filterMenu=ImGui::GetCurrentContext()->OpenPopupStack.back().Window;Check(filterMenu!=filterParent,"Selection Filters submenu opens");
 ImGui::ActivateItemByID(filterMenu->GetID("Enable All"));frame();Check(ui.CanViewportSelect(app,light)&&ui.CanViewportSelect(app,empty)&&ui.PickViewportMesh(app,{-2,0,-5},{0,0,1},30)==cubes[0],"Enable All restores picking");
 ui.ClearSelection();frame();click(screen(world.GetComponent<TransformComponent>(light)->position));Check(ui.GetSelectedEntity()==light,"reenabled light marker selects");
 ui.ClearSelection();frame();click(screen(world.GetComponent<TransformComponent>(empty)->position));Check(ui.GetSelectedEntity()==empty,"Cyrillic named Empty selects");
 auto* font=ImGui::GetFont();const auto* glyph=font->GetFontBaked(ImGui::GetFontSize())->FindGlyphNoFallback(0x0416);Check(glyph&&glyph->Codepoint==0x0416,"font contains real Cyrillic glyph");
 ui.ShowProjectSettings(app);frame();Check(gpu->SaveFrameImage("diagnostics/editor-qol/cyrillic.ppm"),"Cyrillic project path screenshot");
 Check(ui.GoToBookmark(app,0),"bookmark after isolation/filter use");
 selected();Check(ui.IsolateSelected(app),"isolate before Play");app.SetEditorPlayMode(true);frame();Check(!ui.IsIsolationActive(),"Play clears editor isolation");
 Check(!ui.SaveBookmark(app,0)&&!ui.GoToBookmark(app,0)&&!ui.IsolateSelected(app),"Play blocks authoring camera tools");app.SetEditorPlayMode(false);frame();
 selected();ui.IsolateSelected(app);Check(ui.DeleteSelection(app),"delete isolated entities");frame();Check(!ui.IsIsolationActive(),"deleting all isolated entities exits mode");ui.UndoEdit(app);frame();
 Check(app.OpenScene(key,&error),error);Check(ui.GetSceneBookmarks(app)&&(*ui.GetSceneBookmarks(app))[0]->name==Utf8(u8"\u041e\u0431\u0449\u0438\u0439 \u0432\u0438\u0434"),"reopen same scene restores bookmarks");
 Check(SceneSerializer::CaptureWorld(world,app.GetSceneSettings())==original,"scene disk data unaffected by editor tools");
 auto secondKey="Scenes/Other.json";Check(app.SaveSceneAs(secondKey,false,&error),error);Check(!ui.GetSceneBookmarks(app),"different scene path has independent bookmarks");
 Check(ui.SaveBookmark(app,0),"save separate scene bookmark");Check(app.OpenScene(key,&error),error);Check(ui.GoToBookmark(app,0),"return to original scene bookmark");Near(app.GetCameraPosition().z,poses[0].z,"scene bookmark identity preserved");
 ui.GetSelectionFilters().lights=false;ui.PersistPreferences(app);Check(gpu->GetValidationErrorCount()==0,"DX12 validation zero");
 app.NewScene();Check(!ui.GetSceneBookmarks(app)&&!ui.SaveBookmark(app,0),"untitled scene cannot overwrite saved-scene bookmarks");
 Check(app.CloseProject(Application::ProjectDecision::Discard,&error),error);app.Shutdown();
 }
 {
 Application app;Check(app.Initialize(Application::StartupMode::ProjectBrowser),"restart editor");glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());
 Check(app.OpenProject(descriptor,Application::ProjectDecision::Discard,&error),error);Check(app.OpenScene(key,&error),error);auto& ui=app.GetEditorLayer();
 Check(ui.GetSceneBookmarks(app)&&(*ui.GetSceneBookmarks(app))[0]->name==Utf8(u8"\u041e\u0431\u0449\u0438\u0439 \u0432\u0438\u0434")&&!ui.GetSelectionFilters().lights,"bookmarks and filters survive engine restart");
 Check(ui.GoToBookmark(app,2),"restored bookmark navigates after restart");Near(app.GetCameraPosition().x,-3,"restored camera position");
 Check(app.CloseProject(Application::ProjectDecision::Discard,&error),error);app.Shutdown();
 }
 std::cout<<"Editor QoL: "<<checks<<" checks passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<"EDITOR QOL FAILED: "<<e.what()<<'\n';return 1;}}
