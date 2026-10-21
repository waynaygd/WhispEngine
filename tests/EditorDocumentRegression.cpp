#define NOMINMAX
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "editor/AssetCatalog.h"
#include "resources/ResourceManager.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "SyntheticEditorInput.h"
#include <imgui_internal.h>
#include <fstream>
#include <iostream>
using namespace editor;
static unsigned checks=0;
static void Check(bool v,const std::string& message){if(!v)throw std::runtime_error(message);++checks;}
namespace editor {
struct EditorDocumentTestAccess {
    static auto& Session(EditorLayer& e,Application& a,const std::string& k){return e.UIDocumentSession(a,k);}
    static void View(EditorLayer& e){e.m_UIZoom=.63f;e.m_UIPan={17,23};e.m_UIFit=false;}
    static bool ViewPreserved(EditorLayer& e){return e.m_UIZoom==.63f&&e.m_UIPan.x==17&&e.m_UIPan.y==23;}
    static void MaterialDirty(EditorLayer& e,const MaterialResource& data){e.m_MaterialSessions[e.m_EditingMaterial].draft=data;e.m_MaterialDirty=true;}
    static void ClearPrompt(EditorLayer& e){e.m_DocumentPrompt=false;}
    static auto DraftCount(EditorLayer& e){return e.m_UIDocuments.size();}
};
}
static std::string Id(EditorLayer& e,const std::string& key){for(auto& d:e.Documents())if(d.key==key)return d.id;throw std::runtime_error("Missing tab: "+key);}
static const EditorDocument& Doc(EditorLayer& e,const std::string& key){for(auto& d:e.Documents())if(d.key==key)return d;throw std::runtime_error(key);}
static ImGuiWindow* Panel(const char* name){auto* w=ImGui::FindWindowByName(name);Check(w&&w->Active,std::string("active ")+name);return w;}
int main(int argc,char** argv){try{
    const auto directory=AssetPaths::GetExecutableDirectory();Check(directory.filename()=="document-regression","isolated settings");
    const bool restart=argc>1&&std::string(argv[1])=="--restore";
    std::filesystem::path descriptor;
    if(restart){std::ifstream file(directory/"fixture.txt");std::string path;std::getline(file,path);descriptor=std::filesystem::u8path(path);}
    else {std::filesystem::remove(PreferencesPath());auto root=directory/ecs::NewSceneIdentity();std::filesystem::create_directories(root);auto p=ProjectManager::Create(root,"DocumentWorkspace");descriptor=p->descriptorPath;std::ofstream file(directory/"fixture.txt");file<<AssetPaths::ToUtf8String(descriptor);}
    Application app;Check(app.Initialize(),"initialize DX12 editor");SyntheticEditorInput input;auto& e=app.GetEditorLayer();std::string error;
    Check(app.OpenProject(descriptor,Application::ProjectDecision::Discard,&error),error);
    auto frame=[&](unsigned n=4){app.SetFrameLimit(n);Check(app.Run()==0,"native frames");};frame(8);
    const auto scene=app.GetSceneKey();const std::string one="ui/one/HUD.ui.json",two="ui/two/HUD.ui.json",material="materials/test.material.json",lua="scripts/document.lua";
    if(restart){
        Check(e.Documents().size()==4,"restart restores scene/UI/material/Lua tabs");Check(e.ActiveDocument()==Id(e,one),"restart restores active UI");Panel("Canvas Designer");Panel("Widget Inspector");Check(Panel("Canvas Designer")->DockId!=0,"restart native docking");Check(!e.UIDraftsDirty(),"only saved resource data restored");
        Check(e.SaveCurrentWorkspace(app),e.GetError());app.Shutdown();std::cout<<"Editor documents restart: "<<checks<<" checks passed\n";return 0;
    }
    auto project=app.GetProject();std::filesystem::create_directories(project->ResolveAsset("ui/one"));std::filesystem::create_directories(project->ResolveAsset("ui/two"));
    Check(UILoader::Save(one,ui::EmptyDocument(),&error),error);Check(UILoader::Save(two,ui::EmptyDocument(),&error),error);Check(e.CreateMaterialAsset(app,material,&error),error);
    {std::ofstream file(project->ResolveAsset(lua));file<<"return { on_update = function(self, dt) end }\n";}
    const auto world=app.GetWorld().GetIdentity();auto entity=app.CreateEmptyEntity("Document selection");e.SelectEntity(entity);e.MarkDirty();Check(app.SaveCurrentScene(&error),error);
    auto sceneID=Id(e,scene);
    e.SetAssetGrid(false);e.NavigateAssetFolder("ui/one");e.SetAssetSearch("");frame();
    auto* browser=Panel("Asset Browser");ImGuiWindow* content=nullptr;for(auto* child:browser->DC.ChildWindows)if(std::string(child->Name).find("/Content_")!=std::string::npos)content=child;
    Check(content!=nullptr,"actual asset browser content child");auto position=content->WorkRect.Min;position.x+=24;position.y+=ImGui::GetTextLineHeight()*.5f;
    const auto doubleClickTime=ImGui::GetIO().MouseDoubleClickTime;ImGui::GetIO().MouseDoubleClickTime=2;
    for(int click=0;click<2;++click){SyntheticEditorInput::Position(position.x,position.y);SyntheticEditorInput::Button(0,true);frame(1);SyntheticEditorInput::Button(0,false);frame(1);}
    ImGui::GetIO().MouseDoubleClickTime=doubleClickTime;frame();auto uiID=Id(e,one);Check(e.ActiveDocument()==uiID,"real UI asset double-click opens document tab");
    auto* canvas=Panel("Canvas Designer");Check(canvas->DockId!=0&&canvas->Size.x>Panel("Widget Hierarchy")->Size.x,"UI canvas central and larger than hierarchy");Panel("Widget Inspector");Panel("Asset Browser");Panel("Document Tabs");
    Check(e.WidgetCommand(app,"Add","Button"),e.GetError());auto draft=e.GetUIDraft(app,one);draft.widgets.back().position={49,63};ui::AnimationClip clip;clip.id="Appear";clip.widget="Button";draft.animations.push_back(clip);
    const std::string styleKey="ui/test.uistyle.json";ui::StyleSheet styles;styles.styles["Primary"]={"Button",{{"color",{.1,.4,.7,1}}}};Check(ui::SaveStyles(styleKey,styles,&error),error);
    for(auto& widget:draft.widgets)if(widget.id=="Button"){widget.styleAsset=styleKey;widget.styleId="Primary";}
    Check(e.EditUIDocument(app,one,draft),e.GetError());styles.styles["Primary"].properties["hoverColor"]={.2,.5,.8,1};Check(e.EditUIStyle(app,styleKey,styles),e.GetError());
    EditorDocumentTestAccess::Session(e,app,one).selected="Button";EditorDocumentTestAccess::View(e);const auto edited=nlohmann::json(e.GetUIDraft(app,one));Check(e.DocumentDirty(Doc(e,one)),"dirty indicator uses UI draft");
    Check(e.OpenDocument(app,two),e.GetError());frame();Check(Id(e,two)!=uiID,"same filename different path distinct IDs");auto count=e.Documents().size();Check(e.OpenDocument(app,one),e.GetError());frame();Check(e.Documents().size()==count,"reopen existing tab deduplicates");Check(EditorDocumentTestAccess::ViewPreserved(e),"per document zoom/pan preserved");
    auto drafts=EditorDocumentTestAccess::DraftCount(e);Check(e.OpenDocument(app,"ui/ONE/HUD.ui.json"),e.GetError());Check(e.Documents().size()==count&&EditorDocumentTestAccess::DraftCount(e)==drafts,"Windows path aliases reuse tab and UI draft");
    Check(e.ActivateDocument(app,sceneID),e.GetError());frame();Check(e.GetSelectedEntity()==entity&&app.GetWorld().GetIdentity()==world,"UI switch keeps scene world/selection");Check(e.ActivateDocument(app,uiID),e.GetError());frame();Check(nlohmann::json(e.GetUIDraft(app,one))==edited&&EditorDocumentTestAccess::Session(e,app,one).selected=="Button","draft/layout/animation/selection preserved");
    Check(e.CanUndoActiveDocument()&&e.UndoActiveDocument(app),"Undo UI after tab switch");Check(e.CanUndoActiveDocument(true)&&e.UndoActiveDocument(app,true),"Redo UI after tab switch");
    Check(!e.CloseDocument(app,uiID,EditorLayer::OpenDecision::Cancel),"dirty close Cancel preserves tab");Check(e.SaveActiveDocument(app),e.GetError());Check(!e.DocumentDirty(Doc(e,one)),"Save clears dirty source");
    Check(e.WidgetCommand(app,"Add","Text"),e.GetError());Check(e.CloseDocument(app,uiID,EditorLayer::OpenDecision::Discard),e.GetError());Check(!e.UIDraftsDirty(),"Discard clears UI draft dirty");Check(e.OpenDocument(app,one),e.GetError());frame();Check(nlohmann::json(e.GetUIDraft(app,one))==edited,"Discard reloads saved UI");uiID=Id(e,one);
    Check(e.SaveCurrentWorkspace(app),e.GetError());const auto dock=Panel("Canvas Designer")->DockId;e.SetWorkspaceFocused(true);frame();Check(e.IsWorkspaceFocused()&&Panel("Canvas Designer")->Size.x>canvas->Viewport->WorkSize.x*.9f,"focus expands central canvas");Check(!ImGui::FindWindowByName("Asset Browser")->Active,"focus temporarily hides utilities");e.SetWorkspaceFocused(false);frame();Check(Panel("Canvas Designer")->DockId==dock,"focus restores original native layout");Panel("Asset Browser");
    auto preview=app.BuildUIDesignerPreview(e.GetUIDraft(app,one),{800,600});Check(!preview.vertices.empty()&&preview.errors.empty(),"DX12 UI designer preview geometry");for(auto& command:preview.commands)Check(app.GetRenderAdapter()->GetTexturePreviewId(command.texture)!=0,"real GPU texture");
    Check(e.OpenDocument(app,material),e.GetError());frame();Check(e.GetWorkspace()==EditorWorkspace::Material,"Material workspace");Check(Panel("Material Editor")->DockId!=0,"material properties central dock");
    auto resource=app.GetResourceManager()->Get<MaterialResource>(material);Check(resource&&resource->IsLoaded(),"actual material loaded");auto data=resource->GetData();data.roughness=.22f;resource->ReplaceData(data,ResourceLoadState::Loaded);app.GetResourceManager()->SetMaterialDirty(material,true);EditorDocumentTestAccess::MaterialDirty(e,data);Check(e.DocumentDirty(Doc(e,material)),"material dirty indicator");Check(e.SaveActiveDocument(app),e.GetError());
    Check(e.OpenDocument(app,lua),e.GetError());frame();Panel("Script Document");Panel("Lua Runtime Inspector");Panel("Console / Diagnostics");Check(e.GetWorkspace()==EditorWorkspace::Script,"Script workspace");
    Check(!e.OpenDocument(app,"ui/no-scene.json"),"unrelated JSON not a scene");EditorDocumentTestAccess::ClearPrompt(e);
    Check(e.CloseDocument(app,Id(e,two)),"close inactive UI tab");
    const auto other="Scenes/other/Main.json";std::filesystem::create_directories(project->ResolveScene(other).parent_path());std::filesystem::copy_file(project->ResolveScene(scene),project->ResolveScene(other));
    Check(e.ActivateDocument(app,sceneID),e.GetError());frame();e.SelectEntity(entity);app.SetEditorCameraPose({1,2,3},.4f,.2f);e.MarkDirty();
    Check(!e.OpenDocument(app,other),"dirty scene switch guarded");EditorDocumentTestAccess::ClearPrompt(e);auto otherID=Id(e,other);Check(app.GetSceneKey()==scene&&e.IsSceneDirty(),"guard preserves unsaved world");Check(e.ActivateDocument(app,otherID,EditorLayer::OpenDecision::Save),e.GetError());frame();Check(e.ActivateDocument(app,sceneID),e.GetError());frame();Check(app.GetCameraPosition().x==1&&e.GetSelectedEntity().IsValid(),"scene camera and stable selection restored");
    app.SetEditorPlayMode(true);frame();app.SetEditorPaused(true);const auto runtime=app.GetWorld().GetIdentity();Check(e.ActivateDocument(app,uiID),e.GetError());frame();Check(app.IsEditorPlayMode()&&app.IsEditorPaused()&&app.GetWorld().GetIdentity()==runtime,"UI navigation preserves paused runtime");Check(!e.ActivateDocument(app,otherID),"scene navigation in Play requires Stop");Check(app.GetWorld().GetIdentity()==runtime,"Cancel preserves runtime world");Check(e.ActivateDocument(app,otherID,EditorLayer::OpenDecision::Discard),e.GetError());frame();Check(!app.IsEditorPlayMode(),"confirmed scene navigation stops Play");
    Check(e.CloseDocument(app,otherID),e.GetError());Check(e.ActivateDocument(app,sceneID),e.GetError());frame();
    SyntheticEditorInput::Key(ImGuiKey_F11,true);frame(1);SyntheticEditorInput::Key(ImGuiKey_F11,false);frame();Check(e.IsViewportMaximized(),"F11 remains functional");SyntheticEditorInput::Key(ImGuiKey_F11,true);frame(1);SyntheticEditorInput::Key(ImGuiKey_F11,false);frame();Check(!e.IsViewportMaximized(),"F11 restore");
    Check(e.ActivateDocument(app,uiID),e.GetError());frame();Check(e.SaveCurrentWorkspace(app),e.GetError());Check(e.PersistPreferences(app),e.GetError());
    auto otherProject=ProjectManager::Create(project->root.parent_path(),"OtherDocumentProject");const auto oldProject=Doc(e,one).project;
    Check(app.OpenProject(otherProject->descriptorPath,Application::ProjectDecision::Discard,&error),error);frame();Check(e.Documents().size()==1&&e.Documents()[0].project!=oldProject,"Project Switch removes old document identities");Check(!e.ActivateDocument(app,uiID),"stale document ID rejected");Check(!e.UIDraftsDirty(),"no stale drafts after project switch");
    Check(app.OpenProject(descriptor,Application::ProjectDecision::Discard,&error),error);frame();Check(e.Documents().size()==4&&e.ActiveDocument()==Id(e,one),"project session restores tabs and active document");
    auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(gpu&&gpu->GetValidationErrorCount()==0,"no DX12 validation errors");Check(gpu->SaveFrameImage("document-regression/workspace.ppm"),"capture actual UI workspace");
    while(!e.Documents().empty()){Check(e.CloseDocument(app,e.Documents().back().id,EditorLayer::OpenDecision::Discard),e.GetError());frame();}
    Check(e.ActiveDocument().empty(),"closing last document clears active entry");Panel("Start Workspace");Check(!ImGui::FindWindowByName("Canvas Designer")->Active,"last UI close hides document panels");
    for(const auto& key:{scene,material,lua,one}){Check(e.OpenDocument(app,key),e.GetError());frame();}
    EditorDocumentTestAccess::Session(e,app,one).selected="Button";Check(e.PersistPreferences(app),e.GetError());
    app.Shutdown();std::cout<<"Editor documents: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& ex){std::cerr<<"EDITOR DOCUMENT FAILED after "<<checks<<": "<<ex.what()<<'\n';return 1;}}
