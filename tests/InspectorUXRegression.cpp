#define NOMINMAX
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "editor/InspectorControls.h"
#include "editor/EditorWidgets.h"
#include "ecs/components/LuaScriptComponent.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "ecs/components/PrefabInstanceComponent.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "scene/SceneSerializer.h"
#include "SyntheticEditorInput.h"
#include <imgui_internal.h>
#include <fstream>
#include <iostream>
using namespace editor;
static unsigned checks=0;
static void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);++checks;std::cerr<<checks<<": "<<message<<'\n';}
static void Near(float a,float b,const char* message){Check(std::abs(a-b)<.001f,message);}
namespace editor {struct EditorDocumentTestAccess {static bool Resource(EditorLayer& ui,const ecs::PropertyDescriptor& p,std::string& key){return ui.DrawInspectorResource(p,key);}};}
struct DropFixture {
    EditorLayer* ui;ecs::PropertyDescriptor property;std::string key="scripts/fields.lua";AssetPayload payload{};bool source=false,changed=false;ImVec2 target;
    static void Draw(ImGuiContext*,ImGuiContextHook* hook){
        auto& f=*static_cast<DropFixture*>(hook->UserData);ImGui::SetNextWindowPos({350,100},ImGuiCond_Always);ImGui::SetNextWindowSize({320,150},ImGuiCond_Always);ImGui::Begin("Resource Drop Fixture");
        if(f.source&&ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern|ImGuiDragDropFlags_SourceNoPreviewTooltip)){ImGui::SetDragDropPayload("WHISP_ASSET",&f.payload,sizeof(f.payload));ImGui::EndDragDropSource();}
        const auto pos=ImGui::GetCursorScreenPos();f.target={pos.x+40,pos.y+12};f.changed|=EditorDocumentTestAccess::Resource(*f.ui,f.property,f.key);ImGui::End();
    }
};
struct TextCapture {
    std::string text;
    static void Begin(ImGuiContext*,ImGuiContextHook*){ImGui::LogToBuffer(0);GImGui->LogWindow=nullptr;}
    static void End(ImGuiContext*,ImGuiContextHook* hook){static_cast<TextCapture*>(hook->UserData)->text=GImGui->LogBuffer.c_str();ImGui::LogFinish();}
};
int main(){try{
    const auto dir=AssetPaths::GetExecutableDirectory();Check(dir.filename()=="inspector-ux-regression","isolated native executable");
    std::filesystem::remove(PreferencesPath());const auto root=dir/ecs::NewSceneIdentity();std::filesystem::create_directories(root);auto project=ProjectManager::Create(root,"InspectorUX");
    std::filesystem::create_directories(project->ResolveAsset("scripts"));
    std::ofstream(project->ResolveAsset("scripts/fields.lua"))<<"return { fields = { speed = {type='number', default=2}, active={type='bool',default=true} } }\n";
    std::filesystem::copy_file(project->ResolveAsset("scripts/fields.lua"),project->ResolveAsset("scripts/drop.lua"));
    Application app;Check(app.Initialize(),"native ImGui DX12 initialization");SyntheticEditorInput input;
    auto& ui=app.GetEditorLayer();auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(gpu,"DX12 adapter");std::string error;
    Check(app.OpenProject(project->descriptorPath,Application::ProjectDecision::Discard,&error),error);
    auto frame=[&](unsigned count=3){app.SetFrameLimit(count);Check(app.Run()==0,"native frames");};frame(8);ui.SwitchWorkspace(EditorWorkspace::LevelEditing);frame();ui.ResetLayout();frame();
    auto& world=app.GetWorld();auto entity=ui.CreateEntity(app,"Inspector \xD0\xA2\xD0\xB5\xD1\x81\xD1\x82");frame();
    auto* window=ImGui::FindWindowByName("Inspector");Check(window&&window->Active,"existing Inspector");ImGui::SetWindowDock(window,0,ImGuiCond_Always);ImGui::SetWindowPos(window,{600,120});
    auto entitySeed=[&]{auto id=ImHashData(&entity.index,sizeof(entity.index),window->ID);return ImHashData(&entity.generation,sizeof(entity.generation),id);};
    auto component=[&](const char* name){return ImHashStr(name,0,entitySeed());};
    auto property=[&](const char* c,const char* p){return ImHashStr(p,0,ImHashStr("Properties",0,component(c)));};
    auto axis=[&](const char* p,int n){return ImHashStr("##axis",0,ImHashData(&n,sizeof(n),property("Transform",p)));};
    auto key=[&](ImGuiKey k){SyntheticEditorInput::Key(k,true);frame(1);SyntheticEditorInput::Key(k,false);frame();};
    auto type=[&](ImGuiID id,const char* text){ImGui::SetWindowFocus("Inspector");ImGui::ActivateItemByID(id);GImGui->NavNextActivateFlags=ImGuiActivateFlags_PreferInput;frame(1);Check(ImGui::GetActiveID()==id,"native property input activation");SyntheticEditorInput::Queue([&](auto& io){io.AddInputCharactersUTF8(text);});frame(1);key(ImGuiKey_Enter);};
    for(float width:{280.f,380.f,550.f}){
        ImGui::SetWindowSize(window,{width,580});window->Scroll.y=0;frame(5);
        auto* table=ImGui::TableFindByID(ImHashStr("Properties",0,component("Transform")));
        Check(table&&table->ColumnsCount==(width<380?1:2),"adaptive property columns width="+std::to_string(width)+" columns="+std::to_string(table?table->ColumnsCount:0));Check(window->ScrollMax.x==0,"no horizontal overflow");
        Check(gpu->SaveFrameImage("inspector-ux-regression/width-"+std::to_string(int(width))+".ppm"),"native layout capture");
    }
    ImGui::SetWindowSize(window,{550,580});window->Scroll.y=0;frame();auto undo=ui.UndoCount();
    type(axis("position",0),"12.5");Near(world.LocalTransform(entity).position.x,12.5f,"typed vector X");Check(ui.UndoCount()==undo+1,"numeric input is one transaction");ui.UndoEdit(app);Near(world.LocalTransform(entity).position.x,0,"vector Undo");ui.RedoEdit(app);Near(world.LocalTransform(entity).position.x,12.5f,"vector Redo");
    ImGui::SetWindowFocus("Inspector");ImGui::SetFocusID(axis("position",0),window);frame();
    auto* dragTable=ImGui::TableFindByID(ImHashStr("Properties",0,component("Transform")));const auto& valueColumn=dragTable->Columns[1];const auto center=ImVec2(valueColumn.WorkMinX+(valueColumn.WorkMaxX-valueColumn.WorkMinX)/6,dragTable->OuterRect.Min.y+ImGui::GetFrameHeight()*.5f+ImGui::GetStyle().CellPadding.y);
    undo=ui.UndoCount();SyntheticEditorInput::Position(center.x,center.y);frame(1);SyntheticEditorInput::Button(0,true);frame(1);SyntheticEditorInput::Position(center.x+35,center.y);frame(3);SyntheticEditorInput::Button(0,false);frame();
    Check(world.LocalTransform(entity).position.x!=12.5f&&ui.UndoCount()==undo+1,"native numeric drag is one Undo");ui.UndoEdit(app);Near(world.LocalTransform(entity).position.x,12.5f,"drag Undo restores prior value");
    undo=ui.UndoCount();ImGui::ActivateItemByID(axis("position",0));GImGui->NavNextActivateFlags=ImGuiActivateFlags_PreferInput;frame(1);SyntheticEditorInput::Queue([](auto& io){io.AddInputCharactersUTF8("99");});frame(1);key(ImGuiKey_Escape);Near(world.LocalTransform(entity).position.x,12.5f,"Escape restores numeric value");Check(ui.UndoCount()==undo&&!ui.HasPendingTransaction(),"Escape produces no Undo");
    ImGui::ActivateItemByID(axis("position",0));GImGui->NavNextActivateFlags=ImGuiActivateFlags_PreferInput;frame(1);key(ImGuiKey_Tab);Check(GImGui->NavId==axis("position",1)||ImGui::GetActiveID()==axis("position",1),"Tab advances between vector axes");key(ImGuiKey_F11);Check(!ui.IsViewportMaximized(),"active numeric text owns editor hotkeys");key(ImGuiKey_Escape);
    const auto header=ImHashStr("Transform",0,component("Transform"));ImGui::ActivateItemByID(header);frame();Check(window->StateStorage.GetInt(header)==0,"component collapse");ImGui::ActivateItemByID(header);frame();Check(window->StateStorage.GetInt(header)==1,"component expand");
    const auto advanced=ImHashStr("Advanced",0,component("Transform"));ImGui::ActivateItemByID(advanced);frame();Check(window->StateStorage.GetInt(advanced)==1,"Transform advanced opens existing Pivot adapter");ImGui::ActivateItemByID(advanced);frame();
    type(axis("rotation",1),"90");Near(world.LocalTransform(entity).rotation.y,1.5707963f,"rotation degrees adapter");type(axis("scale",2),"2");Near(world.LocalTransform(entity).scale.z,2,"scale adapter");
    const auto beforeSearch=SceneSerializer::CaptureWorld(world,app.GetSceneSettings());undo=ui.UndoCount();
    type(ImHashStr("##PropertySearch",0,window->ID),"POSITION");Check(SceneSerializer::CaptureWorld(world,app.GetSceneSettings())==beforeSearch&&ui.UndoCount()==undo,"case-insensitive search is read-only");
    type(ImHashStr("##PropertySearch",0,window->ID),"");
    // Empty InputText insertion does not clear: use Ctrl+A / Backspace explicitly.
    ImGui::ActivateItemByID(ImHashStr("##PropertySearch",0,window->ID));frame(1);SyntheticEditorInput::Key(ImGuiMod_Ctrl,true);key(ImGuiKey_A);SyntheticEditorInput::Key(ImGuiMod_Ctrl,false);key(ImGuiKey_Backspace);key(ImGuiKey_Enter);
    for(const auto* name:{"MeshRenderer","Material","Rigidbody","Collider","LuaScript"})Check(ui.AddComponent(app,entity,ecs::ComponentId(name)),std::string("existing component adapter ")+name);
    frame();window->StateStorage.SetInt(ImHashStr("Transform",0,component("Transform")),0);window->StateStorage.SetInt(ImHashStr("Lua Script",0,component("LuaScript")),0);window->Scroll.y=0;frame();
    const auto meshPicker=ImHashStr("##value",0,property("MeshRenderer","meshPath"));ImGui::ActivateItemByID(meshPicker);frame();Check(ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel),"native Resource Picker opens");key(ImGuiKey_Escape);
    Check(ui.SetProperty(app,entity,ecs::ComponentId("MeshRenderer"),"meshPath",std::string("models/missing.obj")),ui.GetError());frame();Check(world.GetComponent<ecs::MeshRendererComponent>(entity)->meshPath=="models/missing.obj","missing reference preserved");
    Check(ui.SetProperty(app,entity,ecs::ComponentId("LuaScript"),"scriptPath",std::string("scripts/fields.lua")),ui.GetError());frame();Check(app.GetLuaRuntime().Fields("scripts/fields.lua").size()==2,"Lua exposed schema uses existing runtime");
    window->StateStorage.SetInt(ImHashStr("Lua Script",0,component("LuaScript")),1);window->Scroll.y=0;frame();
    const auto activeField=ImHashStr("##value",0,ImHashStr("LuaProperties",0,ImHashStr("active",0,component("LuaScript"))));undo=ui.UndoCount();ImGui::ActivateItemByID(activeField);frame();Check(world.GetComponent<ecs::LuaScriptComponent>(entity)->properties.value("active",true)==false&&ui.UndoCount()==undo+1,"native Lua bool uses existing transaction");ui.UndoEdit(app);frame();
    DropFixture drop{&ui};drop.property.asset=ecs::AssetKind::LuaScript;drop.property.kind=ecs::PropertyKind::Asset;ImGuiContextHook hook;hook.Type=ImGuiContextHookType_EndFramePre;hook.Callback=DropFixture::Draw;hook.UserData=&drop;const auto hookId=ImGui::AddContextHook(GImGui,&hook);frame();
    auto deliver=[&](ecs::AssetKind kind,const char* value){drop.changed=false;drop.payload.kind=kind;std::memset(drop.payload.key,0,sizeof(drop.payload.key));std::memcpy(drop.payload.key,value,std::strlen(value));SyntheticEditorInput::Position(drop.target.x,drop.target.y);SyntheticEditorInput::Button(0,true);frame(2);drop.source=true;frame(2);SyntheticEditorInput::Button(0,false);frame(1);drop.source=false;frame();};
    deliver(ecs::AssetKind::Mesh,"models/missing.obj");Check(!drop.changed&&drop.key=="scripts/fields.lua","native resource drop rejects wrong asset type");deliver(ecs::AssetKind::LuaScript,"scripts/drop.lua");Check(drop.changed&&drop.key=="scripts/drop.lua","native resource drop accepts matching existing payload");ImGui::RemoveContextHook(GImGui,hookId);Check(ui.SetProperty(app,entity,ecs::ComponentId("LuaScript"),"scriptPath",drop.key),ui.GetError());frame();
    Check(app.SaveCurrentScene(&error),error);const auto scene=app.GetSceneKey();Check(ui.CreatePrefab(app,"prefabs/inspector.prefab.json"),ui.GetError());frame();
    TextCapture capture;ImGuiContextHook logBegin;logBegin.Type=ImGuiContextHookType_NewFramePost;logBegin.Callback=TextCapture::Begin;const auto logBeginId=ImGui::AddContextHook(GImGui,&logBegin);ImGuiContextHook logEnd;logEnd.Type=ImGuiContextHookType_EndFramePre;logEnd.Callback=TextCapture::End;logEnd.UserData=&capture;const auto logEndId=ImGui::AddContextHook(GImGui,&logEnd);
    Check(ui.SetProperty(app,entity,ecs::ComponentId("LuaScript"),"enabled",false),ui.GetError());frame();Check(capture.text.find("Enabled *")!=std::string::npos,"native Inspector labels local Prefab override");Check(ui.RevertPrefab(app,entity),ui.GetError());frame();Check(world.GetComponent<ecs::LuaScriptComponent>(entity)->enabled&&capture.text.find("Enabled *")==std::string::npos,"Prefab Revert restores inherited value and removes marker");ImGui::RemoveContextHook(GImGui,logBeginId);ImGui::RemoveContextHook(GImGui,logEndId);
    auto meshOnly=ui.CreateEntity(app,"Mesh measurement");Check(ui.AddComponent(app,meshOnly,ecs::ComponentId("MeshRenderer")),ui.GetError());Check(ui.AddComponent(app,meshOnly,ecs::ComponentId("Material")),ui.GetError());
    auto transformOnly=ui.CreateEntity(app,"Transform measurement");auto luaOnly=ui.CreateEntity(app,"Lua measurement");Check(ui.AddComponent(app,luaOnly,ecs::ComponentId("LuaScript")),ui.GetError());Check(ui.SetProperty(app,luaOnly,ecs::ComponentId("LuaScript"),"scriptPath",std::string("scripts/fields.lua")),ui.GetError());
    auto point=ui.CreateLight(app,LightType::Point),spot=ui.CreateLight(app,LightType::Spot),directional=ui.CreateLight(app,LightType::Directional);ui.SelectEntity(spot);frame();
    Check(world.HasComponent<ecs::SpotLightComponent>(spot),"Spot adapter rendered without changing component");
    std::ofstream perf(dir/"performance.csv");perf<<"case,mean_ms,path_probes\n";
    for(const auto& sample:std::vector<std::pair<std::string,std::vector<ecs::Entity>>>{{"none",{}},{"transform",{transformOnly}},{"mesh",{meshOnly}},{"spot",{spot}},{"lua",{luaOnly}},{"multi",{point,spot,directional}}}){
        ui.ClearSelection();for(auto selected:sample.second)ui.SelectViewportEntity(app,selected,!ui.GetSelectedEntities().empty(),false);frame(15);std::uint64_t count=0;double total=0;for(int i=0;i<32;++i){frame(1);total+=ui.GetStatistics().inspectorMs;count+=ui.GetStatistics().inspectorPathProbes;}perf<<sample.first<<','<<total/32<<','<<count<<'\n';Check(count==0,"steady Inspector frame has no path probes");
    }
    ui.SelectEntity(entity);frame();key(ImGuiKey_F11);Check(ui.IsViewportMaximized(),"F11 maximize");key(ImGuiKey_F11);Check(!ui.IsViewportMaximized(),"F11 restore");app.SetEditorPlayMode(true);frame();Check(app.IsEditorPlayMode(),"Play");app.SetEditorPlayMode(false);frame();Check(!app.IsEditorPlayMode(),"Stop");
    Check(app.SaveCurrentScene(&error),error);const auto savedWorld=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());Check(app.LoadCurrentScene(&error),error);frame();Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==savedWorld,"Scene Save/Load retains Inspector edits");
    const auto scenePath=project->ResolveScene(scene);auto bytes=[](const auto& path){std::ifstream f(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});};const auto savedBytes=bytes(scenePath);
    Check(ui.OpenDocument(app,"prefabs/inspector.prefab.json"),ui.GetError());frame();Check(app.IsPrefabEditing(),"Prefab Document opens existing editor");auto prefabEntity=app.GetWorld().Entities().front();ui.SelectEntity(prefabEntity);Check(ui.SetProperty(app,prefabEntity,ecs::ComponentId("Transform"),"position",ecs::Vec3{4,0,0}),ui.GetError());frame();Check(ui.SavePrefabEditor(app,true),ui.GetError());Check(bytes(scenePath)==savedBytes,"Prefab Save leaves Scene bytes intact");Check(ui.OpenDocument(app,scene),ui.GetError());frame();Check(!app.IsPrefabEditing(),"clean Prefab returns to Scene");
    std::filesystem::create_directories(project->ResolveAsset("ui"));Check(UILoader::Save("ui/inspector.ui.json",ui::EmptyDocument(),&error),error);Check(ui.OpenDocument(app,"ui/inspector.ui.json"),ui.GetError());frame();Check(ui.OpenDocument(app,scene),ui.GetError());frame();Check(!app.IsPrefabEditing()&&app.GetSceneKey()==scene,"UI Document returns to Scene");
    auto selected=app.GetWorld().Entities().front();ui.SelectEntity(selected);frame();window=ImGui::FindWindowByName("Inspector");ImGui::SetWindowDock(window,0,ImGuiCond_Always);ImGui::SetWindowPos(window,{600,120});ImGui::SetWindowSize(window,{550,580});const auto style=ImGui::GetStyle();ImGui::GetStyle().FontScaleDpi=1.5f;frame(5);Check(window->ScrollMax.x==0,"DPI 150 percent has no horizontal overflow");Check(gpu->SaveFrameImage("inspector-ux-regression/dpi-150.ppm"),"DPI native capture");ImGui::GetStyle()=style;frame();
    Check(gpu->GetValidationErrorCount()==0,"zero DX12 validation errors");app.Shutdown();std::cout<<"InspectorUX: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& failure){std::cerr<<"InspectorUX failed after "<<checks<<": "<<failure.what()<<'\n';return 1;}}
