#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "ApplicationTestAccess.h"
#include "core/Application.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "core/AssetPaths.h"
#include "editor/WidgetDesignerTools.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "scene/SceneSerializer.h"
#include <imgui_internal.h>
#include <iostream>
#include <fstream>
using Json=nlohmann::json;
static int checks=0;
static void Check(bool v,const std::string& text){if(!v)throw std::runtime_error(text);++checks;}
static void Near(float a,float b,const std::string& text){Check(std::abs(a-b)<.02f,text+" actual="+std::to_string(a)+" expected="+std::to_string(b));}
static void Same(ui::Rect a,ui::Rect b){Near(a.x,b.x,"rectangle x");Near(a.y,b.y,"rectangle y");Near(a.w,b.w,"rectangle width");Near(a.h,b.h,"rectangle height");}
struct Owned {std::filesystem::path path;~Owned(){std::error_code error;std::filesystem::remove(path,error);}};
namespace editor {
struct WidgetDesignerTestAccess {
 static auto& Session(EditorLayer& e,Application& a){return e.UIDocumentSession(a,e.m_DesignDocument);}
 static void Select(EditorLayer& e,Application& a,std::string id){Session(e,a).selected=std::move(id);}
 static void Preview(EditorLayer& e,ui::Pair size){e.m_UIPreviewSize=size;e.m_UIFit=true;}
 static void Snap(EditorLayer& e,bool value){e.m_UISnap=value;e.m_UIGridSize=16;}
 static void Close(EditorLayer& e){e.m_ShowCanvasDesigner=e.m_ShowWidgetHierarchy=e.m_ShowWidgetInspector=false;}
 static ImVec2 Point(EditorLayer& e,Application& a,float x,float y){auto& d=Session(e,a).draft;float scale=std::min(e.m_UIPreviewSize[0]/d.referenceResolution[0],e.m_UIPreviewSize[1]/d.referenceResolution[1])*e.m_UIZoom;return {e.m_UICanvasOrigin.x+x*scale,e.m_UICanvasOrigin.y+y*scale};}
 static float Zoom(EditorLayer& e){return e.m_UIZoom;}
 static ImVec2 Pan(EditorLayer& e){return {e.m_UIPan.x,e.m_UIPan.y};}
};
}
using namespace editor;using namespace editor::widgets;
static ui::Document Fixture(){
 auto d=ui::EmptyDocument();d.referenceResolution={400,300};ui::Widget panel;panel.id="Panel";panel.parent=d.root;panel.position={30,25};panel.size={110,80};panel.color={1,.1f,.1f,.7f};panel.clipChildren=true;d.widgets.push_back(panel);
 ui::Widget child;child.id="Child";child.parent=panel.id;child.position={90,10};child.size={40,30};child.color={0,1,0,1};d.widgets.push_back(child);
 ui::Widget other;other.id="Other";other.parent=d.root;other.position={240,50};other.size={100,150};other.color={.1f,.2f,1,.5f};d.widgets.push_back(other);
 ui::Widget text;text.id="Text";text.type="Text";text.parent=d.root;text.position={40,150};text.size={180,70};text.fontSize=24;text.text="Здоровье\nТекст";d.widgets.push_back(text);
 ui::Widget stretch;stretch.id="Stretch";stretch.parent=d.root;stretch.anchorMax={1,0};stretch.size={-80,24};stretch.position={40,250};stretch.color={1,1,0,1};d.widgets.push_back(stretch);
 ui::Widget image;image.id="Image";image.parent=d.root;image.type="Image";image.texture="textures/material_test.png";image.position={250,215};image.size={40,25};d.widgets.push_back(image);
 ui::Widget bar;bar.id="Progress";bar.parent=d.root;bar.type="ProgressBar";bar.value=.5f;bar.position={250,10};bar.size={100,20};d.widgets.push_back(bar);return d;
}
static void CPU(){
 auto d=Fixture();auto canvas=Canvas(d,{400,300});auto initial=Rectangle(d,"Panel",canvas);
 for(auto preview:{ui::Pair{400,300},ui::Pair{1600,900},ui::Pair{1080,1920}}){auto logical=Canvas(d,preview);for(auto& w:d.widgets){auto copy=w;auto r=Rectangle(d,w.id,logical),p=ParentRectangle(d,w,logical);SetRectangle(copy,r,p);Same(ui::LayoutRect(copy,p),r);}}
 for(int anchor=0;anchor<12;++anchor){auto next=d;Preset(next,"Panel",anchor,-1,canvas);Same(Rectangle(next,"Panel",canvas),initial);Check(ui::Validate(next).empty(),"all anchor presets valid");}
 for(int pivot=0;pivot<9;++pivot){auto next=d;Preset(next,"Panel",-1,pivot,canvas);Same(Rectangle(next,"Panel",canvas),initial);}
 auto next=d;auto child=Rectangle(d,"Child",canvas);Reparent(next,"Panel","Other",canvas);Same(Rectangle(next,"Panel",canvas),initial);Same(Rectangle(next,"Child",canvas),child);
 auto original=Json(next);try{Reparent(next,"Other","Child",canvas);Check(false,"cycle should fail");}catch(const std::exception&){}Check(Json(next)==original,"cycle failure is atomic");
 try{Reparent(next,"Root","Other",canvas);}catch(const std::exception&){}Check(Json(next)==original,"root reparent rejected");
 try{Reparent(next,"Panel","Text",canvas);}catch(const std::exception&){}Check(Json(next)==original,"non-Panel reparent rejected");
 Rename(next,"Panel","Status");Check(Find(next,"Child").parent=="Status","rename updates child references");original=next;try{Rename(next,"Status","Other");}catch(const std::exception&){}Check(Json(next)==original,"duplicate ID rename atomic");
 auto copied=Duplicate(next,"Status");Check(Find(next,"Child_Copy").parent==copied,"duplicate subtree internal IDs");Delete(next,copied);Check(Json(next)==original,"delete entire duplicated subtree");
 auto snapped=Drag(initial,10,13,-1,16);Near(snapped.x,48,"logical x grid snap");Near(snapped.y,32,"logical y grid snap");
 for(int h=0;h<8;++h){auto r=Drag(initial,15,20,h,0);Check(r.w>=0&&r.h>=0,"all eight handles");auto w=Find(d,"Stretch");SetRectangle(w,r,canvas);Same(ui::LayoutRect(w,canvas),r);}
 auto clamped=Drag(initial,10000,10000,0,0);Near(clamped.w,0,"resize cannot cross right");Near(clamped.h,0,"resize cannot cross bottom");
 auto font=ui::RasterizeFont();auto draw=ui::LayoutDocument(d,400,300,font,{1},[](auto&){return RenderTextureHandle{};});
 Check(Pick(draw,125,40)=="Child","topmost nested visible quad picked");Check(Pick(draw,150,40).empty(),"clipped child not picked outside Panel");Check(Pick(draw,5,5).empty(),"transparent root ignored");
 next=d;for(auto& w:next.widgets)if(w.id=="Panel")w.visible=false;draw=ui::LayoutDocument(next,400,300,font,{1},[](auto&){return RenderTextureHandle{};});Check(Pick(draw,125,40).empty(),"hidden subtree not picked");
}
static void Native(){
 Application app;Check(app.Initialize(),"initialize editor");ApplicationTestAccess::FrameDelta(app,1.f/30);SyntheticEditorInput input;auto& e=app.GetEditorLayer();auto frame=[&](unsigned n=3){app.SetFrameLimit(n);Check(app.Run()==0,"native frames");};app.NewScene();frame();
 auto key="ui/designer-"+ecs::NewSceneIdentity()+".ui.json";Owned file{ui::SafeDocumentPath(key)};auto d=Fixture();Check(UILoader::Save(key,d),"fixture document saved");auto scene=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());
 Check(e.OpenUIDesigner(app,key),e.GetError());WidgetDesignerTestAccess::Preview(e,{400,300});frame(12);Check(e.GetWorkspace()==EditorWorkspace::UIDesigner,"UI workspace applied");Check(e.SaveCurrentWorkspace(app),e.GetError());EditorPreferences savedPreferences;Check(LoadPreferences(PreferencesPath(),savedPreferences)&&savedPreferences.activeWorkspace==EditorWorkspace::UIDesigner&&savedPreferences.workspaces.contains("ui"),"UI workspace saved through existing preferences");Check(ImGui::FindWindowByName("Canvas Designer")&&ImGui::FindWindowByName("Widget Hierarchy")&&ImGui::FindWindowByName("Widget Inspector"),"three docked panels");
 auto draw=app.BuildUIDesignerPreview(e.GetUIDraft(app,key),{400,300});Check(!draw.vertices.empty(),"native preview runtime geometry");Check(draw.errors.empty(),"designer Image resolves actual GPU texture");for(auto& cmd:draw.commands)Check(app.GetRenderAdapter()->GetTexturePreviewId(cmd.texture)!=0,"real preview GPU textures available");
 auto point=[&](float x,float y){return WidgetDesignerTestAccess::Point(e,app,x,y);};
 auto mouse=[&](ImVec2 p){SyntheticEditorInput::Position(p.x,p.y);frame(1);};
 auto click=[&](ImVec2 p){mouse(p);SyntheticEditorInput::Button(0,true);frame(1);SyntheticEditorInput::Button(0,false);frame(2);};
 click(point(60,65));Check(WidgetDesignerTestAccess::Session(e,app).selected=="Panel","real Canvas mouse selection");
 const auto before=Json(e.GetUIDraft(app,key));auto history=WidgetDesignerTestAccess::Session(e,app).undo.size();
 auto drag=[&](ImVec2 start,float dx,float dy,bool cancel=false){mouse(start);SyntheticEditorInput::Button(0,true);frame(1);auto pressed=ImGui::GetIO().MousePos;mouse({start.x+dx,start.y+dy});mouse({start.x+dx*1.5f,start.y+dy*1.5f});if(cancel){SyntheticEditorInput::Key(ImGuiKey_Escape,true);frame(1);SyntheticEditorInput::Key(ImGuiKey_Escape,false);}auto final=ImGui::GetIO().MousePos;SyntheticEditorInput::Button(0,false);frame(3);return ui::Pair{(final.x-pressed.x)/WidgetDesignerTestAccess::Zoom(e),(final.y-pressed.y)/WidgetDesignerTestAccess::Zoom(e)};};
 float scale=WidgetDesignerTestAccess::Zoom(e);auto delta=drag(point(60,65),20*scale,10*scale);auto moved=Rectangle(e.GetUIDraft(app,key),"Panel",{0,0,400,300});Near(moved.x,30+delta[0],"drag from baseline x without accumulation");Near(moved.y,25+delta[1],"drag from baseline y");Check(WidgetDesignerTestAccess::Session(e,app).undo.size()==history+1,"one move equals one undo");
 Check(e.UndoUIDocument(app,key),"undo move");Check(Json(e.GetUIDraft(app,key))==before,"undo restores whole move");Check(e.UndoUIDocument(app,key,true),"redo move");frame();
 auto dragBefore=Json(e.GetUIDraft(app,key));history=WidgetDesignerTestAccess::Session(e,app).undo.size();drag(point(80,80),10*scale,10*scale,true);Check(Json(e.GetUIDraft(app,key))==dragBefore,"Escape cancels move");Check(WidgetDesignerTestAccess::Session(e,app).undo.size()==history,"cancel adds no undo");
 WidgetDesignerTestAccess::Select(e,app,"Panel");frame();auto r=Rectangle(e.GetUIDraft(app,key),"Panel",{0,0,400,300});delta=drag(point(r.x+r.w,r.y+r.h),10*scale,10*scale);auto resized=Rectangle(e.GetUIDraft(app,key),"Panel",{0,0,400,300});Near(resized.w,r.w+delta[0],"real bottom-right resize handle");Near(resized.h,r.h+delta[1],"resize height");
 WidgetDesignerTestAccess::Select(e,app,"Stretch");frame();r=Rectangle(e.GetUIDraft(app,key),"Stretch",{0,0,400,300});delta=drag(point(r.x+r.w,r.y+r.h*.5f),10*scale,0);resized=Rectangle(e.GetUIDraft(app,key),"Stretch",{0,0,400,300});Near(resized.w,r.w+delta[0],"stretched right resize");Check(Find(e.GetUIDraft(app,key),"Stretch").size[0]<0,"stretch retains negative SizeDelta");
 WidgetDesignerTestAccess::Select(e,app,"Panel");r=Rectangle(e.GetUIDraft(app,key),"Panel",{0,0,400,300});Check(e.WidgetCommand(app,"Anchor","11"),e.GetError());Same(Rectangle(e.GetUIDraft(app,key),"Panel",{0,0,400,300}),r);Check(e.WidgetCommand(app,"Pivot","4"),e.GetError());Same(Rectangle(e.GetUIDraft(app,key),"Panel",{0,0,400,300}),r);
 Check(e.WidgetCommand(app,"Reparent","Other"),e.GetError());Same(Rectangle(e.GetUIDraft(app,key),"Panel",{0,0,400,300}),r);WidgetDesignerTestAccess::Select(e,app,"Other");auto unchanged=Json(e.GetUIDraft(app,key));Check(!e.WidgetCommand(app,"Reparent","Child"),"reject cyclic hierarchy command");Check(Json(e.GetUIDraft(app,key))==unchanged,"invalid command unchanged");
 WidgetDesignerTestAccess::Select(e,app,"Panel");Check(e.WidgetCommand(app,"Duplicate"),e.GetError());Check(e.WidgetCommand(app,"Rename","Copy"),e.GetError());Check(Find(e.GetUIDraft(app,key),"Child_Copy").parent=="Copy","renamed copy references");Check(e.WidgetCommand(app,"Delete"),e.GetError());Check(e.UndoUIDocument(app,key)&&Find(e.GetUIDraft(app,key),"Copy").id=="Copy","delete undo");Check(e.UndoUIDocument(app,key,true),"delete redo");
 WidgetDesignerTestAccess::Select(e,app,"Text");frame();draw=app.BuildUIDesignerPreview(e.GetUIDraft(app,key),{400,300});auto text=std::find_if(draw.widgets.begin(),draw.widgets.end(),[](auto& w){return w.id=="Text";});auto vertex=draw.vertices[draw.commands[text->first].first];delta=drag(point(vertex.x+2,vertex.y+2),10*scale,0);Near(Find(e.GetUIDraft(app,key),"Text").position[0],40+delta[0],"real text drag uses runtime glyph picking");
 // Snapping applies in logical units, independently of the screen zoom.
 WidgetDesignerTestAccess::Snap(e,true);WidgetDesignerTestAccess::Select(e,app,"Text");frame();draw=app.BuildUIDesignerPreview(e.GetUIDraft(app,key),{400,300});text=std::find_if(draw.widgets.begin(),draw.widgets.end(),[](auto& w){return w.id=="Text";});vertex=draw.vertices[draw.commands[text->first].first];drag(point(vertex.x+2,vertex.y+2),10*scale,0);Near(Find(e.GetUIDraft(app,key),"Text").position[0],64,"native logical grid snapping");WidgetDesignerTestAccess::Snap(e,false);
 // Real wheel and middle-button events leave authored geometry untouched.
 auto previewBefore=Json(e.GetUIDraft(app,key));mouse(point(200,120));float oldZoom=WidgetDesignerTestAccess::Zoom(e);SyntheticEditorInput::Queue([](ImGuiIO& io){io.AddMouseWheelEvent(0,1);});frame();Check(WidgetDesignerTestAccess::Zoom(e)>oldZoom,"native wheel zoom");auto oldPan=WidgetDesignerTestAccess::Pan(e);auto panStart=point(200,120);mouse(panStart);SyntheticEditorInput::Button(2,true);frame(1);mouse({panStart.x+12,panStart.y+10});SyntheticEditorInput::Button(2,false);frame();auto newPan=WidgetDesignerTestAccess::Pan(e);Near(newPan.x,oldPan.x+12,"native middle pan x");Near(newPan.y,oldPan.y+10,"native middle pan y");Check(Json(e.GetUIDraft(app,key))==previewBefore,"zoom and pan do not author changes");
 auto keyboardBefore=Json(e.GetUIDraft(app,key));auto sceneHistory=e.UndoCount();Check(e.WidgetCommand(app,"Add","Panel"),"keyboard fixture command");ImGui::FocusWindow(ImGui::FindWindowByName("Canvas Designer"));SyntheticEditorInput::Key(ImGuiMod_Ctrl,true);SyntheticEditorInput::Key(ImGuiKey_Z,true);frame(1);SyntheticEditorInput::Key(ImGuiKey_Z,false);SyntheticEditorInput::Key(ImGuiMod_Ctrl,false);frame();Check(Json(e.GetUIDraft(app,key))==keyboardBefore&&e.UndoCount()==sceneHistory,"Ctrl+Z routes only to UI history");
 auto draft=Json(e.GetUIDraft(app,key));WidgetDesignerTestAccess::Preview(e,{1080,1920});frame();Check(Json(e.GetUIDraft(app,key))==draft,"preview size never modifies reference or authored geometry");
 auto key2="ui/designer-other-"+ecs::NewSceneIdentity()+".ui.json";Owned file2{ui::SafeDocumentPath(key2)};Check(UILoader::Save(key2,ui::EmptyDocument()),"second document");Check(e.OpenUIDesigner(app,key2)&&e.OpenUIDesigner(app,key),"switch documents");Check(Json(e.GetUIDraft(app,key))==draft,"dirty draft survives document switch");e.SwitchWorkspace(EditorWorkspace::LevelEditing);frame();e.SwitchWorkspace(EditorWorkspace::UIDesigner);frame();Check(Json(e.GetUIDraft(app,key))==draft,"dirty draft survives workspace switch");WidgetDesignerTestAccess::Close(e);frame();e.OpenUIDesigner(app,key);frame();Check(Json(e.GetUIDraft(app,key))==draft,"closing panels retains draft");
 Check(e.SaveUIDocument(app,key),e.GetError());auto loaded=UILoader::Load(key,{});Check(loaded.success&&Json(*loaded.data.document)==draft,"saved runtime document equals designer");auto actual=app.BuildUIDesignerPreview(*loaded.data.document,{400,300});auto expected=app.BuildUIDesignerPreview(e.GetUIDraft(app,key),{400,300});Check(actual.vertices.size()==expected.vertices.size()&&actual.commands.size()==expected.commands.size(),"saved runtime geometry parity");for(size_t i=0;i<actual.vertices.size();++i){Near(actual.vertices[i].x,expected.vertices[i].x,"saved geometry x");Near(actual.vertices[i].y,expected.vertices[i].y,"saved geometry y");}
 Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==scene,"widget edits leave ECS World unchanged");
 auto external=*loaded.data.document;external.name="External";Check(UILoader::Save(key,external),"external edit");Check(!e.SaveUIDocument(app,key),"external modification protected");Check(Json(e.GetUIDraft(app,key))==draft,"conflict preserves draft");Check(e.ReloadUIDocument(app,key),e.GetError());Check(e.GetUIDraft(app,key).name=="External"&&WidgetDesignerTestAccess::Session(e,app).undo.empty(),"explicit reload replaces draft and clears UI history");
 app.SetEditorPlayMode(true);Check(!e.WidgetCommand(app,"Add","Panel"),"authored edit blocked during Play");app.SetEditorPlayMode(false);frame();
 std::filesystem::create_directories("diagnostics/widget-designer");auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(gpu->SaveFrameImage("diagnostics/widget-designer/designer.ppm"),"designer screenshot");Check(e.OpenUIDesigner(app,"ui/PlayerHUD.ui.json"),e.GetError());WidgetDesignerTestAccess::Preview(e,{1920,1080});frame(5);Check(gpu->SaveFrameImage("diagnostics/widget-designer/player-hud.ppm"),"PlayerHUD Designer screenshot");Check(gpu->GetValidationErrorCount()==0,"zero DX12 validation errors");app.Shutdown();
}
int main(){ScopedEditorPreferenceRestore preferences;try{CPU();Native();std::cout<<"Widget Designer: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"WIDGET DESIGNER FAILED after "<<checks<<": "<<e.what()<<'\n';return 1;}}
