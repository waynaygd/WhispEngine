#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#include "ApplicationTestAccess.h"
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "resources/ResourceManager.h"
#include "core/AssetPaths.h"
#include "scripting/Prefab.h"
#include <limits>
#include "ecs/components/UICanvasComponent.h"
#include "ecs/components/LuaScriptComponent.h"
#include "ecs/components/PlayerComponent.h"
#include "scene/SceneSerializer.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#include <GLFW/glfw3native.h>
#include <imgui_internal.h>
#include <fstream>
#include <iostream>
#include <chrono>
using namespace ecs;
static unsigned checks=0;
static void Check(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F>static void Reject(F fn,const char* why){bool failed=false;try{fn();}catch(...){failed=true;}Check(failed,why);}
static ui::Widget& Widget(ui::Document& d,const std::string& id){auto at=std::find_if(d.widgets.begin(),d.widgets.end(),[&](auto& w){return w.id==id;});if(at==d.widgets.end())throw std::runtime_error("fixture Widget missing");return *at;}
struct Owned {
 std::filesystem::path path;
 explicit Owned(std::filesystem::path p):path(std::move(p)){Check(!std::filesystem::exists(path),"owned fixture already exists");}
 ~Owned(){std::error_code ec;std::filesystem::remove(path,ec);}
 void Write(const std::string& text){std::filesystem::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);f<<text;Check(bool(f),"fixture write");}
};
static ui::Document Fixture(){
 auto d=ui::EmptyDocument();d.name="InteractiveFixture";d.referenceResolution={400,300};
 ui::Widget b;b.id="Action";b.parent="Root";b.type="Button";b.position={20,60};b.size={100,50};b.color={1,0,0,1};b.hoverColor={0,1,0,1};b.pressedColor={0,0,1,1};b.disabledColor={.25f,.25f,.25f,1};d.widgets.push_back(b);auto top=b;top.id="Top";top.zOrder=10;top.visible=false;d.widgets.push_back(top);
 ui::Widget text;text.id="Prompt";text.parent="Root";text.type="Text";text.position={20,100};text.size={300,40};text.text="Authored";d.widgets.push_back(text);
 auto ghost=text;ghost.id="Ghost";ghost.position={20,230};d.widgets.push_back(ghost);
 ui::Widget bar;bar.id="Bar";bar.parent="Root";bar.type="ProgressBar";bar.position={20,150};bar.size={100,20};bar.value=.1f;d.widgets.push_back(bar);
 ui::Widget image;image.id="Image";image.parent="Root";image.type="Image";image.position={200,20};image.size={60,50};image.texture="textures/ui3-owned-red.tga";d.widgets.push_back(image);
 d.requiredWidgets={"Action","Prompt","Bar"};return d;
}
static void Tga(Owned& file,bool blue){std::string bytes(18,'\0');bytes[2]=2;bytes[12]=1;bytes[14]=1;bytes[16]=24;bytes+=blue?std::string("\xff\0\0",3):std::string("\0\0\xff",3);file.Write(bytes);}
static const char* Script=R"(
return {
 fields={},
 on_start=function(self)
  self.h=ui.get(self.entity);self.clicks=0;self.fire=0;self.held=0
  Input.bind("UIProbeFire","MouseLeft")
  self.h:set_text("Prompt","Нажмите E")
  self.h:set_progress("Bar",0.65);self.h:set_visible("Ghost",false)
  self.h:set_opacity("Image",0.8);self.h:set_image("Image","textures/ui3-owned-blue.tga")
  self.h:set_color("Bar",{0.1,0.2,0.3,1});self.h:set_enabled("Action",true)
  assert(self.h:get_text("Prompt")=="Нажмите E" and self.h:get_progress("Bar")>0.64)
  assert(not self.h:get_visible("Ghost") and self.h:get_enabled("Action"))
  assert(self.h:get_image("Image")=="textures/ui3-owned-blue.tga" and self.h:get_opacity("Image")>0.79)
  local r,g,b,a=self.h:get_color("Bar");assert(r>0.09 and g>0.19 and b>0.29 and a==1)
  ui.set_input_mode("UIOnly")
 end,
 on_update=function(self,dt)
  if Input.was_pressed("UIProbeFire") then self.fire=self.fire+1 end
  if Input.is_down("UIProbeFire") then self.held=self.held+1 end
 end,
 on_ui_event=function(self,event)
  if event.type=="click" then
   assert(event.source==self.entity and event.owner==self.entity and event.session>0)
   assert(event.canvas:valid())
   self.clicks=self.clicks+1;self.h=ui.get(self.entity)
   self.h:set_text("Prompt","clicked "..self.clicks)
  end
 end
})";
static double Field(Application& app,Entity e,const std::string& key){auto snap=app.GetLuaRuntime().Inspect(e);for(auto& v:snap.fields)if(v.name==nlohmann::json(key).dump())return std::stod(v.value);throw std::runtime_error("missing Lua numeric field "+key);}
int main(){ScopedEditorPreferenceRestore preferences;try{
 auto document=Fixture();Check(ui::Validate(document).empty(),"Button document valid");auto round=nlohmann::json(document).get<ui::Document>();Check(nlohmann::json(round)==nlohmann::json(document),"Button Save/Load state colors");
 auto invalid=document;Widget(invalid,"Action").pressedColor[0]=std::numeric_limits<float>::quiet_NaN();Check(!ui::Validate(invalid).empty(),"invalid Button color");invalid=document;invalid.requiredWidgets.push_back("Missing");Check(!ui::Validate(invalid).empty(),"required Widget IDs");Check(!ui::ValidUTF8(std::string("\xc0\x80",2))&&!ui::ValidUTF8(std::string("\xed\xa0\x80",3)),"strict UTF-8 validation");
 Owned doc(ui::SafeDocumentPath("ui/ui3-owned.ui.json")),script(scripting::SafeAsset("scripts/ui3-owned.lua",".lua"));
 Owned red(AssetPaths::ResolveAssetOutputPath("textures/ui3-owned-red.tga")),blue(AssetPaths::ResolveAssetOutputPath("textures/ui3-owned-blue.tga"));Tga(red,false);Tga(blue,true);script.Write(Script);
 std::string error;Check(UILoader::Save("ui/ui3-owned.ui.json",document,&error),error);
 Application app;Check(app.Initialize(),"editor initialize");SyntheticEditorInput editorInput;ApplicationTestAccess::FrameDelta(app,1.f/60);app.NewScene();auto& editor=app.GetEditorLayer();editor.SetFixedDiagnosticLayout(true);editor.SetDiagnosticViewport(400,300);editor.SetGameView(true);editor.SetGridVisible(false);
 auto settings=app.GetSceneSettings();settings.background.background=EditorBackground::Solid;settings.background.solid={0,0,0};settings.environment.enabled=false;settings.shadows.enabled=false;Check(app.SetSceneSettings(settings),"fixture settings");
 auto canvas=editor.CreateUICanvas(app,"ui/ui3-owned.ui.json");Check(canvas.IsValid(),"Canvas created");auto& behaviour=app.GetWorld().AddComponent<LuaScriptComponent>(canvas);behaviour.scriptPath="scripts/ui3-owned.lua";behaviour.inputEnabled=true;
 const auto authored=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());const auto disk=nlohmann::json(document);
 auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow());auto* native=window->GetGlfwHandle();HWND hwnd=glfwGetWin32Window(native);glfwFocusWindow(native);
 auto frames=[&](unsigned count=2){app.SetFrameLimit(count);Check(app.Run()==0,"native frames");};
 auto point=[&](float px,float py){ImGui::FocusWindow(ImGui::FindWindowByName(editor.IsViewportMaximized()?"Viewport##Maximized":"Viewport"));const auto b=editor.GetViewportBounds();float w=float(editor.GetViewportPixelWidth()),h=float(editor.GetViewportPixelHeight());double x=b[0]+px/w*(b[2]-b[0]),y=b[1]+py/h*(b[3]-b[1]);SyntheticEditorInput::Position(float(x),float(y));SendMessageW(hwnd,WM_MOUSEMOVE,0,MAKELPARAM(int(x),int(y)));TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT),TME_CANCEL|TME_LEAVE,hwnd,0};TrackMouseEvent(&tracking);MSG leave{};while(PeekMessageW(&leave,hwnd,WM_MOUSELEAVE,WM_MOUSELEAVE,PM_REMOVE)){};};
 auto button=[&](bool down){auto pos=window->PointerPosition();double x=pos[0],y=pos[1];PostMessageW(hwnd,down?WM_LBUTTONDOWN:WM_LBUTTONUP,down?MK_LBUTTON:0,MAKELPARAM(int(x),int(y)));SyntheticEditorInput::Button(0,down);frames();};
 auto click=[&](float x,float y){point(x,y);frames();button(true);button(false);};
 auto pixels=[&](){return gpu->ReadBackViewportPixels();};auto color=[&](int x,int y){auto p=pixels();auto i=std::size_t(y*editor.GetViewportPixelWidth()+x)*4;Check(i+3<p.size(),"readback dimensions");return std::array<int,3>{p[i],p[i+1],p[i+2]};};
 frames(12);point(350,270);frames();Check(color(40,80)[0]>240,"Button DX12 normal pixels");
 const auto editEvents=app.GetLuaRuntime().Events().Rows().size();click(40,80);Check(app.GetLuaRuntime().Events().Rows().size()==editEvents,"Edit click never emits gameplay UI event");
 app.GetLuaRuntime().Events().StartCapture();app.SetEditorPlayMode(true);frames(8);Entity runtime{};for(auto e:app.GetWorld().Entities())if(app.GetWorld().HasComponent<UICanvasComponent>(e))runtime=e;
 auto& ui=app.GetRuntimeUI();auto h=ui.Get(runtime);Check(app.GetUIInputMode()=="UIOnly"&&!app.IsGameplayMouseCaptured(),"UIOnly releases actual capture");Check(ui.Read(h,"Prompt").text!="Authored"&&ui.Read(h,"Bar").value==.65f&&!ui.Read(h,"Ghost").visible,"Lua text/progress/visibility");Check(ui.Read(h,"Image").opacity==.8f&&ui.Read(h,"Image").texture.ends_with("blue.tga"),"Lua Image/opacity");frames(6);auto image=color(220,40);Check(image[2]>180&&image[0]<10,"Lua Image reaches DX12 pixels");
 point(40,80);frames();Check(color(40,80)[1]>240,"native hover pixels");button(true);Check(color(40,80)[2]>240,"native pressed pixels");ui.Set(h,"Ghost","visible",false);frames();button(false);Check(Field(app,runtime,"clicks")==1&&Field(app,runtime,"fire")==0,"native click delivered only to UI Lua");
 auto& rows=app.GetLuaRuntime().Events().Rows();Check(std::any_of(rows.begin(),rows.end(),[&](auto& e){return e.kind=="UI"&&e.widget=="Action"&&e.canvas==h.document&&e.target.entity==runtime&&e.status==scripting::EventStatus::Success;}),"Gameplay Event Inspector UI identity/status");
 app.SetUIInputMode("GameAndUI");click(40,80);Check(Field(app,runtime,"clicks")==2&&Field(app,runtime,"fire")==0,"GameAndUI consumes mouse action");click(350,270);Check(Field(app,runtime,"fire")==1,"GameAndUI outside UI reaches gameplay");
 point(40,80);frames();button(true);point(350,270);frames();button(false);Check(Field(app,runtime,"clicks")==2,"release outside cancels Click");
 ui.Set(h,"Action","enabled",false);click(40,80);Check(Field(app,runtime,"clicks")==2,"disabled Button ignores click");Check(color(40,80)[0]>50&&color(40,80)[0]<80,"disabled Button pixels");ui.Set(h,"Action","enabled",true);
 point(40,80);frames();button(true);auto heldBefore=Field(app,runtime,"held");ui.Set(h,"Action","visible",false);frames();Check(Field(app,runtime,"held")==heldBefore,"hidden UI retains mouse gesture consumption");ui.Set(h,"Action","visible",true);button(false);Check(Field(app,runtime,"clicks")==2,"hiding pressed Button cancels gesture");
 app.SetUIInputMode("UIOnly");app.SetEditorPaused(true);click(40,80);Check(Field(app,runtime,"clicks")==3,"UI events dispatch while Lua Update paused");app.SetEditorPaused(false);
 auto other=app.GetWorld().CreateEntity();app.GetWorld().AddComponent<UICanvasComponent>(other).document=h.document;frames();Reject([&]{ui.Get("InteractiveFixture");},"ambiguous document name rejected");auto otherHandle=ui.Get(other);ui.Set(otherHandle,"Prompt","text","second");Check(ui.Read(h,"Prompt").text!="second","Canvas instances independent");app.GetWorld().GetComponent<UICanvasComponent>(other)->sortOrder=10;click(40,80);Check(Field(app,runtime,"clicks")==3,"upper Canvas owns pointer; missing receiver safely ignored");app.GetWorld().DestroyEntity(other);Check(!ui.Valid(otherHandle),"deleted Canvas handle invalid");auto reused=app.GetWorld().CreateEntity();app.GetWorld().AddComponent<UICanvasComponent>(reused).document=h.document;frames();Check(!ui.Valid(otherHandle),"reused index cannot revive handle");app.GetWorld().DestroyEntity(reused);frames();
 auto next=document;Widget(next,"Root").clipChildren=true;Widget(next,"Root").anchorMax={0,0};Widget(next,"Root").size={35,300};Check(UILoader::Save(h.document,next,&error),error);frames(4);Check(!ui.Valid(h),"document reload invalidates revision handle");h=ui.Get(runtime);Check(ui.Read(h,"Prompt").text=="clicked 3","compatible overrides survive reload");click(80,80);Check(Field(app,runtime,"clicks")==3,"clipping rejects outside Button hit");
 next=document;Widget(next,"Prompt").type="Panel";Check(UILoader::Save(h.document,next,&error),error);frames(4);h=ui.Get(runtime);Check(ui.Read(h,"Prompt").text=="Authored","type change drops incompatible override");Check(UILoader::Save(h.document,document,&error),error);frames(4);h=ui.Get(runtime);
 Reject([&]{ui.Set(h,"Prompt","progress",.2);},"wrong widget operation");Reject([&]{ui.Set(h,"Prompt","text",std::string("\xff",1));},"invalid UTF-8 setter");Reject([&]{ui.Set(h,"Bar","progress",2);},"out of range setter");Reject([&]{ui.Set(h,"Image","image","../escape.png");},"unsafe Image key");
 ui.Set(h,"Top","visible",true);auto clicksBeforeZ=Field(app,runtime,"clicks");click(40,80);Check(Field(app,runtime,"clicks")==clicksBeforeZ+1,"upper sibling gets exactly one click");Check(std::find_if(app.GetLuaRuntime().Events().Rows().rbegin(),app.GetLuaRuntime().Events().Rows().rend(),[](auto& e){return e.kind=="UI"&&e.name=="click";})->widget=="Top","sibling Z order chooses receiver");ui.Set(h,"Top","visible",false);
 Check(app.GetLuaRuntime().ReloadScript("scripts/ui3-owned.lua"),"Lua hot reload");frames(4);Check(app.GetLuaRuntime().Error("scripts/ui3-owned.lua").empty()&&Field(app,runtime,"clicks")==0,"Lua reload recreates owner safely");h=ui.Get(runtime);
 app.SetUIInputMode("GameOnly");Check(app.IsGameplayMouseCaptured(),"GameOnly recaptures through existing capture service");app.SetUIInputMode("UIOnly");editor.SetDiagnosticViewport(800,600);frames(4);point(80,160);frames();Check(color(80,160)[1]>240,"resize/DPI coordinate mapping");
 ImGui::FocusWindow(ImGui::FindWindowByName("Viewport"));PostMessageW(hwnd,WM_KEYDOWN,VK_F11,0);SyntheticEditorInput::Key(ImGuiKey_F11,true);frames(1);PostMessageW(hwnd,WM_KEYUP,VK_F11,0);SyntheticEditorInput::Key(ImGuiKey_F11,false);frames(1);Check(editor.IsViewportMaximized(),"native F11");click(80,160);Check(Field(app,runtime,"clicks")==1,"F11 Button click mapping");
 auto old=h;app.SetEditorPlayMode(false);Check(ui.Instances().empty()&&!ui.Valid(old)&&app.GetUIInputMode()=="GameOnly","Stop clears UI state/handles/input overrides");Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==authored,"Editor World and prefab data unchanged");Check(nlohmann::json(*UILoader::Load("ui/ui3-owned.ui.json",{}).data.document)==disk,"Lua never rewrites source document");
 app.SetEditorPlayMode(true);frames(6);auto again=ui.Get("InteractiveFixture");Check(!ui.Valid(old)&&again.session!=old.session&&ui.Read(again,"Prompt").text!="clicked 1","repeat Play starts from authored state");app.SetEditorPlayMode(false);frames();
 Check(editor.OpenUIDesigner(app,"ui/ui3-owned.ui.json"),editor.GetError());auto before=nlohmann::json(editor.GetUIDraft(app,"ui/ui3-owned.ui.json"));Check(editor.WidgetCommand(app,"Add","Button"),editor.GetError());auto added=editor.GetUIDraft(app,"ui/ui3-owned.ui.json");Check(added.widgets.size()==document.widgets.size()+2,"Designer Add Button includes child label");Check(editor.UndoUIDocument(app,"ui/ui3-owned.ui.json"),"Designer Button Undo");Check(nlohmann::json(editor.GetUIDraft(app,"ui/ui3-owned.ui.json"))==before,"Designer Undo exact");Check(editor.UndoUIDocument(app,"ui/ui3-owned.ui.json",true)&&editor.SaveUIDocument(app,"ui/ui3-owned.ui.json"),"Designer Redo Save");
 // Exercise the shipped menu with the real FPS controller and native key messages.
 app.NewScene();Check(app.OpenScene("scenes/interactive_room_interactive_ui.json",&error),error);editor.ClearSelection();frames(6);
 const auto roomAuthored=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());
 app.SetEditorPlayMode(true);frames(8);Entity roomCanvas{},player{};for(auto e:app.GetWorld().Entities()){if(app.GetWorld().HasComponent<UICanvasComponent>(e))roomCanvas=e;if(app.GetWorld().HasComponent<PlayerComponent>(e))player=e;}
 Check(roomCanvas.IsValid()&&player.IsValid(),"interactive demo Canvas and existing FPS player");auto roomUI=ui.Get(roomCanvas);
 auto key=[&](int vk,bool down){PostMessageW(hwnd,down?WM_KEYDOWN:WM_KEYUP,vk,(LPARAM(MapVirtualKeyW(vk,MAPVK_VK_TO_VSC))<<16)|(down?0:(LPARAM(3)<<30)));frames();};
 point(700,500);frames();key('M',true);key('M',false);Check(app.GetUIInputMode()=="UIOnly"&&ui.Read(roomUI,"Menu").visible&&!app.IsGameplayMouseCaptured(),"demo M opens menu with released cursor");
 auto measureUI=[&](const char* name){std::vector<double> layout,submit;ApplicationTestAccess::ObserveFrames(app,[&]{layout.push_back(app.GetRenderStatistics().runtimeUILayoutMs);submit.push_back(app.GetRenderStatistics().runtimeUISubmitMs);});frames(32);ApplicationTestAccess::ObserveFrames(app,{});std::sort(layout.begin(),layout.end());std::sort(submit.begin(),submit.end());nlohmann::json value={{"layoutMedianMs",layout[16]},{"layoutP95Ms",layout[30]},{"submitMedianMs",submit[16]}};std::cout<<"UI_COST "<<name<<" "<<value.dump()<<std::endl;return value;};
 std::filesystem::create_directories("diagnostics/interactive-ui");nlohmann::json costs;costs["menu"]=measureUI("menu");Check(gpu->SaveFrameImage("diagnostics/interactive-ui/menu.ppm"),"demo menu screenshot");
 Check(ui.Read(roomUI,"SanityBar").value!=.65f&&ui.Read(roomUI,"InteractPrompt").text!="[E] Interact / Нажмите E","demo Lua updates HUD");
 // Menu is centered at logical 1920x1080; reference scale at 800x600 is 5/12.
 click(400,280);Check(app.GetUIInputMode()=="GameOnly"&&!ui.Read(roomUI,"Menu").visible&&app.IsGameplayMouseCaptured(),"demo Resume calls Lua and restores FPS capture");
 costs["hud"]=measureUI("hud");costs["viewport"]={800,600};costs["samplesPerState"]=32;std::ofstream("diagnostics/interactive-ui/cost.json")<<costs.dump(2);Check(gpu->SaveFrameImage("diagnostics/interactive-ui/hud.ppm"),"demo HUD screenshot");
 auto playerBefore=app.GetWorld().WorldTransform(player).position;key('W',true);frames(8);key('W',false);auto playerAfter=app.GetWorld().WorldTransform(player).position;Check(std::sqrt((playerAfter.x-playerBefore.x)*(playerAfter.x-playerBefore.x)+(playerAfter.z-playerBefore.z)*(playerAfter.z-playerBefore.z))>.05f,"native W moves FPS player after Resume");
 key(VK_TAB,true);key(VK_TAB,false);Check(!app.IsGameplayMouseCaptured(),"native Tab releases FPS cursor");point(700,500);frames();click(700,500);Check(app.IsGameplayMouseCaptured(),"FPS click recaptures after Tab");
 key('M',true);key('M',false);Check(app.GetUIInputMode()=="UIOnly","demo menu opens from captured FPS");click(400,320);Check(app.GetUIInputMode()=="GameOnly"&&app.IsGameplayMouseCaptured(),"demo Close Menu restores gameplay");
 key(VK_ESCAPE,true);key(VK_ESCAPE,false);Check(!app.IsEditorPlayMode()&&ui.Instances().size()<=1,"native Escape stops Play");Check(!ui.Valid(roomUI)&&SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==roomAuthored,"demo Stop preserves authored scene and invalidates handles");
 app.NewScene();frames();Check(!ui.Valid(roomUI)&&ui.Instances().empty(),"scene switch discards instances");
 Check(gpu->GetValidationErrorCount()==0,"DX12 validation errors zero");std::filesystem::create_directories("diagnostics/interactive-ui");Check(gpu->SaveFrameImage("diagnostics/interactive-ui/editor.ppm"),"native screenshot");app.Shutdown();std::cout<<"Interactive UI: "<<checks<<" checks passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<"INTERACTIVE UI FAILED after "<<checks<<": "<<e.what()<<'\n';return 1;}}
