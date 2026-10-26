#define NOMINMAX
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "editor/EditorTheme.h"
#include "resources/ResourceManager.h"
#include "resources/loaders/MaterialLoader.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#include "SyntheticEditorInput.h"
#include <imgui_internal.h>
#include <fstream>
#include <iostream>
using namespace editor;
namespace editor {struct EditorDocumentTestAccess {
 static int Tool(EditorLayer& e){return e.m_GizmoOperation;}
 static bool Snap(EditorLayer& e){return e.m_Snapping;}
 static bool Grid(EditorLayer& e){return e.m_ShowGrid;}
};}
static unsigned checks=0;
static void Check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);++checks;}
int main(){try{
 auto dir=AssetPaths::GetExecutableDirectory();Check(dir.filename()=="editor-visual-ux-regression","isolated visual regression");std::filesystem::remove(PreferencesPath());
 Application app;Check(app.Initialize(Application::StartupMode::ProjectBrowser),"real Application / ImGui / DX12");SyntheticEditorInput input;auto& editor=app.GetEditorLayer();auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle();
 auto frame=[&](unsigned n=3){app.SetFrameLimit(n);Check(app.Run()==0,"native frames");};
 auto capture=[&](const std::string& name){Check(gpu->SaveFrameImage(AssetPaths::ToUtf8String((dir/(name+".ppm")).lexically_relative(std::filesystem::current_path()))),"capture "+name);};
 auto activate=[&](const char* windowName,const char* item){auto* w=ImGui::FindWindowByName(windowName);Check(w&&w->Active,std::string("active ")+windowName);ImGui::ActivateItemByID(w->GetID(item));frame();};
 auto key=[&](ImGuiKey k){SyntheticEditorInput::Key(k,true);frame(1);SyntheticEditorInput::Key(k,false);frame();};
 frame(8);Check(!editor.HasProjectEdits(),"startup remains clean");capture("project-browser");
 auto* font=ImGui::GetIO().FontDefault;Check(font&&std::string_view(font->GetDebugName())=="Browser UI","proportional UI font is default");for(ImWchar c:{ImWchar(0x416),ImWchar(0x44f),ImWchar(0x25cf),ImWchar(0x25a0),ImWchar(0x25ca),ImWchar(0x25b2)})Check(font->GetFontBaked(16)->FindGlyphNoFallback(c)!=nullptr,"Cyrillic UI glyph");Check(MonoFont()!=font,"technical font is separate");
 auto root=dir/ecs::NewSceneIdentity();std::filesystem::create_directories(root);auto project=ProjectManager::Create(root,"VisualUX");std::string error;editor.RequestProjectOpen(app,AssetPaths::ToUtf8String(project->descriptorPath));frame(8);Check(app.GetProject()!=nullptr,"Project opens without false dirty prompt");
 auto entity=editor.CreatePrimitive(app,"cube");app.GetWorld().GetComponent<ecs::TransformComponent>(entity)->position={0,0,4};editor.SelectEntity(entity);Check(app.SaveCurrentScene(&error),error);frame(10);const auto scene=editor.ActiveDocument();
 MaterialResource material;material.name="Visual material";material.placeholder=false;material.shaderPath="dx12/textured.hlsl";Check(MaterialLoader::Save("materials/visual.material.json",material,&error),error);
 const std::string uiKey="ui/panel.ui.json";auto document=ui::EmptyDocument();document.name="Проверка интерфейса";Check(UILoader::Save(uiKey,document,&error),error);
 std::ofstream(project->ResolveAsset("scripts/visual.lua"))<<"-- Cyrillic UI uses the UI font; source uses monospace\nreturn {}\n";
 Check(editor.CreatePrefab(app,"prefabs/visual.prefab.json"),editor.GetError());Check(app.SaveCurrentScene(&error),error);
 auto toolbarCheck=[&]{auto* w=ImGui::FindWindowByName("Main Toolbar");Check(w&&w->Active&&!w->ScrollbarX&&!w->ScrollbarY,"toolbar has no scrollbars");Check(w->ContentSize.x<=w->Size.x-w->WindowPadding.x*2+1,"toolbar fits width");Check(w->ContentSize.y<=w->Size.y-w->WindowPadding.y*2+1,"toolbar fits height");};
 ApplyEditorTheme(1);glfwSetWindowSize(window,1280,720);frame(8);activate("Main Toolbar","Rotate");Check(EditorDocumentTestAccess::Tool(editor)==1,"Rotate command");activate("Main Toolbar","Move");Check(EditorDocumentTestAccess::Tool(editor)==0,"Move command");activate("Main Toolbar","Snap");Check(EditorDocumentTestAccess::Snap(editor),"Snap command");
 activate("Main Toolbar","Play");Check(app.IsEditorPlayMode(),"single Play command");activate("Main Toolbar","Pause");Check(app.IsEditorPaused(),"Pause command");activate("Main Toolbar","Resume");Check(!app.IsEditorPaused(),"Resume command");activate("Main Toolbar","Stop");Check(!app.IsEditorPlayMode(),"Stop command");
 const bool grid=EditorDocumentTestAccess::Grid(editor);activate("Viewport","Display");Check(!GImGui->OpenPopupStack.empty(),"contextual viewport display popup");auto* display=GImGui->OpenPopupStack.back().Window;Check(display!=nullptr,"viewport display visible");activate(display->Name,"Grid");Check(EditorDocumentTestAccess::Grid(editor)!=grid,"viewport overlay command");key(ImGuiKey_Escape);
 auto* inspector=ImGui::FindWindowByName("Inspector");Check(inspector&&inspector->DockId,"Inspector is docked");
 std::ofstream timings(dir/"ui-cost.csv");timings<<"workspace,width,height,dpi,ui_mean_ms,ui_median_ms,ui_p95_ms,asset_ms,path_probes\n";
 auto measure=[&](const char* name,int width,int height,float dpi){frame(12);double ui=0,assets=0;std::vector<double> samples;std::uint64_t probes=0;for(int i=0;i<16;++i){frame(1);ui+=editor.GetStatistics().uiMs;samples.push_back(editor.GetStatistics().uiMs);assets+=editor.GetStatistics().assetBrowserMs;probes+=editor.GetBrowserStatistics().pathProbes;}std::sort(samples.begin(),samples.end());timings<<name<<','<<width<<','<<height<<','<<dpi<<','<<ui/16<<','<<samples[8]<<','<<samples[15]<<','<<assets/16<<','<<probes<<'\n';};
 for(float dpi:{1.f,1.5f})for(auto size:{ImVec2(1280,720),ImVec2(1600,900),ImVec2(1920,1080)}){
  glfwSetWindowSize(window,int(size.x),int(size.y));ApplyEditorTheme(dpi);frame(8);
  Check(editor.ActivateDocument(app,scene),editor.GetError());frame(8);toolbarCheck();measure("level",int(size.x),int(size.y),dpi);capture("level-"+std::to_string(int(size.x))+"-"+std::to_string(int(dpi*100)));
  Check(editor.OpenDocument(app,uiKey),editor.GetError());frame(8);activate("Canvas Designer","Fit");toolbarCheck();measure("ui",int(size.x),int(size.y),dpi);capture("ui-"+std::to_string(int(size.x))+"-"+std::to_string(int(dpi*100)));
  Check(editor.OpenDocument(app,"materials/visual.material.json"),editor.GetError());frame(12);toolbarCheck();for(const char* name:{"Canvas Designer","Widget Hierarchy","Widget Inspector"}){auto* panel=ImGui::FindWindowByName(name);Check(!panel||!panel->Active,"UI panels do not leak into Material workspace");}Check(gpu->GetMaterialPreviewMetrics().draws>0,"Material Preview remains live");measure("material",int(size.x),int(size.y),dpi);capture("material-"+std::to_string(int(size.x))+"-"+std::to_string(int(dpi*100)));
  Check(editor.ActivateDocument(app,scene),editor.GetError());editor.SwitchWorkspace(EditorWorkspace::AssetManagement);frame(8);measure("assets",int(size.x),int(size.y),dpi);
 }
 ApplyEditorTheme(1);glfwSetWindowSize(window,800,600);Check(editor.ActivateDocument(app,scene),editor.GetError());editor.SwitchWorkspace(EditorWorkspace::LevelEditing);frame(8);toolbarCheck();capture("compact");activate("Main Toolbar","Tools");Check(!GImGui->OpenPopupStack.empty(),"compact Tools popup");key(ImGuiKey_Escape);
 Check(editor.OpenDocument(app,"prefabs/visual.prefab.json"),editor.GetError());frame(8);capture("prefab");editor.BackToSceneDocument(app);frame();Check(!app.IsPrefabEditing()&&editor.ActiveDocument()==scene,"Prefab return retains document identity");
 Check(editor.OpenDocument(app,"scripts/visual.lua"),editor.GetError());frame(8);capture("lua");Check(editor.ActivateDocument(app,scene),editor.GetError());frame();
 editor.SetWorkspaceFocused(true);frame();Check(editor.IsWorkspaceFocused(),"workspace focus");editor.SetWorkspaceFocused(false);frame();ImGui::SetWindowFocus("Viewport");key(ImGuiKey_F11);Check(editor.IsViewportMaximized(),"F11 independent maximize");key(ImGuiKey_F11);Check(!editor.IsViewportMaximized(),"F11 return");
 Check(editor.OpenDocument(app,uiKey),editor.GetError());frame();auto uiId=editor.ActiveDocument();auto edited=editor.GetUIDraft(app,uiKey);edited.name="Unsaved visual draft";Check(editor.EditUIDocument(app,uiKey,edited),editor.GetError());Check(!editor.RequestDocumentNavigation(app,uiId,true),"dirty close requests decision");frame();auto* tabs=ImGui::FindWindowByName("Document Tabs");Check(tabs!=nullptr,"Document Tabs window exists");auto* bar=GImGui->TabBars.GetByKey(tabs->GetID("Documents"));bool dirtyTab=false;if(bar)for(auto& tab:bar->Tabs)dirtyTab|=(tab.Flags&ImGuiTabItemFlags_UnsavedDocument)!=0;Check(dirtyTab,"dirty indicator is drawn on document tab");capture("dirty-transition");activate("Document transition","Cancel");Check(editor.ActiveDocument()==uiId,"Cancel preserves active draft");Check(!editor.RequestDocumentNavigation(app,uiId,true),"dirty close retried");frame();activate("Document transition","Discard");Check(editor.ActiveDocument()!=uiId,"Discard completes close");
 Check(editor.OpenDocument(app,uiKey),editor.GetError());frame();uiId=editor.ActiveDocument();edited=editor.GetUIDraft(app,uiKey);edited.name="Saved visual draft";Check(editor.EditUIDocument(app,uiKey,edited),editor.GetError());Check(!editor.RequestDocumentNavigation(app,uiId,true),"dirty Save transition");frame();activate("Document transition","Save");Check(UILoader::Load(uiKey,{}).data.document->name==edited.name,"Save completes transition at correct destination");
 Check(gpu->GetValidationErrorCount()==0,"zero DX12 validation errors including font atlas");app.Shutdown();std::cout<<"EditorVisualUX: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"EditorVisualUX after "<<checks<<": "<<e.what()<<'\n';return 1;}}
