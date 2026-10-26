#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "ApplicationTestAccess.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "editor/WidgetDesignerTools.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "ecs/components/LuaScriptComponent.h"
#include "ecs/components/UICanvasComponent.h"
#include "scene/SceneSerializer.h"
#include "scripting/GameplayServices.h"
#include <fstream>
#include <iostream>
#if defined(ENABLE_DX12)
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#endif
using namespace editor;using namespace ecs;using Json=nlohmann::json;namespace fs=std::filesystem;
static unsigned checks=0;
static void Check(bool b,const std::string& why){if(!b)throw std::runtime_error(why);++checks;}
static ui::Document Fixture(){
 auto d=ui::EmptyDocument();d.referenceResolution={400,300};
 for(auto id:{"A","B","C","Other","Nested"}){ui::Widget w;w.id=id;w.parent=std::string(id)=="Nested"?"Other":d.root;w.size={60,30};w.position={10,10};d.widgets.push_back(w);}
 for(auto type:{"Text","Image","ProgressBar","Button"}){ui::Widget w;w.id=w.type=type;w.parent="Nested";w.text="Health";d.widgets.push_back(w);}
 d.requiredWidgets={"C"};ui::AnimationClip clip;clip.id="Fade";clip.widget="C";d.animations.push_back(clip);return d;
}
static std::vector<std::string> Children(const ui::Document& d,const std::string& parent){std::vector<std::string> out;for(auto& w:d.widgets)if(w.parent==parent)out.push_back(w.id);return out;}
static void Cpu(){
 auto d=Fixture();auto original=d;auto rect=widgets::Rectangle(d,"C",{0,0,400,300});
 widgets::Move(d,"C","A",widgets::Drop::Before,{0,0,400,300});Check(Children(d,d.root)==std::vector<std::string>({"C","A","B","Other"}),"Before changes document sibling order");
 widgets::Move(d,"C","B",widgets::Drop::After,{0,0,400,300});Check(d==original,"After restores original exact document");
 widgets::Move(d,"C","Nested",widgets::Drop::Inside,{0,0,400,300});Check(widgets::Find(d,"C").parent=="Nested","nested Panel accepts children");auto moved=widgets::Rectangle(d,"C",{0,0,400,300});Check(moved.x==rect.x&&moved.y==rect.y,"Manual reparent preserves screen position");
 widgets::Move(d,"C",d.root,widgets::Drop::Inside,{0,0,400,300});Check(widgets::Find(d,"C").parent==d.root,"move out to Root");Check(d.animations[0].widget=="C"&&d.requiredWidgets[0]=="C","move retains authored references and IDs");
 for(auto [id,target,drop]:std::vector<std::tuple<std::string,std::string,widgets::Drop>>{std::tuple{d.root,"A",widgets::Drop::Inside},std::tuple{"Other","Nested",widgets::Drop::Inside},std::tuple{"A","Text",widgets::Drop::Inside},std::tuple{"A","ProgressBar",widgets::Drop::Inside},std::tuple{"A",d.root,widgets::Drop::Before},std::tuple{"A","A",widgets::Drop::After}}){auto before=d;bool rejected=false;try{widgets::Move(d,id,target,drop,{0,0,400,300});}catch(const std::exception&){rejected=true;}Check(rejected&&d==before,"invalid move is atomic");}
 for(int layout:{1,2,3}){auto next=original;next.widgets[0].layout=layout;auto old=ui::ComputeLayout(next,{0,0,400,300});widgets::Move(next,"C","A",widgets::Drop::Before,{0,0,400,300});auto now=ui::ComputeLayout(next,{0,0,400,300});Check(Children(next,next.root)[0]=="C","all layout modes use reordered document");if(layout<3)Check((layout==1?now.at("C").y:now.at("C").x)<(layout==1?now.at("A").y:now.at("A").x),"layout immediately places C before A");else Check(now.at("C").x==old.at("C").x&&now.at("C").y==old.at("C").y,"Stack preserves stacking geometry");Check(widgets::Find(next,"C").zOrder==widgets::Find(original,"C").zOrder,"document order never changes Local Z");}
 auto deep=ui::EmptyDocument();std::string parent=deep.root;for(int i=0;i<64;++i){ui::Widget w;w.id="Depth"+std::to_string(i);w.parent=parent;parent=w.id;deep.widgets.push_back(w);}ui::Widget extra;extra.id="Extra";extra.parent=deep.root;deep.widgets.push_back(extra);Check(ui::Validate(deep).empty(),"maximum valid depth fixture");auto deepBefore=deep;bool excessive=false;try{widgets::Move(deep,"Extra",parent,widgets::Drop::Inside,{0,0,400,300});}catch(const std::exception&){excessive=true;}Check(excessive&&deep==deepBefore,"depth overflow is rejected without mutation");
 auto styled=original;auto& styledC=const_cast<ui::Widget&>(widgets::Find(styled,"C"));styledC.styleAsset="ui/shared.uistyle.json";styledC.styleId="Primary";styledC.styleOverrides={"color"};styledC.clipChildren=true;widgets::Move(styled,"C","Button",widgets::Drop::Inside,{0,0,400,300});Check(widgets::Find(styled,"C").styleOverrides==std::vector<std::string>{"color"}&&widgets::Find(styled,"C").clipChildren,"Button container preserves style overrides and clipping");
 auto& log=Logger::Get();log.BeginRuntimeSession();for(int i=0;i<500;++i)log.Diagnostic("ERROR","scripts/hud.lua","on_update","nil Weapon",40,i==0?"first trace":"last trace");auto summary=log.RuntimeErrors();Check(summary.total==500&&summary.issues.size()==1&&summary.issues[0].count==500,"identical diagnostics group without Console polling");
 Check(summary.issues[0].firstTrace=="first trace"&&summary.issues[0].lastTrace=="last trace","group retains first and last traceback");
 log.ClearMessages();Check(log.RuntimeErrors().total==500,"clearing bounded Console does not destroy summary");
 for(unsigned i=0;i<Logger::MaxRuntimeIssues+10;++i)log.Diagnostic("ERROR","Runtime","",std::to_string(i));
 log.Diagnostic("ERROR","Runtime","",std::string(Logger::MaxDiagnosticText+1,'x'));
 summary=log.RuntimeErrors();Check(summary.issues.size()==Logger::MaxRuntimeIssues&&summary.dropped==12&&summary.truncated>0,"bounded groups and text account for dropped diagnostics");
 log.EndRuntimeSession();Check(!log.RuntimeErrors().active&&log.RuntimeErrors().Copy().find("nil Weapon")!=std::string::npos,"retained summary exports complete errors");
 auto total=log.RuntimeErrors().total;log.Error("outside Play");Check(log.RuntimeErrors().total==total,"editor errors excluded after Stop");log.BeginRuntimeSession();Check(log.RuntimeErrors().total==0,"next Play resets session");log.Error("message containing Lua is still generic");Check(log.RuntimeErrors().issues[0].source=="Runtime"&&log.RuntimeErrors().issues[0].callback.empty()&&log.RuntimeErrors().issues[0].line==0,"legacy message is never guessed to be Lua");log.EndRuntimeSession();log.ClearRuntimeSession();Check(log.RuntimeErrors().session==0,"project/restart clear removes previous summary");
}
#if defined(ENABLE_DX12)
namespace editor {struct WidgetDesignerTestAccess {
 static auto& Session(EditorLayer& e,Application& a){return e.UIDocumentSession(a,e.m_DesignDocument);}
 static void Select(EditorLayer& e,Application& a,const std::string& id){Session(e,a).selected=id;}
 static void Clip(EditorLayer& e){e.m_UIClipId="Fade";}
 static void Console(EditorLayer& e){e.m_ShowConsole=true;e.m_WorkspaceFocus=false;}
 static bool Errors(EditorLayer& e){return e.m_ShowRuntimeErrors;}
};}
struct Files {
 fs::path directory=AssetPaths::GetExecutableDirectory()/("ux2_"+NewSceneIdentity());
 std::shared_ptr<const ProjectContext> project,other,previous=ProjectManager::Active();
 Files(){Check(directory.lexically_normal().parent_path()==AssetPaths::GetExecutableDirectory(),"owned path containment");Check(fs::create_directory(directory),"new owned fixture");project=ProjectManager::Create(directory,"UX2");other=ProjectManager::Create(directory,"Other");ProjectManager::Activate(project);
  std::string error;Check(UILoader::Save("ui/test.ui.json",Fixture(),&error),error);
  for(auto callback:{"on_start","on_update","on_stop","on_destroy"}){auto path=project->ResolveAsset("scripts/"+std::string(callback)+".lua");fs::create_directories(path.parent_path());std::ofstream(path)<<"return {"<<callback<<"=function(self) error('fixture "<<callback<<"') end}";}
  World scene;Check(SceneSerializer::SaveWorld(project->ResolveScene("Scenes/B.json"),scene,&error),error);
 }
 ~Files(){ProjectManager::Activate(previous);if(directory.lexically_normal().parent_path()==AssetPaths::GetExecutableDirectory()){std::error_code ec;fs::remove_all(directory,ec);}}
};
static void Native(){
 Files files;ProjectManager::Activate(files.previous);Application app;Check(app.Initialize(Application::StartupMode::ProjectBrowser),"DX12 initialize");SyntheticEditorInput input;glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());std::string error;Check(app.OpenProject(files.project->descriptorPath,Application::ProjectDecision::Discard,&error),error);
 auto& e=app.GetEditorLayer();ApplicationTestAccess::FrameDelta(app,1.f/60);e.SetDiagnosticViewport(400,300);auto frame=[&](unsigned n=3){app.SetFrameLimit(n);Check(app.Run()==0,"native frames");};
 Check(e.OpenUIDesigner(app,"ui/test.ui.json"),e.GetError());frame(8);
 for(auto name:{"Widget Hierarchy","Widget Inspector"}){auto* w=ImGui::FindWindowByName(name);Check(w&&w->Active,"Designer panel active");ImGui::SetWindowDock(w,0,ImGuiCond_Always);ImGui::SetWindowPos(w,{std::string(name)=="Widget Hierarchy"?0.f:600.f,120});ImGui::SetWindowSize(w,{520,700});}frame();
 auto& session=WidgetDesignerTestAccess::Session(e,app);auto baseline=session.draft;auto history=session.undo.size();
 for(auto id:{baseline.root.c_str(),"Text","Image","ProgressBar","Button"}){WidgetDesignerTestAccess::Select(e,app,id);frame();Check(session.draft==baseline&&session.undo.size()==history,"Inspector draws every type without false dirty");}
 auto* inspector=ImGui::FindWindowByName("Widget Inspector");inspector->StateStorage.SetInt(inspector->GetID("Styles"),1);inspector->StateStorage.SetInt(inspector->GetID("Animations"),1);WidgetDesignerTestAccess::Clip(e);frame();Check(session.draft==baseline,"style and clip controls have no false edits");inspector->StateStorage.SetInt(inspector->GetID("Rect Transform"),0);WidgetDesignerTestAccess::Select(e,app,"A");frame();WidgetDesignerTestAccess::Select(e,app,"B");frame();Check(inspector->StateStorage.GetInt(inspector->GetID("Rect Transform"),1)==0,"section state survives selection");inspector->StateStorage.SetInt(inspector->GetID("Rect Transform"),1);
 // Locate actual hierarchy TreeNodes via hovered ImGui IDs, then deliver mouse drag/drop.
 auto* hierarchy=ImGui::FindWindowByName("Widget Hierarchy");
 auto rowId=[&](const char* name){auto root=ImHashStr("Widget",0,ImHashStr(baseline.root.c_str(),0,hierarchy->ID));return ImHashStr("Widget",0,ImHashStr(name,0,root));};
 auto row=[&](const char* name){ImGui::SetWindowFocus("Widget Hierarchy");for(float y=hierarchy->WorkRect.Min.y;y<hierarchy->WorkRect.Max.y;y+=3){SyntheticEditorInput::Position(hierarchy->Pos.x+240,y);frame(1);if(ImGui::GetHoveredID()==rowId(name))return ImVec2(hierarchy->Pos.x+240,y);}throw std::runtime_error(std::string("hierarchy row not found: ")+name);};
 auto c=row("C"),a=row("A");
 auto drag=[&](ImVec2 from,ImVec2 to,bool cancel){SyntheticEditorInput::Position(from.x,from.y);frame(1);SyntheticEditorInput::Button(0,true);frame(1);SyntheticEditorInput::Position(from.x+20,from.y);frame(2);SyntheticEditorInput::Position(to.x,to.y);frame(2);Check(session.draft==baseline,"drag preview does not mutate document");if(cancel){SyntheticEditorInput::Key(ImGuiKey_Escape,true);frame(1);SyntheticEditorInput::Key(ImGuiKey_Escape,false);frame(1);}SyntheticEditorInput::Button(0,false);frame(3);};
 drag(c,a,true);Check(session.draft==baseline&&session.undo.size()==history,"Escape cancels active hierarchy drag");
 drag(c,a,false);Check(Children(session.draft,session.draft.root)[0]=="C"&&session.undo.size()==history+1,"real native Before drop changes document once");Check(e.UndoUIDocument(app,"ui/test.ui.json")&&session.draft==baseline,"native drag Undo restores baseline");
 WidgetDesignerTestAccess::Select(e,app,"C");Check(e.WidgetCommand(app,"Before","A"),e.GetError());Check(session.undo.size()==history+1&&session.selected=="C","one completed move is one selected transaction");Check(e.UndoUIDocument(app,"ui/test.ui.json")&&session.draft==baseline,"Undo restores document order");Check(e.UndoUIDocument(app,"ui/test.ui.json",true)&&Children(session.draft,session.draft.root)[0]=="C","Redo restores move");
 Check(e.WidgetCommand(app,"Inside","Nested"),e.GetError());Check(widgets::Find(session.draft,"C").parent=="Nested","native reparent");auto before=session.draft;Check(!e.WidgetCommand(app,"Inside","Text")&&session.draft==before,"invalid native drop changes nothing");Check(e.SaveUIDocument(app,"ui/test.ui.json"),e.GetError());Check(e.ReloadUIDocument(app,"ui/test.ui.json")&&session.draft==before,"save reload preserves order parent and references");
 // Runtime pixels must follow document order immediately in an automatic Layout.
 auto visual=Fixture();std::erase_if(visual.widgets,[&](auto& w){return w.id!=visual.root&&w.id!="A"&&w.id!="B"&&w.id!="C";});visual.widgets[0].layout=1;visual.widgets[0].color={0,0,0,1};
 const_cast<ui::Widget&>(widgets::Find(visual,"A")).color={1,0,0,1};const_cast<ui::Widget&>(widgets::Find(visual,"B")).color={0,0,1,1};const_cast<ui::Widget&>(widgets::Find(visual,"C")).color={0,1,0,1};
 Check(e.EditUIDocument(app,"ui/test.ui.json",visual),e.GetError());auto canvas=e.CreateUICanvas(app,"ui/test.ui.json");Check(canvas.IsValid(),"pixel test Canvas");e.SwitchWorkspace(EditorWorkspace::LevelEditing);e.SetUIPreviewVisible(true);e.SetGameView(true);e.SetGridVisible(false);frame(12);
 auto* renderer=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());auto image=renderer->ReadBackViewportPixels();auto pixel=std::size_t(10*400+20)*4;Check(image[pixel]>240&&image[pixel+1]<10,"runtime first row is red A");WidgetDesignerTestAccess::Select(e,app,"C");Check(e.WidgetCommand(app,"Before","A"),e.GetError());frame(8);image=renderer->ReadBackViewportPixels();Check(image[pixel+1]>240&&image[pixel]<10,"reorder immediately renders green C first");Check(e.UndoUIDocument(app,"ui/test.ui.json"),e.GetError());frame();image=renderer->ReadBackViewportPixels();Check(image[pixel]>240&&image[pixel+1]<10,"Undo restores real runtime visual order");e.SetUIPreviewVisible(false);Check(e.DeleteEntity(app,canvas),e.GetError());
 // Lua errors are real callbacks; summary must survive retirement of the Runtime World.
 e.SwitchWorkspace(EditorWorkspace::LevelEditing);for(auto callback:{"on_start","on_update","on_stop","on_destroy"}){auto entity=e.CreateEntity(app,callback);app.GetWorld().AddComponent<LuaScriptComponent>(entity).scriptPath="scripts/"+std::string(callback)+".lua";}
 app.SetEditorPlayMode(true);frame(8);Check(Logger::Get().RuntimeErrors().active,"Play starts capture");auto broken=app.GetWorld().CreateEntity();app.GetWorld().AddComponent<UICanvasComponent>(broken).document="ui/missing.ui.json";frame(12);app.SetEditorPlayMode(false);frame();auto summary=Logger::Get().RuntimeErrors();
 for(auto callback:{"on_start","on_update","on_stop","on_destroy"})Check(std::any_of(summary.issues.begin(),summary.issues.end(),[&](auto& issue){return issue.callback==callback&&issue.line==1&&!issue.source.empty()&&!issue.firstTrace.empty();}),std::string("structured Lua ")+callback+" retained after Stop");
 Check(std::any_of(summary.issues.begin(),summary.issues.end(),[](auto& issue){return issue.source=="Runtime UI"&&issue.callback=="Sync";}),"failed UI resource enters same session summary");
 Check(!summary.active&&WidgetDesignerTestAccess::Errors(e)&&ImGui::FindWindowByName("Runtime Errors")->Active,"non-modal summary opens once after Stop");auto* window=ImGui::FindWindowByName("Runtime Errors");ImGui::ActivateItemByID(window->GetID("Copy All"));frame();Check(std::string(ImGui::GetClipboardText()).find("fixture on_stop")!=std::string::npos,"Copy All uses real clipboard");
 int firstIssueIndex=0;ImGuiWindow* issuesWindow=nullptr;for(auto* w:ImGui::GetCurrentContext()->Windows)if(w->ParentWindow==window&&(w->Flags&ImGuiWindowFlags_ChildWindow))issuesWindow=w;Check(issuesWindow,"summary issues child");ImGui::ActivateItemByID(ImHashStr("Copy Error",0,ImHashData(&firstIssueIndex,sizeof(firstIssueIndex),issuesWindow->ID)));frame();Check(std::string(ImGui::GetClipboardText()).find(summary.issues[0].message)!=std::string::npos,"Copy Error exports selected issue");
 WidgetDesignerTestAccess::Console(e);frame();ImGui::SetWindowFocus("Console / Diagnostics");frame();auto* console=ImGui::FindWindowByName("Console / Diagnostics");Check(console&&console->Active,"existing Console visible");auto barId=console->GetID("ConsoleTabs");auto* bar=ImGui::GetCurrentContext()->TabBars.GetByKey(barId);Check(bar,"existing Console tab bar");ImGuiID diagnosticsId=0,consoleId=0;ImVec2 tabPoint{};
 for(auto& tab:bar->Tabs){auto name=std::string(ImGui::TabBarGetTabName(bar,&tab));if(name=="Console")consoleId=tab.ID;if(name=="Diagnostics"){diagnosticsId=tab.ID;tabPoint={bar->BarRect.Min.x+tab.Offset+tab.Width*.5f,bar->BarRect.GetCenter().y};}}
 Check(diagnosticsId&&consoleId,"Console tab identities");SyntheticEditorInput::Position(tabPoint.x,tabPoint.y);frame(1);SyntheticEditorInput::Button(0,true);frame(1);SyntheticEditorInput::Button(0,false);frame();Check(bar->SelectedTabId==diagnosticsId,"Console diagnostics tab active before navigation");
 ImGui::SetWindowFocus("Runtime Errors");frame();ImGui::ActivateItemByID(ImHashStr("Show in Console",0,ImHashData(&firstIssueIndex,sizeof(firstIssueIndex),issuesWindow->ID)));frame();Check(bar->SelectedTabId==consoleId,"Show in Console selects log tab as well as original record");
 ImGui::SetWindowFocus("Runtime Errors");frame();
 ImGui::ActivateItemByID(window->GetID("#CLOSE"));frame();Check(!WidgetDesignerTestAccess::Errors(e),"closing completed summary does not reopen next frame");
 app.SetEditorPlayMode(true);Check(Logger::Get().RuntimeErrors().session!=summary.session,"repeated Play uses fresh collection");Check(ApplicationTestAccess::GameplayHost(app).loadScene("Scenes/B.json"),"queue travel");frame(12);Logger::Get().Diagnostic("ERROR","Runtime","Travel","after travel");app.SetEditorPlayMode(false);frame();Check(Logger::Get().RuntimeErrors().Copy().find("after travel")!=std::string::npos,"travel retains current Play session diagnostics");
 Check(app.OpenProject(files.other->descriptorPath,Application::ProjectDecision::Discard,&error),error);frame();Check(Logger::Get().RuntimeErrors().session==0&&!WidgetDesignerTestAccess::Errors(e),"Project Switch clears old summary");app.SetEditorPlayMode(true);frame();app.SetEditorPlayMode(false);frame();Check(Logger::Get().RuntimeErrors().total==0&&!WidgetDesignerTestAccess::Errors(e),"clean Play Stop opens nothing");
 auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(gpu&&gpu->GetValidationErrorCount()==0,"zero DX12 validation errors");app.SetEditorPlayMode(true);Logger::Get().Error("owned shutdown diagnostic");app.Shutdown();Check(!Logger::Get().RuntimeErrors().active&&Logger::Get().RuntimeErrors().total==1,"shutdown ends capture without discarding summary");
 Application restarted;Check(restarted.Initialize(Application::StartupMode::ProjectBrowser),"editor restart initialize");Check(Logger::Get().RuntimeErrors().session==0,"restart starts without prior summary");restarted.Shutdown();
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{
#if defined(ENABLE_DX12)
 if(argc>1&&std::string(argv[1])=="--dx12")Native();else
#endif
 Cpu();std::cout<<"Editor UX2: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& ex){std::cerr<<"Editor UX2 failed after "<<checks<<": "<<ex.what()<<'\n';return 1;}}
