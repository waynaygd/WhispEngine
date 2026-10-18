#define NOMINMAX
#include "EditorTestPreferences.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "editor/HierarchyTree.h"
#include "ecs/Hierarchy.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "scene/SceneSerializer.h"
#include "SyntheticEditorInput.h"
#include "ecs/components/MeshRendererComponent.h"
#include "ecs/components/MaterialComponent.h"
#include <ImGuizmo.h>
#ifdef ENABLE_DX12
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "resources/ResourceManager.h"
#include "resources/MeshResource.h"
#include "resources/MaterialResource.h"
#include "resources/ShaderResource.h"
#include <GLFW/glfw3.h>
#endif
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <iostream>
#include <source_location>
using namespace ecs;
using Json=nlohmann::json;
namespace fs=std::filesystem;
static unsigned checks=0;
static void Check(bool value,const std::string& message,std::source_location at=std::source_location::current()){if(!value)throw std::runtime_error(message+" at "+std::to_string(at.line()));++checks;}
namespace editor {struct EditorHierarchyTreeTestAccess {
    static const auto& Rows(const EditorLayer& ui){return ui.m_HierarchyTreeRows;}
    static const auto& State(const EditorLayer& ui){return ui.m_HierarchyTree;}
    static const auto& Selection(const EditorLayer& ui){return ui.m_Selection;}
};}
struct ApplicationTestAccess {
    static bool New(Application& app,std::string& error){return app.NewSceneInternal(World::DataMode::HierarchyData,&error);}
    static bool Open(Application& app,const std::string& key,std::string& error){return app.OpenSceneInternal(key,true,&error);}
    static bool Project(Application& app,const fs::path& path,std::string& error){return app.OpenProjectInternal(path,Application::ProjectDecision::Discard,true,&error);}
};
static Entity Object(World& world,const std::string& id,const std::string& name={}){auto e=world.CreateEntity();world.AddComponent<SceneIdentityComponent>(e).value=id;world.AddComponent<TransformComponent>(e);world.AddComponent<TagComponent>(e).name=name.empty()?id:name;return e;}
static Entity Find(const World& world,const std::string& id){for(auto e:world.Entities())if(SceneSerializer::EntitySceneId(world,e)==id)return e;return {};}
static void Populate(World& world,bool reverse=false){std::vector<std::string> ids={"root","a","handle","b","z","other"};if(reverse)std::reverse(ids.begin(),ids.end());for(const auto& id:ids)Object(world,id,id=="a"?"Door":id=="handle"?"DoorHandle":id);
    for(const auto& id:ids){auto e=Find(world,id);auto& r=world.AddComponent<RelationshipComponent>(e);if(id=="a"||id=="b"||id=="z")r.parent="root";if(id=="handle")r.parent="a";r.order=id=="other"?20:id=="root"?10:id=="z"?2:1;}
}
static std::vector<std::string> Ids(const World& world,const std::vector<editor::HierarchyTreeRow>& rows){std::vector<std::string> result;for(const auto& row:rows)result.push_back(SceneSerializer::EntitySceneId(world,row.entity));return result;}
static void Data(){World world(World::DataMode::HierarchyData);editor::HierarchyTreeState state;
    auto build=[&]{auto graph=HierarchyGraph::Build(world);Check(bool(graph),"valid graph fixture");return editor::BuildHierarchyTreeRows(world,*graph,state);};
    Check(build().empty(),"empty hierarchy");auto root=Object(world,"single");Check(build().size()==1&&build()[0].depth==0,"single root");world.DestroyEntity(root);Populate(world);const auto original=SceneSerializer::CaptureWorld(world);
    auto rows=build();Check(Ids(world,rows)==std::vector<std::string>({"root","a","handle","b","z","other"}),"graph sibling order then stable ID preorder");Check(rows[0].children&&rows[1].children&&!rows[2].children&&rows[2].depth==2,"root parent leaf depths");
    state.collapsed.insert("a");Check(Ids(world,build())==std::vector<std::string>({"root","a","b","z","other"}),"collapsed branch hides descendants");state.collapsed.insert("root");Check(Ids(world,build())==std::vector<std::string>({"root","other"}),"collapsed root");state.collapsed.erase("root");Check(build().size()==5,"expansion restores child expansion choice");state.collapsed.clear();Check(build().size()==6,"all expanded");
    World reordered(World::DataMode::HierarchyData);Populate(reordered,true);auto graph=HierarchyGraph::Build(reordered);editor::HierarchyTreeState reorderedState;
    Check(Ids(reordered,editor::BuildHierarchyTreeRows(reordered,*graph,reorderedState))==Ids(world,rows),"different ECS storage insertion order deterministic");
    auto document=original;std::reverse(document["entities"].begin(),document["entities"].end());World restored(World::DataMode::HierarchyData);SceneSettings settings;std::string error;Check(SceneSerializer::RestoreWorld(document,restored,settings,&error),error);auto restoredGraph=HierarchyGraph::Build(restored);
    state.collapsed.insert("root");Check(Ids(restored,editor::BuildHierarchyTreeRows(restored,*restoredGraph,state))==Ids(world,rows)&&state.collapsed.empty()&&state.world==restored.GetIdentity(),"publication identity resets expansion and old handles");
    Check(SceneSerializer::CaptureWorld(world)==original,"presentation does not mutate ECS/document");
    World deep(World::DataMode::HierarchyData);std::string previous;for(int i=0;i<12000;++i){auto id="deep-"+std::to_string(i);auto e=Object(deep,id);if(i)deep.AddComponent<RelationshipComponent>(e).parent=previous;previous=id;}
    auto deepGraph=HierarchyGraph::Build(deep);editor::HierarchyTreeState deepState;auto deepRows=editor::BuildHierarchyTreeRows(deep,*deepGraph,deepState);Check(deepRows.size()==12000&&deepRows.back().depth==11999,"deep hierarchy iterative flattening without UI recursion");
    deepState.filter="deep-11999";Check(editor::BuildHierarchyTreeRows(deep,*deepGraph,deepState).size()==12000,"deep search keeps ancestor chain in linear passes");
    editor::HierarchyTreeState search;auto g=HierarchyGraph::Build(world);(void)editor::BuildHierarchyTreeRows(world,*g,search);search.collapsed.insert("root");search.filter="dOoRhAnDlE";
    Check(Ids(world,editor::BuildHierarchyTreeRows(world,*g,search))==std::vector<std::string>({"root","a","handle"})&&search.collapsed.contains("root"),"case-insensitive search temporarily reveals ancestors");
    search.filter.clear();Check(Ids(world,editor::BuildHierarchyTreeRows(world,*g,search))==std::vector<std::string>({"root","other"}),"clearing search restores persistent expansion");
#ifdef _WIN32
    world.GetComponent<TagComponent>(Find(world,"z"))->name="\xD0\x94\xD0\xB2\xD0\xB5\xD1\x80\xD1\x8C";
    search.filter="\xD0\x94\xD0\x92\xD0\x95\xD0\xA0\xD0\xAC";
    Check(Ids(world,editor::BuildHierarchyTreeRows(world,*g,search))==std::vector<std::string>({"root","z"}),"UTF-8 Cyrillic names use invariant Unicode lowercase");
#endif
}
static ImGuiWindow* RowsWindow(){auto* panel=ImGui::FindWindowByName("Hierarchy Tree");Check(panel&&panel->Active,"real hierarchy panel active");for(auto* child:panel->DC.ChildWindows)if(child->Active&&std::string(child->Name).find("/Rows_")!=std::string::npos)return child;throw std::runtime_error("Rows child not found");}
static ImVec2 RowPoint(Application& app,const std::string& id,bool arrow=false){auto* child=RowsWindow();const auto& rows=editor::EditorHierarchyTreeTestAccess::Rows(app.GetEditorLayer());
    auto it=std::find_if(rows.begin(),rows.end(),[&](const auto& row){return SceneSerializer::EntitySceneId(app.GetWorld(),row.entity)==id;});Check(it!=rows.end(),"visible row "+id);
    const float indent=std::min(float(it->depth)*ImGui::GetStyle().IndentSpacing,std::max(0.f,child->WorkRect.GetWidth()*.45f));
    return {child->WorkRect.Min.x+indent+ImGui::GetFontSize()*(arrow?.5f:2.5f),child->DC.CursorStartPos.y+float(it-rows.begin())*ImGui::GetFrameHeightWithSpacing()+ImGui::GetFrameHeight()*.5f};
}
template<class Frame>static void Click(Application& app,Frame& frame,const std::string& id,bool ctrl=false,bool shift=false,bool arrow=false){auto p=RowPoint(app,id,arrow);
    SyntheticEditorInput::Key(ImGuiMod_Ctrl,ctrl);SyntheticEditorInput::Key(ImGuiMod_Shift,shift);SyntheticEditorInput::Position(p.x,p.y);frame();SyntheticEditorInput::Button(0,true);frame();SyntheticEditorInput::Button(0,false);frame();SyntheticEditorInput::Key(ImGuiMod_Ctrl,false);SyntheticEditorInput::Key(ImGuiMod_Shift,false);frame();
}
template<class Frame>static void Search(Frame& frame,const std::string& text){auto* panel=ImGui::FindWindowByName("Hierarchy Tree");const auto p=panel->DC.CursorStartPos;
    SyntheticEditorInput::Position(p.x+60,p.y+ImGui::GetFrameHeight()*.5f);frame();SyntheticEditorInput::Button(0,true);frame();SyntheticEditorInput::Button(0,false);frame();
    SyntheticEditorInput::Key(ImGuiMod_Ctrl,true);SyntheticEditorInput::Key(ImGuiKey_A,true);frame();SyntheticEditorInput::Key(ImGuiKey_A,false);SyntheticEditorInput::Key(ImGuiMod_Ctrl,false);frame();
    SyntheticEditorInput::Key(ImGuiKey_Backspace,true);frame();SyntheticEditorInput::Key(ImGuiKey_Backspace,false);frame();
    if(!text.empty()){SyntheticEditorInput::Queue([&](ImGuiIO& io){io.AddInputCharactersUTF8(text.c_str());});frame();}
    SyntheticEditorInput::Key(ImGuiKey_Enter,true);frame();SyntheticEditorInput::Key(ImGuiKey_Enter,false);frame();
}
template<class Frame>static void Interactions(Application& app,Frame& frame){auto& ui=app.GetEditorLayer();const auto original=SceneSerializer::CaptureWorld(app.GetWorld());auto selection=[&]()->const auto&{return editor::EditorHierarchyTreeTestAccess::Selection(ui);};auto ids=[&]{return Ids(app.GetWorld(),editor::EditorHierarchyTreeTestAccess::Rows(ui));};
    Click(app,frame,"root");Check(selection().items==std::vector<Entity>({Find(app.GetWorld(),"root")})&&selection().active==Find(app.GetWorld(),"root")&&selection().anchor==selection().active,"real row click sets single active/anchor");
    Click(app,frame,"a",true);Check(selection().items.size()==2&&ui.IsSelected(Find(app.GetWorld(),"root"))&&ui.IsSelected(Find(app.GetWorld(),"a")),"Ctrl Parent+Child explicit selection retained");
    Click(app,frame,"a",true);Check(selection().items.size()==1&&selection().active==Find(app.GetWorld(),"root"),"Ctrl toggles child off");
    Click(app,frame,"a");Click(app,frame,"z",false,true);Check(selection().items==std::vector<Entity>({Find(app.GetWorld(),"a"),Find(app.GetWorld(),"handle"),Find(app.GetWorld(),"b"),Find(app.GetWorld(),"z")})&&selection().active==Find(app.GetWorld(),"z")&&selection().anchor==Find(app.GetWorld(),"a"),"Shift range is visible tree preorder, including explicit descendants");
    Click(app,frame,"root",true,true);Check(selection().items.size()==5&&selection().anchor==Find(app.GetWorld(),"a")&&selection().active==Find(app.GetWorld(),"root"),"Ctrl Shift appends visible range and retains anchor");
    Click(app,frame,"handle");const auto childSelection=selection();Click(app,frame,"a",false,false,true);
    Check(ids()==std::vector<std::string>({"root","a","b","z","other"})&&selection().items==childSelection.items&&selection().active==childSelection.active&&selection().anchor==childSelection.anchor,"arrow collapse hides child without selecting parent or dropping hidden selection");
    Click(app,frame,"a",false,false,true);Check(ids().size()==6&&selection().items==childSelection.items,"re-expand preserves selected child");
    Click(app,frame,"a",false,false,true);Click(app,frame,"b",false,true);Check(selection().items==std::vector<Entity>({Find(app.GetWorld(),"b")}),"hidden anchor falls back to clicked visible row, never ECS indices");Click(app,frame,"a",false,false,true);
    Click(app,frame,"handle");Click(app,frame,"root",false,false,true);const auto hidden=selection();
    Search(frame,"dOoRhAnDlE");Check(ids()==std::vector<std::string>({"root","a","handle"})&&selection().items==hidden.items,"real UTF-8 text input reveals ancestor chain without changing selection");
    Search(frame,"root");Check(ids()==std::vector<std::string>({"root"})&&selection().items==hidden.items,"root search retains hidden selected child");
    Search(frame,"DOOR");Check(ids()==std::vector<std::string>({"root","a","handle"}),"child name case-insensitive search");Search(frame,"missing");Check(ids().empty()&&selection().items==hidden.items,"no-match filter preserves explicit selection");
    Search(frame,"");Check(ids()==std::vector<std::string>({"root","other"})&&selection().items==hidden.items&&selection().anchor==hidden.anchor,"search reset restores persistent collapsed root and selection anchor");Click(app,frame,"root",false,false,true);
    Check(SceneSerializer::CaptureWorld(app.GetWorld())==original&&ui.UndoCount()==0&&!ui.IsSceneDirty()&&!ui.HasPendingTransaction(),"tree/search/selection never mutate ECS, history or dirty state");
}
static void CpuUi(){Application app;std::string error;Check(ApplicationTestAccess::New(app,error),error);Populate(app.GetWorld());const auto before=SceneSerializer::CaptureWorld(app.GetWorld());
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1000,700};io.DeltaTime=1.f/60;io.Fonts->AddFontDefault();unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    for(int i=0;i<3;++i){ImGui::NewFrame();app.GetEditorLayer().Render(app,nullptr,0);ImGui::Render();}
    Check(ImGui::FindWindowByName("Hierarchy Tree")&&ImGui::FindWindowByName("Hierarchy Tree")->Active,"production EditorLayer builds real ImGui hierarchy panel");
    Check(editor::EditorHierarchyTreeTestAccess::Rows(app.GetEditorLayer()).size()==6,"production panel uses visible preorder");Check(!ImGui::FindWindowByName("Inspector")&&!ImGui::FindWindowByName("Main Toolbar"),"legacy mutation surfaces stay closed");Check(SceneSerializer::CaptureWorld(app.GetWorld())==before,"UI display preserves authoritative ECS");
    auto frame=[&]{ImGui::NewFrame();app.GetEditorLayer().Render(app,nullptr,0);ImGui::Render();};Interactions(app,frame);ImGui::DestroyContext();
}
#ifdef ENABLE_DX12
struct Fixture {
    fs::path root=fs::current_path()/("editor-tree-"+NewSceneIdentity());std::shared_ptr<const ProjectContext> a,b;
    Fixture(){Check(fs::create_directory(root),"unique owned fixture");a=ProjectManager::Create(root,"A");b=ProjectManager::Create(root,"B");World world(World::DataMode::HierarchyData);Populate(world);
        auto mesh=Find(world,"z");world.GetComponent<TransformComponent>(mesh)->position={0,0,4};world.AddComponent<MeshRendererComponent>(mesh).meshPath="@engine/models/primitives/cube.obj";world.AddComponent<MaterialComponent>(mesh).materialPath="@engine/materials/default_primitive.material.json";
        auto scene=SceneSerializer::CaptureWorld(world);std::string error;Check(SceneSerializer::WriteAtomic(a->ResolveScene("Scenes/Tree.json"),scene,&error),error);std::reverse(scene["entities"].begin(),scene["entities"].end());Check(SceneSerializer::WriteAtomic(a->ResolveScene("Scenes/Reordered.json"),scene,&error),error);Check(SceneSerializer::WriteAtomic(b->ResolveScene(b->startupScene),scene,&error),error);
        scene["entities"][0]["components"]["Relationship"]["parent"]="missing";Check(SceneSerializer::WriteAtomic(a->ResolveScene("Scenes/Invalid.json"),scene,&error),error);
    }
    ~Fixture(){ProjectManager::Activate({});std::error_code error;fs::remove_all(root,error);}
};
static void InactiveLegacyControls(){for(const char* name:{"Scene Hierarchy","Inspector","Main Toolbar","Asset Browser","Scene Settings"}){auto* window=ImGui::FindWindowByName(name);Check(!window||!window->Active,std::string("legacy control remains inactive: ")+name);}Check(!ImGuizmo::IsUsing()&&!ImGuizmo::IsOver(),"H selection does not activate old Gizmo");}
static void Native(){Fixture fixture;Application app;std::string error;Check(app.Initialize(Application::StartupMode::ProjectBrowser),"actual Application DX12 Initialize");
    auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle();glfwHideWindow(window);SyntheticEditorInput synthetic;
    auto frame=[&]{app.SetFrameLimit(2);Check(app.Run()==0,"actual Application/ImGui/DX12 Run");};SyntheticEditorInput::Queue([](ImGuiIO& io){io.AddFocusEvent(true);});frame();Check(app.IsProjectBrowserActive(),"RootOnly Project Browser remains available");
    Check(app.OpenProject(fixture.a->descriptorPath,Application::ProjectDecision::Discard,&error),error);frame();auto* flat=ImGui::FindWindowByName("Scene Hierarchy");Check(flat&&flat->Active,"existing RootOnly flat panel rendered before H activation");
    Check(ApplicationTestAccess::Open(app,"Scenes/Tree.json",error),error);Check(app.SetEditorCameraPose({0,0,-8},0,0),"fixture camera");auto mesh=app.GetResourceManager()->Load<MeshResource>("@engine/models/primitives/cube.obj");app.GetResourceManager()->Load<MaterialResource>("@engine/materials/default_primitive.material.json");app.GetResourceManager()->Load<ShaderResource>("dx12/textured.hlsl");app.GetResourceManager()->DrainProjectLoads();frame();frame();Check(mesh->IsLoaded()&&app.GetRenderStatistics().submitted.drawCalls>0,"actual DX12 scene rendering continues behind safe panel");
    InactiveLegacyControls();Interactions(app,frame);InactiveLegacyControls();auto& ui=app.GetEditorLayer();const auto author=SceneSerializer::CaptureWorld(app.GetEditorWorld(),app.GetSceneSettings());
    auto press=[&](ImGuiKey key){SyntheticEditorInput::Key(key,true);frame();SyntheticEditorInput::Key(key,false);frame();};
    Click(app,frame,"a");const auto original=SceneSerializer::CaptureWorld(app.GetWorld());press(ImGuiKey_Delete);
    SyntheticEditorInput::Key(ImGuiMod_Ctrl,true);frame();press(ImGuiKey_D);press(ImGuiKey_Z);press(ImGuiKey_Y);SyntheticEditorInput::Key(ImGuiMod_Ctrl,false);frame();press(ImGuiKey_W);press(ImGuiKey_E);press(ImGuiKey_R);
    const auto point=RowPoint(app,"a");SyntheticEditorInput::Key(ImGuiMod_Alt,true);SyntheticEditorInput::Position(point.x,point.y);frame();SyntheticEditorInput::Button(0,true);frame();SyntheticEditorInput::Position(point.x+95,point.y+45);frame();Check(!ImGui::IsDragDropActive()&&!ImGuizmo::IsUsing(),"H row drag cannot enter legacy payload/Gizmo path");SyntheticEditorInput::Button(0,false);SyntheticEditorInput::Key(ImGuiMod_Alt,false);frame();
    SyntheticEditorInput::Position(point.x,point.y);frame();SyntheticEditorInput::Button(1,true);frame();SyntheticEditorInput::Button(1,false);frame();
    Check(SceneSerializer::CaptureWorld(app.GetWorld())==original&&ui.UndoCount()==0&&!ui.HasPendingTransaction()&&!ui.IsSceneDirty()&&!ImGui::IsDragDropActive(),"Delete, duplicate, Undo/Redo, W/E/R, Alt drag and context click never mutate H");InactiveLegacyControls();
    Click(app,frame,"handle");Click(app,frame,"root",true);Click(app,frame,"a",false,false,true);const auto editSelection=editor::EditorHierarchyTreeTestAccess::Selection(ui);const auto editIdentity=app.GetEditorWorld().GetIdentity();
    Check(editor::EditorHierarchyTreeTestAccess::State(ui).collapsed.contains("a"),"author expansion prepared for Play");app.SetEditorPlayMode(true);frame();
    Check(app.IsEditorPlayMode()&&app.GetWorld().GetIdentity()!=editIdentity&&ui.GetSelectedEntities().size()==editSelection.items.size(),"Play maps explicit selection to real runtime world");
    for(auto e:ui.GetSelectedEntities())Check(app.GetWorld().IsAlive(e)&&!app.GetEditorWorld().IsAlive(e),"runtime selection uses isolated valid handles");
    Check(editor::EditorHierarchyTreeTestAccess::State(ui).world==app.GetWorld().GetIdentity()&&editor::EditorHierarchyTreeTestAccess::State(ui).collapsed.empty(),"runtime has independent expansion state");
    Click(app,frame,"root",false,false,true);
    auto titleClick=[&]{auto* panel=ImGui::FindWindowByName("Hierarchy Tree");SyntheticEditorInput::Position(panel->Pos.x+ImGui::GetStyle().FramePadding.x+ImGui::GetFontSize()*.5f,panel->Pos.y+ImGui::GetStyle().FramePadding.y+ImGui::GetFontSize()*.5f);frame();SyntheticEditorInput::Button(0,true);frame();SyntheticEditorInput::Button(0,false);frame();};
    titleClick();Check(ImGui::FindWindowByName("Hierarchy Tree")->Collapsed,"real ImGui title collapse");
    const auto retired=Find(app.GetWorld(),"root");app.GetMutationQueue().Destroy(retired);app.ApplyStructuralCommands();const auto replacement=Object(app.GetWorld(),"replacement");Check(replacement.index==retired.index&&replacement.generation!=retired.generation&&!app.GetWorld().IsAlive(retired),"runtime slot reuse preserves generation guard before UI pruning");frame();
    Check(ui.GetSelectedEntities().empty()&&editor::EditorHierarchyTreeTestAccess::Rows(ui).empty(),"collapsed panel still prunes selection and discards row handles");titleClick();Check(!ImGui::FindWindowByName("Hierarchy Tree")->Collapsed,"real ImGui title expand");
    Check(ui.GetSelectedEntities().empty()&&!editor::EditorHierarchyTreeTestAccess::Selection(ui).active.IsValid()&&!editor::EditorHierarchyTreeTestAccess::Selection(ui).anchor.IsValid()&&editor::EditorHierarchyTreeTestAccess::State(ui).collapsed.empty(),"external runtime subtree destruction prunes selection/anchor/expansion without stale handles");
    app.SetEditorPlayMode(false);frame();Check(ui.GetSelectedEntities()==editSelection.items&&ui.GetSelectedEntity()==editSelection.active&&editor::EditorHierarchyTreeTestAccess::Selection(ui).anchor==editSelection.anchor&&editor::EditorHierarchyTreeTestAccess::State(ui).collapsed.contains("a"),"Stop restores A2 selection/history policy and author presentation");
    Check(SceneSerializer::CaptureWorld(app.GetEditorWorld(),app.GetSceneSettings())==author,"Play/UI/runtime destruction never alter authoring scene/settings");
    Search(frame,"Door");const auto beforeInvalid=editor::EditorHierarchyTreeTestAccess::State(ui);const auto beforeSelection=ui.GetSelectedEntities();
    Check(!ApplicationTestAccess::Open(app,"Scenes/Invalid.json",error)&&app.GetWorld().GetIdentity()==editIdentity&&ui.GetSelectedEntities()==beforeSelection&&editor::EditorHierarchyTreeTestAccess::State(ui).filter==beforeInvalid.filter,"invalid graph publication cannot replace active tree/session");frame();
    const auto filterPosition=ImGui::FindWindowByName("Hierarchy Tree")->DC.CursorStartPos;SyntheticEditorInput::Position(filterPosition.x+60,filterPosition.y+ImGui::GetFrameHeight()*.5f);frame();SyntheticEditorInput::Button(0,true);frame();SyntheticEditorInput::Button(0,false);frame();Check(ImGui::GetIO().WantTextInput,"search remains actively edited before publication");
    auto old=ui.GetSelectedEntity();Check(ApplicationTestAccess::Open(app,"Scenes/Reordered.json",error),error);Check(editor::EditorHierarchyTreeTestAccess::Rows(ui).empty()&&ui.GetSelectedEntities().empty(),"world publication clears cached row handles before next UI pass");frame();
    Check(!app.GetWorld().IsAlive(old)&&Ids(app.GetWorld(),editor::EditorHierarchyTreeTestAccess::Rows(ui))==std::vector<std::string>({"root","a","handle","b","z","other"})&&editor::EditorHierarchyTreeTestAccess::State(ui).filter.empty()&&editor::EditorHierarchyTreeTestAccess::State(ui).collapsed.empty()&&!editor::EditorHierarchyTreeTestAccess::Selection(ui).anchor.IsValid(),"scene switch clears selection/anchor/filter/expansion and reordered JSON stays deterministic");
    Click(app,frame,"handle");Check(ApplicationTestAccess::Project(app,fixture.b->descriptorPath,error),error);Check(editor::EditorHierarchyTreeTestAccess::Rows(ui).empty()&&ui.GetSelectedEntities().empty(),"project publication immediately clears row/selection handles");frame();
    Check(editor::EditorHierarchyTreeTestAccess::State(ui).world==app.GetWorld().GetIdentity()&&!ui.GetSelectedEntity().IsValid(),"same stable IDs in another project never inherit selection");
    Check(ApplicationTestAccess::New(app,error),error);frame();Check(editor::EditorHierarchyTreeTestAccess::Rows(ui).empty(),"empty H UI panel");
    std::string previous;for(int i=0;i<2000;++i){auto id="deep-"+std::to_string(i);auto e=Object(app.GetWorld(),id);if(i)app.GetWorld().AddComponent<RelationshipComponent>(e).parent=previous;previous=id;}frame();
    Check(editor::EditorHierarchyTreeTestAccess::Rows(ui).size()==2000&&editor::EditorHierarchyTreeTestAccess::Rows(ui).back().depth==1999&&RowsWindow()->DrawList->VtxBuffer.Size<100000,"native deep tree is flattened and clipped, no recursive widget traversal");
    Check(app.CloseProject(Application::ProjectDecision::Discard,&error),error);frame();Check(app.IsProjectBrowserActive()&&editor::EditorHierarchyTreeTestAccess::Rows(ui).empty(),"Close returns Browser and discards tree presentation");
    Check(app.OpenLegacyWorkspace(&error)&&!app.GetWorld().IsHierarchyData(),error);frame();flat=ImGui::FindWindowByName("Scene Hierarchy");Check(flat&&flat->Active,"explicit Legacy retains old flat hierarchy panel");
    Check(dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter())->GetValidationErrorCount()==0,"DX12 tree Editor validation ERROR/CORRUPTION zero");app.Shutdown();
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{
#ifdef ENABLE_DX12
    if(argc>1&&std::string(argv[1])=="--dx12")Native();else
#endif
    {Data();CpuUi();}std::cout<<"Editor hierarchy tree: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
