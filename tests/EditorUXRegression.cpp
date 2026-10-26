#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "editor/EntityNaming.h"
#include "editor/WidgetDesignerTools.h"
#include "editor/EditorPreferences.h"
#include "scene/SceneSerializer.h"
#include "ecs/components/SceneIdentityComponent.h"
#include <fstream>
#include <iostream>
#if defined(ENABLE_DX12)
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#endif
using namespace editor;using namespace ecs;
static unsigned checks=0;
static void Check(bool v,const std::string& message){if(!v)throw std::runtime_error(message);++checks;}
static ui::Document Fixture(){
 auto d=ui::EmptyDocument();ui::Widget a;a.id="Panel";a.parent=d.root;d.widgets.push_back(a);ui::Widget child;child.id="Child";child.parent=a.id;d.widgets.push_back(child);
 d.requiredWidgets={a.id};ui::AnimationClip clip;clip.id="Fade";clip.widget=a.id;d.animations.push_back(clip);return d;
}
static void Cpu(){
 auto d=Fixture();auto before=nlohmann::json(d);
 for(auto name:{"","   ","Child","bad\nID"}){bool rejected=false;try{widgets::Rename(d,"Panel",name);}catch(const std::exception&){rejected=true;}Check(rejected&&nlohmann::json(d)==before,"invalid rename is atomic");}
 widgets::Rename(d,"Panel","Status");Check(widgets::Find(d,"Child").parent=="Status"&&d.animations[0].widget=="Status"&&d.requiredWidgets[0]=="Status","rename repairs all authored references");
 Check(!EditorPreferences{}.uiPreview,"fresh profile preview Off");auto old=PreferencesFromJson({{"version",1}});Check(old.uiPreview,"legacy file without preview key keeps its previous On startup behavior");
 for(bool enabled:{false,true}){EditorPreferences p;p.uiPreview=enabled;p.cameraSpeed=4;auto copy=PreferencesFromJson(PreferencesToJson(p));Check(copy==p,"preview preference and unrelated settings roundtrip");}
 World world;auto node=[&](const std::string& name,Entity parent=Entity{}){auto e=world.CreateEntity();world.AddComponent<TransformComponent>(e);world.AddComponent<TagComponent>(e).name=name;Check(world.SetParent(e,parent,false),"naming fixture hierarchy");return e;};
 auto parent=node("Parent");node("Cube");node("Cube1");node("Cube3");node("Cube4");node("Cube1",parent);EntityNames names(world);
 Check(names.Claim("Cube",{},true)=="Cube2","duplicate takes next free suffix");Check(names.Claim("Cube3",{},true)=="Cube5","numeric suffix advances past collisions");Check(names.Claim("Cube Copy Copy",{},true)=="Cube6","legacy Copy suffix normalization");Check(names.Claim("Cube",parent,true)=="Cube2","sibling namespace independent");
 Check(names.Claim("Cube9",{},true)=="Cube10"&&names.Claim("Cube6",{},true)=="Cube7","high reservation never skips lower free suffix");
 for(unsigned i=1;i<=2000;++i)Check(names.Claim("Batch",{},true)=="Batch"+std::to_string(i),"batch successor reservations");
}
#if defined(ENABLE_DX12)
namespace editor {
struct WidgetDesignerTestAccess {
 static auto& Session(EditorLayer& e,Application& a){return e.UIDocumentSession(a,e.m_DesignDocument);}
 static void Select(EditorLayer& e,Application& a,const std::string& id){Session(e,a).selected=id;}
 static int Operation(EditorLayer& e){return e.m_GizmoOperation;}
 static bool Renaming(EditorLayer& e){return !e.m_WidgetInlineId.empty();}
 static auto Tools(EditorLayer& e){return std::array<int,3>{e.m_GizmoMode,int(e.m_Snapping),int(e.m_RotationSnapping)};}
};
}
static std::string Id(EditorLayer& e,const std::string& key){for(const auto& d:e.Documents())if(d.key==key)return d.id;throw std::runtime_error("missing document: "+key);}
struct Owned {
 std::vector<std::filesystem::path> files;
 void Add(const std::string& key){files.push_back(AssetPaths::ResolveAssetOutputPath(key));}
 ~Owned(){std::error_code error;for(auto& p:files)std::filesystem::remove(p,error);}
};
static void Native(){
 Application app;Check(app.Initialize(),"DX12 Initialize");Owned owned;SyntheticEditorInput input;auto& e=app.GetEditorLayer();glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());app.NewScene();e.SetFixedDiagnosticLayout(false);e.SetDiagnosticViewport(400,300);
 auto frame=[&](unsigned n=3){app.SetFrameLimit(n);Check(app.Run()==0,"native ImGui/DX12 frames");};
 auto key=[&](ImGuiKey key,bool ctrl=false){if(ctrl)SyntheticEditorInput::Key(ImGuiMod_Ctrl,true);SyntheticEditorInput::Key(key,true);frame(1);SyntheticEditorInput::Key(key,false);if(ctrl)SyntheticEditorInput::Key(ImGuiMod_Ctrl,false);frame(2);};
 auto focus=[&](const char* name){ImGui::ClearActiveID();ImGui::SetWindowFocus(name);frame(3);};
 auto type=[&](const std::string& text){key(ImGuiKey_A,true);SyntheticEditorInput::Queue([&](ImGuiIO& io){io.AddInputCharactersUTF8(text.c_str());});frame(2);};
 auto token=NewSceneIdentity();auto scene="scenes/ux-"+token+".json",uiKey="ui/ux-"+token+".ui.json",prefab="prefabs/ux-"+token+".prefab.json",lua="scripts/ux-"+token+".lua",material="materials/ux-"+token+".material.json";
 for(auto& path:{scene,uiKey,prefab,lua,material})owned.Add(path);
 auto cube=e.CreateEntity(app,"Cube"),child=e.CreateChild(app,cube);auto name=[&](Entity x){return app.GetWorld().GetComponent<TagComponent>(x)->name;};
 e.SelectEntity(cube);auto one=e.DuplicateEntity(app,cube);Check(name(one)=="Cube1","native duplicate numeric name");e.SelectEntity(cube);auto two=e.DuplicateEntity(app,cube);Check(name(two)=="Cube2","native second duplicate numeric name");auto id=SceneSerializer::EntitySceneId(app.GetWorld(),two);e.UndoEdit(app);e.RedoEdit(app);Check(SceneSerializer::EntitySceneId(app.GetWorld(),e.GetSelectedEntity())==id&&name(e.GetSelectedEntity())=="Cube2","duplicate Redo retains name and GUID");
 std::string error;Check(app.SaveSceneAs(scene,false,&error),error);frame();auto sceneId=Id(e,scene);auto authored=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());
 focus("Viewport");e.SetUIPreviewVisible(false);key(ImGuiKey_U,true);Check(e.IsUIPreviewVisible(),"Ctrl+U enables editor preview");key(ImGuiKey_U,true);Check(!e.IsUIPreviewVisible(),"Ctrl+U disables editor preview");
 EditorPreferences saved;Check(LoadPreferences(PreferencesPath(),saved,&error)&&!saved.uiPreview,"Ctrl+U persists preview choice");

 auto* viewport=ImGui::FindWindowByName("Viewport");ImGui::ActivateItemByID(viewport->GetID("Display"));frame(3);auto& popups=ImGui::GetCurrentContext()->OpenPopupStack;Check(!popups.empty()&&popups.back().Window,"Display popup opens");ImGui::ActivateItemByID(popups.back().Window->GetID("UI Preview (Ctrl+U)"));frame(3);Check(e.IsUIPreviewVisible(),"Display checkbox updates the same preview state");Check(LoadPreferences(PreferencesPath(),saved,&error)&&saved.uiPreview,"Display persists explicit On");ImGui::ClosePopupToLevel(0,true);frame();focus("Viewport");key(ImGuiKey_U,true);Check(!e.IsUIPreviewVisible(),"Ctrl U and Display share one switch");
 Check(UILoader::Save(uiKey,Fixture(),&error),error);Check(e.OpenUIDesigner(app,uiKey),e.GetError());frame(10);
 for(auto name:{"Widget Hierarchy","Widget Inspector"}){auto* w=ImGui::FindWindowByName(name);Check(w&&w->Active,"UI panel active");ImGui::SetWindowDock(w,0,ImGuiCond_Always);ImGui::SetWindowPos(w,{std::string(name)=="Widget Hierarchy"?0.f:700.f,140});ImGui::SetWindowSize(w,{500,520});}frame(6);
 WidgetDesignerTestAccess::Select(e,app,"Panel");Check(e.WidgetCommand(app,"Lock"),"lock is a typed editor reference");focus("Widget Hierarchy");auto history=WidgetDesignerTestAccess::Session(e,app).undo.size();key(ImGuiKey_F2);Check(ImGui::GetIO().WantTextInput,"F2 activates actual hierarchy InputText");type("Status");key(ImGuiKey_Enter);
 auto renamed=e.GetUIDraft(app,uiKey);Check(WidgetDesignerTestAccess::Session(e,app).selected=="Status","rename keeps hierarchy selection");Check(renamed.animations[0].widget=="Status"&&renamed.requiredWidgets[0]=="Status"&&widgets::Find(renamed,"Child").parent=="Status","native rename updates references");Check(WidgetDesignerTestAccess::Session(e,app).undo.size()==history+1,"Enter is one rename transaction");
 WidgetDesignerTestAccess::Select(e,app,"Child");Check(e.UndoUIDocument(app,uiKey),e.GetError());Check(e.GetUIDraft(app,uiKey)==Fixture()&&WidgetDesignerTestAccess::Session(e,app).selected=="Child"&&WidgetDesignerTestAccess::Session(e,app).lockedWidgets.contains("Panel"),"rename Undo restores lock reference even with a different selection");WidgetDesignerTestAccess::Select(e,app,"Panel");Check(e.UndoUIDocument(app,uiKey,true)&&WidgetDesignerTestAccess::Session(e,app).selected=="Status"&&WidgetDesignerTestAccess::Session(e,app).lockedWidgets.contains("Status"),"rename Redo preserves selected ID and lock reference");WidgetDesignerTestAccess::Select(e,app,"Status");focus("Widget Hierarchy");key(ImGuiKey_F2);type("Cancelled");key(ImGuiKey_Escape);Check(e.GetUIDraft(app,uiKey)==renamed&&!WidgetDesignerTestAccess::Renaming(e),"Escape closes inline field and cancels hierarchy rename");
 // Invalid IDs remain in the field; Escape still returns to the unchanged draft.
 for(auto text:{"Child",""}){focus("Widget Hierarchy");key(ImGuiKey_F2);if(*text)type(text);else{key(ImGuiKey_A,true);key(ImGuiKey_Backspace);}key(ImGuiKey_Enter);Check(e.GetUIDraft(app,uiKey)==renamed&&!e.GetError().empty(),"invalid inline ID is diagnosed without mutation");key(ImGuiKey_Escape);}
 focus("Widget Inspector");auto* inspector=ImGui::FindWindowByName("Widget Inspector");ImGui::ActivateItemByID(inspector->GetID("##Widget ID"));frame(2);type("InspectorStatus");key(ImGuiKey_Enter);Check(widgets::Find(e.GetUIDraft(app,uiKey),"InspectorStatus").id=="InspectorStatus","Inspector ID confirms directly with Enter");Check(e.SaveUIDocument(app,uiKey),e.GetError());
 // Use the real tab bar's middle-button handling and the normal transition dialog.
 auto middle=[&](const std::string& document){frame();auto* w=ImGui::FindWindowByName("Document Tabs");Check(w&&w->Active,"document tab window");auto* bar=ImGui::GetCurrentContext()->TabBars.GetByKey(ImHashStr("Documents",0,w->ID));Check(bar,"actual ImGui tab bar");int i=0;for(;i<int(e.Documents().size());++i)if(e.Documents()[i].id==document)break;Check(i<bar->Tabs.Size,"tab exists before middle close");auto& tab=bar->Tabs[i];SyntheticEditorInput::Position(bar->BarRect.Min.x+tab.Offset+tab.Width*.5f,bar->BarRect.GetCenter().y);frame(1);SyntheticEditorInput::Button(2,true);frame(1);SyntheticEditorInput::Button(2,false);frame(3);};
 auto exists=[&](const std::string& document){return std::any_of(e.Documents().begin(),e.Documents().end(),[&](auto& d){return d.id==document;});};
 auto decision=[&](const char* label){auto* w=ImGui::FindWindowByName("Document transition");Check(w&&w->Active,"dirty close dialog shown");ImGui::ActivateItemByID(w->GetID(label));frame(4);};
 auto uiId=Id(e,uiKey);middle(uiId);if(exists(uiId)||e.ActiveDocument()!=sceneId){std::cerr<<"middle close active="<<e.ActiveDocument()<<" expected="<<sceneId<<" exists="<<exists(uiId)<<"\n";}Check(!exists(uiId)&&e.ActiveDocument()==sceneId,"clean UI middle-close returns to scene");

 {auto count=e.Documents().size();auto* w=ImGui::FindWindowByName("Document Tabs");SyntheticEditorInput::Position(w->Pos.x+w->Size.x-4,w->Pos.y+w->Size.y*.5f);frame(1);SyntheticEditorInput::Button(2,true);frame(1);SyntheticEditorInput::Button(2,false);frame();Check(e.Documents().size()==count,"middle click empty tab bar closes nothing");auto bounds=e.GetViewportBounds();SyntheticEditorInput::Position(bounds[0]+12,bounds[1]+12);frame(1);SyntheticEditorInput::Button(2,true);frame(1);SyntheticEditorInput::Button(2,false);frame();Check(e.Documents().size()==count,"middle click document content closes nothing");}
 Check(e.OpenUIDesigner(app,uiKey),e.GetError());frame(6);WidgetDesignerTestAccess::Select(e,app,"InspectorStatus");Check(e.WidgetCommand(app,"Rename","Dirty"),e.GetError());uiId=Id(e,uiKey);middle(uiId);decision("Cancel");Check(exists(uiId),"Cancel preserves dirty tab");middle(uiId);decision("Save");Check(!exists(uiId)&&widgets::Find(*UILoader::Load(uiKey,{}).data.document,"Dirty").id=="Dirty","Save from middle-close persists UI and closes");
 Check(e.OpenUIDesigner(app,uiKey),e.GetError());frame(5);WidgetDesignerTestAccess::Select(e,app,"Dirty");Check(e.WidgetCommand(app,"Rename","Discarded"),e.GetError());uiId=Id(e,uiKey);middle(uiId);decision("Discard");Check(!exists(uiId)&&widgets::Find(*UILoader::Load(uiKey,{}).data.document,"Dirty").id=="Dirty","Discard closes without writing draft");
 Check(e.ActivateDocument(app,sceneId,EditorLayer::OpenDecision::Discard),e.GetError());frame();Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==authored,"UI rename/tab navigation preserves scene");
 e.SelectEntity(cube);Check(e.CopyEntity(app,cube),"scene clipboard snapshot");Check(e.CreatePrefab(app,prefab),e.GetError());Check(e.OpenPrefabEditor(app,prefab),e.GetError());frame(8);focus("Viewport");e.SetSnapping(true);auto tools=WidgetDesignerTestAccess::Tools(e);
 for(auto entity:app.GetWorld().Entities()) {if(!app.GetWorld().HasComponent<TransformComponent>(entity))continue;e.SelectEntity(entity);for(auto [button,mode]:{std::pair{ImGuiKey_W,0},std::pair{ImGuiKey_E,1},std::pair{ImGuiKey_R,2}}){key(button);Check(WidgetDesignerTestAccess::Operation(e)==mode,"Prefab root/child W E R route to existing gizmo mode");Check(WidgetDesignerTestAccess::Tools(e)==tools,"W E R preserves Local World and snapping settings");}}


 {auto entities=app.GetWorld().Entities();Check(entities.size()>=2,"Prefab child fixture");e.SelectViewportEntity(app,entities[0],false,false);e.SelectViewportEntity(app,entities[1],true,false);key(ImGuiKey_E);Check(e.GetSelectedEntities().size()==2&&WidgetDesignerTestAccess::Operation(e)==1,"Prefab multiselection hotkeys");e.SelectEntity(entities[0]);}
 focus("Scene Hierarchy");key(ImGuiKey_F2);Check(ImGui::GetIO().WantTextInput,"Entity F2 inline rename in Prefab");auto operation=WidgetDesignerTestAccess::Operation(e);auto preview=e.IsUIPreviewVisible();key(ImGuiKey_W);key(ImGuiKey_U,true);Check(WidgetDesignerTestAccess::Operation(e)==operation&&e.IsUIPreviewVisible()==preview,"text input protects gizmo and preview shortcuts");key(ImGuiKey_Escape);focus("Viewport");
 Check(e.PasteEntity(app).IsValid(),"Copy Paste from Scene into Prefab");e.UndoEdit(app);
 auto root=e.GetSelectedEntity();e.SelectEntity(root);auto oldPosition=app.GetWorld().GetComponent<TransformComponent>(root)->position;Check(e.SetProperty(app,root,ComponentId("Transform"),"position",Vec3{1,2,3}),e.GetError());e.UndoEdit(app);Check(app.GetWorld().GetComponent<TransformComponent>(root)->position.x==oldPosition.x,"Prefab Transform Undo");e.RedoEdit(app);Check(app.GetWorld().GetComponent<TransformComponent>(root)->position.x==1,"Prefab Transform Redo");auto duplicate=e.DuplicateEntity(app,root);Check(duplicate.IsValid(),"duplicate inside Prefab");e.UndoEdit(app);Check(!app.GetWorld().IsAlive(duplicate),"Prefab duplicate Undo");e.RedoEdit(app);Check(app.GetWorld().IsAlive(e.GetSelectedEntity()),"Prefab duplicate Redo");Check(!e.SavePrefabEditor(app,true),"duplicated Prefab root cannot save a multi-root prefab");e.UndoEdit(app);Entity prefabChild;for(auto entity:app.GetWorld().Entities())if(app.GetWorld().Parent(entity)==root){prefabChild=entity;break;}Check(prefabChild.IsValid(),"existing authored child for Prefab duplication");e.SelectEntity(prefabChild);Check(e.DuplicateEntity(app,prefabChild).IsValid(),"duplicate child inside valid Prefab hierarchy");auto prefabId=Id(e,prefab);auto oldMode=WidgetDesignerTestAccess::Operation(e);auto oldPreview=e.IsUIPreviewVisible();middle(prefabId);key(ImGuiKey_W);key(ImGuiKey_U,true);Check(WidgetDesignerTestAccess::Operation(e)==oldMode&&e.IsUIPreviewVisible()==oldPreview,"modal protects Prefab shortcuts");decision("Cancel");Check(app.IsPrefabEditing(),"Cancel preserves Prefab context");Check(e.SavePrefabEditor(app,true),e.GetError());frame();middle(prefabId);Check(!app.IsPrefabEditing()&&e.ActiveDocument()==sceneId,"middle-close Prefab restores Scene context");
 // Inactive Lua and Material tabs use the same close path.
 {std::ofstream f(AssetPaths::ResolveAssetOutputPath(lua));f<<"return {}\n";}
 Check(e.OpenDocument(app,lua),e.GetError());frame(8);auto luaId=Id(e,lua);Check(e.CreateMaterialAsset(app,material,&error),error);Check(e.OpenDocument(app,material),e.GetError());frame(6);auto matId=Id(e,material);middle(luaId);Check(!exists(luaId)&&e.ActiveDocument()==matId,"middle-close targets inactive Lua only");middle(matId);Check(!exists(matId),"middle-close Material");
 Check(e.ActivateDocument(app,sceneId,EditorLayer::OpenDecision::Discard),e.GetError());frame();middle(sceneId);if(exists(sceneId)){decision("Discard");}Check(!exists(sceneId),"middle close Scene follows dirty protection");
 Check(dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter())->GetValidationErrorCount()==0,"zero DX12 validation errors");app.Shutdown();
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{Cpu();
#if defined(ENABLE_DX12)
 if(argc>1&&std::string(argv[1])=="--dx12")Native();
#endif
 std::cout<<"Editor UX: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<"Editor UX failed after "<<checks<<": "<<error.what()<<'\n';return 1;}}
