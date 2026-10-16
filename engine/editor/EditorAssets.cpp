#include "EditorLayer.h"
#include "ScriptEditor.h"
#include "EditorWidgets.h"
#include "PrecisionInputs.h"
#include "AssetImport.h"
#include "../core/Application.h"
#include "../core/AssetPaths.h"
#include "../ecs/components/LuaScriptComponent.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include "../scripting/Prefab.h"
#include <fstream>
#include "../resources/ResourceManager.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <set>
#include <cstring>
#include <chrono>
#include "../scene/SceneSerializer.h"
namespace editor {
bool EditorLayer::OpenMaterial(Application& app,const std::string& key){
    m_ShowMaterialEditor=m_FocusMaterial=true;
    if(m_MaterialDirty&&key!=m_EditingMaterial){m_MaterialError="Save or Revert the current material before switching assets";return false;}
    m_EditingMaterial=key;m_SelectedAsset=key;m_SelectedAssetKind="Material";if(auto* r=app.GetResourceManager())r->LoadAsync<MaterialResource>(std::filesystem::u8path(key));return true;
}
bool EditorLayer::AssignAsset(Application& app,ecs::Entity e,ecs::AssetKind kind,const std::string& key){
    if(!ecs::ComponentRegistry::ValidateAssetKey(key,kind,&m_EditorError))return false;
    FinishTransaction(app);
    if(kind==ecs::AssetKind::Environment){BeginSceneTransaction(app,"Assign environment");app.GetEnvironmentSettings().assetPath=key;app.GetEnvironmentSettings().enabled=true;FinishTransaction(app);return true;}
    auto& w=app.GetWorld();if(!w.IsAlive(e))return false;auto before=CaptureSelectedEntity(app,e);const auto& r=ecs::ComponentRegistry::Builtins();
    if(kind==ecs::AssetKind::Mesh){if(!w.HasComponent<ecs::MeshRendererComponent>(e)&&!r.Add(w,e,ecs::ComponentId("MeshRenderer"),&m_EditorError))return false;w.GetComponent<ecs::MeshRendererComponent>(e)->meshPath=key;}
    else if(kind==ecs::AssetKind::Material){if(!w.HasComponent<ecs::MaterialComponent>(e))w.AddComponent<ecs::MaterialComponent>(e);auto& m=*w.GetComponent<ecs::MaterialComponent>(e);m.materialPath=key;m.shaderPath.clear();m.texturePath.clear();m.overrides={};m.slotMaterials.clear();if(auto* mesh=w.GetComponent<ecs::MeshRendererComponent>(e)){mesh->shaderPath.clear();mesh->texturePath.clear();}}
    else if(kind==ecs::AssetKind::Texture||kind==ecs::AssetKind::Shader){if(!w.HasComponent<ecs::MaterialComponent>(e))w.AddComponent<ecs::MaterialComponent>(e);auto& m=*w.GetComponent<ecs::MaterialComponent>(e);(kind==ecs::AssetKind::Texture?m.texturePath:m.shaderPath)=key;}
    else if(kind==ecs::AssetKind::LuaScript){if(!w.HasComponent<ecs::LuaScriptComponent>(e))w.AddComponent<ecs::LuaScriptComponent>(e);w.GetComponent<ecs::LuaScriptComponent>(e)->scriptPath=key;}
    else return false;app.NotifyEditorMutation(e);CommitUndo(app,"Assign asset",std::move(before));return true;
}

bool EditorLayer::AssetReferenced(Application& app,const std::string& key)const{
    auto contains=[&](const auto& j){return AssetCatalog::ContainsReference(j,key);};
    if(contains(SceneSerializer::CaptureWorld(app.GetEditorWorld(),app.GetSceneSettings())))return true;
    auto snapshot=[&](auto&& self,const EntitySnapshot& s)->bool{if(contains(s.components)||(s.settings&&contains(SceneSettingsToJson(*s.settings))))return true;for(auto& r:s.related)if(self(self,r))return true;return false;};
    for(auto* stack:{&m_UndoStack,&m_RedoStack})for(auto& record:*stack)if(snapshot(snapshot,record.snapshot))return true;
    return m_Transaction&&snapshot(snapshot,m_Transaction->before);
}
bool EditorLayer::CreateMaterialAsset(Application& app,const std::string& key,std::string* error){
    if(app.IsEditorPlayMode()||m_MaterialDirty){if(error)*error="Stop Play and save/revert material edits first";return false;}
    const bool ok=m_AssetCatalog.CreateMaterial(key,error);if(ok)m_AssetListsDirty=true;return ok;
}
bool EditorLayer::RenameMaterialAsset(Application& app,const std::string& from,const std::string& to,bool confirmed,std::string* error){
    if(!confirmed||app.IsEditorPlayMode()||m_MaterialDirty||AssetReferenced(app,from)){if(error)*error="Confirm action in Edit mode; asset must have no scene/history references or unsaved edits";return false;}
    const bool ok=m_AssetCatalog.RenameMaterial(from,to,error);if(ok)InvalidateAssetFileUi(app,from,to);return ok;
}
bool EditorLayer::DeleteMaterialAsset(Application& app,const std::string& key,bool confirmed,std::string* error){
    if(!confirmed||app.IsEditorPlayMode()||m_MaterialDirty||AssetReferenced(app,key)){if(error)*error="Confirm action in Edit mode; asset must have no scene/history references or unsaved edits";return false;}
    const bool ok=m_AssetCatalog.DeleteMaterial(key,error);if(ok)InvalidateAssetFileUi(app,key,{});return ok;
}
bool EditorLayer::AssetBrowserOwnsKeyboard()const{
    if(!ImGui::GetCurrentContext())return false;
    for(auto* window=GImGui->NavWindow;window;window=window->ParentWindow)
        if(std::strcmp(window->Name,"Asset Browser")==0)return true;
    return false;
}
std::vector<std::string> EditorLayer::AssetFileBlockers(Application& app,const std::string& key)const{
    std::vector<std::string> reasons;
    if(app.IsEditorPlayMode())reasons.push_back("Stop Play before modifying asset files");
    if(m_MaterialDirty)reasons.push_back("Save or Revert unsaved material edits first");
    try{(void)AssetCatalog::EditablePath(key);}catch(const std::exception& e){reasons.push_back(e.what());return reasons;}
    try{
        if(AssetReferenced(app,key)){
            reasons.push_back("Current scene, Scene Settings or Undo/Redo history references this asset");
            auto& world=app.GetEditorWorld();
            for(auto e:world.Entities()){
                auto components=ecs::ComponentRegistry::Builtins().Capture(world,e);
                for(const auto& [component,value]:components.items())if(AssetCatalog::ContainsReference(value,key)){
                    auto* tag=world.GetComponent<ecs::TagComponent>(e);
                    reasons.push_back("Entity "+(tag?tag->name:std::to_string(e.index))+" / "+component);
                }
            }
        }
    }catch(const std::exception& e){reasons.push_back("Scene reference audit incomplete: "+std::string(e.what()));}
    if(AssetCatalog::ContainsReference(m_VisualSettings.cubemap,key))reasons.push_back("Editor background preferences reference this environment");
    try{auto disk=AssetCatalog::Dependencies(key);reasons.insert(reasons.end(),disk.begin(),disk.end());}catch(const std::exception& e){reasons.push_back("Dependency audit incomplete: "+std::string(e.what()));}return reasons;
}
void EditorLayer::InvalidateAssetFileUi(Application& app,const std::string& old,const std::string& replacement){
    ReleaseAssetPreview(app.GetRenderAdapter());m_AssetMetadataKey.clear();m_AssetMetadata.clear();m_AssetMetadataVersion=0;
    if(auto* resources=app.GetResourceManager()){
        const auto path=std::filesystem::u8path(old);
        resources->Unload<MaterialResource>(path);resources->Unload<TextureResource>(path);resources->Unload<EnvironmentResource>(path);
    }
    const bool editing=m_EditingMaterial==old;if(editing){m_EditingMaterial.clear();m_MaterialError.clear();}
    m_SelectedAsset=replacement;if(replacement.empty())m_SelectedAssetKind.clear();
    else {NavigateAssetFolder(AssetPaths::ToUtf8String(std::filesystem::u8path(replacement).parent_path()));m_AssetFilter.clear();m_AssetKindFilter=0;}
    if(editing&&!replacement.empty())OpenMaterial(app,replacement);
    m_AssetListsDirty=true;
}
bool EditorLayer::RenameAssetFile(Application& app,const std::string& key,const std::string& name,bool confirmed,std::string* error,bool acceptDynamicRisk){
    try{
        if(!confirmed)throw std::runtime_error("Confirm Rename first");
        const auto reasons=AssetFileBlockers(app,key);if(!reasons.empty()){std::string message="Rename blocked:";for(auto& s:reasons)message+="\n- "+s;throw std::runtime_error(message);}
        auto to=AssetCatalog::RenameDestination(key,name);std::string message;
        if(!m_AssetCatalog.RenameAssetFile(key,to,&message,acceptDynamicRisk))throw std::runtime_error(message);
        InvalidateAssetFileUi(app,key,to);if(error)error->clear();return true;
    }catch(const std::exception& e){if(error)*error=e.what();return false;}
}
bool EditorLayer::DeleteAssetFile(Application& app,const std::string& key,bool confirmed,std::string* error,bool acceptDynamicRisk){
    try{
        if(!confirmed)throw std::runtime_error("Confirm Delete first");
        const auto reasons=AssetFileBlockers(app,key);if(!reasons.empty()){std::string message="Delete blocked:";for(auto& s:reasons)message+="\n- "+s;throw std::runtime_error(message);}
        std::string message;if(!m_AssetCatalog.DeleteAssetFile(key,&message,acceptDynamicRisk))throw std::runtime_error(message);
        InvalidateAssetFileUi(app,key,{});if(error)error->clear();return true;
    }catch(const std::exception& e){if(error)*error=e.what();return false;}
}
void EditorLayer::BeginAssetFileAction(Application& app,int action,const std::string& key){
    m_AssetAction=action;m_AssetActionSource=key;m_AssetActionRequested=true;m_AssetActionFocus=true;m_EditorError.clear();
    m_AssetDynamicRiskAccepted=false;m_AssetActionWarnings.clear();
    try{m_AssetActionWarnings=AssetCatalog::AuditDependencies(key).dynamic;}catch(const std::exception& e){m_EditorError=e.what();}
    auto filename=AssetPaths::ToUtf8String(std::filesystem::u8path(key).filename());m_AssetActionKey=filename.substr(0,filename.size()-AssetCatalog::FileSuffix(key).size());
    m_AssetActionBlockers=AssetFileBlockers(app,key);m_AssetActionPath.clear();
    try{m_AssetActionPath=AssetImport::SafeTarget(AssetPaths::ResolveAssetRoot(),key);const auto checked=AssetCatalog::EditablePath(key);m_AssetActionTime=std::filesystem::last_write_time(checked);m_AssetActionSize=std::filesystem::file_size(checked);}catch(const std::exception& e){m_EditorError=e.what();}
}
namespace {
template<class T>const char* LoadState(ResourceManager* resources,const std::string& key){
    auto asset=resources?resources->Get<T>(std::filesystem::u8path(key)):nullptr;if(!asset)return "Not requested";if(asset->IsLoading())return "Loading";if(asset->IsFailed())return "Failed";return "Ready";
}
const char* AssetLoadState(ResourceManager* resources,ecs::AssetKind kind,const std::string& key){
    switch(kind){case ecs::AssetKind::Mesh:return LoadState<MeshResource>(resources,key);case ecs::AssetKind::Texture:return LoadState<TextureResource>(resources,key);case ecs::AssetKind::Material:return LoadState<MaterialResource>(resources,key);case ecs::AssetKind::Shader:return LoadState<ShaderResource>(resources,key);case ecs::AssetKind::Environment:return LoadState<EnvironmentResource>(resources,key);default:return "Indexed";}
}
}
std::string EditorLayer::CreateLuaScriptAsset(Application& app,const std::string& directory){
        try{
            if(app.IsEditorPlayMode())throw std::runtime_error("Stop Play before creating assets");
            if(directory!="scripts"&&!directory.starts_with("scripts/"))throw std::runtime_error("Choose scripts or a subfolder");
            std::string key=directory+"/new_behaviour.lua";unsigned n=1;
            while(std::filesystem::exists(scripting::SafeAsset(key,".lua")))key=directory+"/new_behaviour_"+std::to_string(n++)+".lua";
            auto path=scripting::SafeAsset(key,".lua");std::filesystem::create_directories(path.parent_path());
            std::ofstream file(path);file<<"return {\n  fields = { speed = {type=\"number\", default=1, min=0, max=20} },\n  on_start = function(self) end,\n  on_update = function(self, dt) end,\n  on_event = function(self, event) end,\n  on_destroy = function(self) end\n}\n";file.close();if(!file)throw std::runtime_error("Script write failed");m_SelectedAsset=key;m_SelectedAssetKind="Lua Script";m_AssetListsDirty=true;NavigateAssetFolder(directory);m_EditorError.clear();return key;
        }catch(const std::exception& error){m_EditorError=error.what();}
    return {};
}
std::string EditorLayer::CreatePrefabAsset(Application& app,const std::string& directory){
        try{
            if(app.IsEditorPlayMode())throw std::runtime_error("Stop Play before creating assets");
            if(directory!="prefabs"&&!directory.starts_with("prefabs/"))throw std::runtime_error("Choose prefabs or a subfolder");
            if(!app.GetWorld().IsAlive(m_Selection.active))throw std::runtime_error("Select an entity first");
            auto components=ecs::ComponentRegistry::Builtins().Capture(app.GetWorld(),m_Selection.active);components.erase("SceneIdentity");
            std::string key=directory+"/entity_"+ecs::NewSceneIdentity()+".prefab.json";
            if(!SceneSerializer::WriteAtomic(scripting::SafeAsset(key,".prefab.json"),{{"version",1},{"components",components}},&m_EditorError,false))throw std::runtime_error(m_EditorError);
            m_SelectedAsset=key;m_SelectedAssetKind="Prefab";m_AssetListsDirty=true;NavigateAssetFolder(directory);m_EditorError.clear();return key;
        }catch(const std::exception& error){m_EditorError=error.what();}
    return {};
}
void EditorLayer::DrawAssetBrowser(Application& app){
    const auto start=std::chrono::steady_clock::now();
    struct Timing {double& ms;std::chrono::steady_clock::time_point start;~Timing(){ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}} timing{m_Statistics.assetBrowserMs,start};
    ImGui::SetNextWindowSizeConstraints(ImVec2(280,180),ImVec2(FLT_MAX,FLT_MAX));if(!ImGui::Begin("Asset Browser",&m_ShowAssetBrowser)){ImGui::End();return;}
    const auto& types=m_AssetCatalog.Types();auto* resources=app.GetResourceManager();
    auto openMaterial=[&](const std::string& key){OpenMaterial(app,key);};
    auto action=[&](int kind,const std::string& key){m_AssetAction=kind;m_AssetActionSource=key;m_AssetActionKey=key;m_AssetActionRequested=true;m_EditorError.clear();};
    auto creationMenu=[&](const std::string& directory){
        ImGui::TextDisabled("%s",directory.empty()?"Project root":directory.c_str());
        const bool edit=!app.IsEditorPlayMode();
        if(ImGui::BeginMenu("Create Asset")){
            auto item=[&](const char* label,const char* root,bool extra=true){
                const bool allowed=edit&&extra&&(directory==root||directory.starts_with(std::string(root)+"/"));
                const bool clicked=ImGui::MenuItem(label,nullptr,false,allowed);
                if(!allowed&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s. Use %s or a subfolder.",!edit?"Stop Play first":!extra?"Select an entity first":"Choose a compatible folder",root);
                return clicked;
            };
            if(item("Lua Script","scripts"))CreateLuaScriptAsset(app,directory);
            if(item("Prefab","prefabs",app.GetWorld().IsAlive(m_Selection.active)))CreatePrefabAsset(app,directory);
            if(item("Material","materials"))action(1,directory+"/new.material.json");
            ImGui::EndMenu();
        }
        if(ImGui::MenuItem("New Folder",nullptr,false,edit)){m_FolderParent=directory;m_FolderPopup=true;}
        if(ImGui::MenuItem("Import Asset...",nullptr,false,edit)){BeginAssetImport();m_ImportTargetFolder=directory;}
        if(ImGui::MenuItem("Refresh"))m_AssetListsDirty=true;
    };
    const float typeWidth=std::min(130.f,ImGui::GetContentRegionAvail().x*.32f);
    const float settingsWidth=ImGui::CalcTextSize("...").x+ImGui::GetStyle().FramePadding.x*2;
    ImGui::SetNextItemWidth(std::max(50.f,ImGui::GetContentRegionAvail().x-typeWidth-settingsWidth-ImGui::GetStyle().ItemSpacing.x*2));ImGui::InputTextWithHint("##search","Search assets",&m_AssetFilter);ImGui::SameLine();ImGui::SetNextItemWidth(typeWidth);
    if(ImGui::BeginCombo("##type",m_AssetKindFilter>0&&m_AssetKindFilter<=int(types.size())?types[m_AssetKindFilter-1].name.c_str():"All types")){
        if(ImGui::Selectable("All types",m_AssetKindFilter==0))m_AssetKindFilter=0;for(int i=0;i<int(types.size());++i)if(ImGui::Selectable(types[i].name.c_str(),m_AssetKindFilter==i+1))m_AssetKindFilter=i+1;ImGui::EndCombo();
    }
    ImGui::SameLine();if(ImGui::SmallButton("..."))ImGui::OpenPopup("Asset settings");
    if(ImGui::BeginPopup("Asset settings")){
        if(ImGui::MenuItem("Refresh"))m_AssetListsDirty=true;
        ImGui::Checkbox("Grid",&m_AssetGrid);ImGui::Checkbox("Project Assets",&m_ProjectAssets);
        ImGui::Checkbox("Search project",&m_AssetNavigation.projectSearch);
        if(m_AssetGrid){ImGui::SetNextItemWidth(160);PrecisionSliderFloat("Card size",&m_AssetCardSize,120,320,"%.0f px");}
        if(ImGui::MenuItem("Locate selected",nullptr,false,!m_SelectedAsset.empty())){NavigateAssetFolder(AssetPaths::ToUtf8String(std::filesystem::u8path(m_SelectedAsset).parent_path()));m_AssetFilter.clear();m_AssetKindFilter=0;}
        ImGui::EndPopup();
    }
    m_AssetNavigation.Build(m_AssetCatalog.Folders(),m_AssetCatalog.Entries(),m_AssetCatalog.Revision());
    ImGui::BeginDisabled(m_AssetNavigation.cursor==0);if(ImGui::SmallButton("Back"))m_AssetDirectory=m_AssetNavigation.Back();ImGui::EndDisabled();ImGui::SameLine();
    ImGui::BeginDisabled(m_AssetNavigation.cursor+1>=m_AssetNavigation.history.size());if(ImGui::SmallButton("Forward"))m_AssetDirectory=m_AssetNavigation.Forward();ImGui::EndDisabled();ImGui::SameLine();
    if(ImGui::SmallButton("Up"))NavigateAssetFolder(AssetNavigation::Parent(m_AssetDirectory));
    ImGui::SameLine();if(ImGui::SmallButton("Project"))NavigateAssetFolder("");std::string breadcrumb;
    for(const auto& part:std::filesystem::u8path(m_AssetDirectory)){if(!breadcrumb.empty())breadcrumb+="/";breadcrumb+=AssetPaths::ToUtf8String(part);ImGui::SameLine();ImGui::TextDisabled("/");ImGui::SameLine();ImGui::PushID(breadcrumb.c_str());if(ImGui::SmallButton(AssetPaths::ToUtf8String(part).c_str()))NavigateAssetFolder(breadcrumb);ImGui::PopID();}
    ImGui::BeginChild("Directories",ImVec2(std::clamp(ImGui::GetContentRegionAvail().x*.2f,100.f,210.f),0),ImGuiChildFlags_Borders);
    if(ImGui::Selectable("Project root",m_AssetDirectory.empty()))NavigateAssetFolder("");
    if(ImGui::BeginPopupContextItem("ProjectActions")){creationMenu("");ImGui::EndPopup();}
    auto tree=[&](auto&& self,const std::string& parent)->void {
        for(auto& folder:m_AssetNavigation.children[parent]){
            const auto name=folder.substr(parent.empty()?0:parent.size()+1);ImGui::PushID(folder.c_str());
            const bool leaf=m_AssetNavigation.children[folder].empty();auto flags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_SpanAvailWidth;
            if(m_AssetDirectory==folder)flags|=ImGuiTreeNodeFlags_Selected;if(leaf)flags|=ImGuiTreeNodeFlags_Leaf;
            bool open=ImGui::TreeNodeEx("Folder",flags,"%s (%zu)",name.c_str(),m_AssetNavigation.counts[folder]);
            if(ImGui::IsItemClicked()&&!ImGui::IsItemToggledOpen())NavigateAssetFolder(folder);
            if(ImGui::BeginPopupContextItem("FolderActions")){creationMenu(folder);ImGui::EndPopup();}
            if(open){self(self,folder);ImGui::TreePop();}ImGui::PopID();
        }
    };tree(tree,"");
    if(ImGui::BeginPopupContextWindow("FolderRoot",ImGuiPopupFlags_MouseButtonRight|ImGuiPopupFlags_NoOpenOverItems)){creationMenu(m_AssetDirectory);ImGui::EndPopup();}
    ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("Content");
    std::vector<const AssetEntry*> rows;for(const auto& row:m_AssetCatalog.Entries()){
        if(m_ProjectAssets&&types[row.type].shaderRoot)continue;
        if(m_AssetKindFilter&&int(row.type)+1!=m_AssetKindFilter)continue;if((m_AssetFilter.empty()||!m_AssetNavigation.projectSearch)&&!AssetNavigation::InFolder(row.key,m_AssetDirectory))continue;
        if(!m_AssetFilter.empty()&&std::search(row.key.begin(),row.key.end(),m_AssetFilter.begin(),m_AssetFilter.end(),[](unsigned char a,unsigned char b){return std::tolower(a)==std::tolower(b);})==row.key.end())continue;rows.push_back(&row);
    }
    const int columns=m_AssetGrid?std::max(1,int(ImGui::GetContentRegionAvail().x/(m_AssetCardSize*ImGui::GetStyle().FontScaleDpi))):1;
    for(const auto& folder:m_AssetNavigation.children[m_AssetDirectory]){
        auto name=folder.substr(m_AssetDirectory.empty()?0:m_AssetDirectory.size()+1);ImGui::PushID(folder.c_str());
        if(ImGui::Selectable(("[Folder] "+name).c_str(),false,ImGuiSelectableFlags_AllowDoubleClick)&&ImGui::IsMouseDoubleClicked(0)){NavigateAssetFolder(folder);ImGui::PopID();break;}
        if(ImGui::BeginPopupContextItem("ContentFolderActions")){creationMenu(folder);ImGui::EndPopup();}ImGui::PopID();
    }
    if(rows.empty())ImGui::TextWrapped("No assets match. Change the folder/filter, or right-click to create or import.");
    if(ImGui::BeginTable("ContentItems",columns,ImGuiTableFlags_SizingStretchSame)){
        ImGuiListClipper clipper;clipper.Begin(int((rows.size()+columns-1)/columns),m_AssetGrid?92.f*ImGui::GetStyle().FontScaleDpi:ImGui::GetFrameHeightWithSpacing());
        while(clipper.Step())for(int rowIndex=clipper.DisplayStart;rowIndex<clipper.DisplayEnd;++rowIndex){ImGui::TableNextRow();for(int col=0;col<columns;++col){auto index=rowIndex*columns+col;if(index>=int(rows.size()))break;ImGui::TableSetColumnIndex(col);const auto& entry=*rows[index];const auto& type=types[entry.type];const auto& key=entry.key;auto name=AssetPaths::ToUtf8String(std::filesystem::u8path(key).filename());ImGui::PushID(key.c_str());
            const char* state=AssetLoadState(resources,type.kind,key);std::string label=m_AssetGrid?type.icon+"\n"+name+"\n"+state:type.icon+" "+name+"  |  "+type.name+"  |  "+state;
            if(ImGui::Selectable(label.c_str(),m_SelectedAsset==key,ImGuiSelectableFlags_None,ImVec2(0,m_AssetGrid?84.f*ImGui::GetStyle().FontScaleDpi:0))){m_SelectedAsset=key;m_SelectedAssetKind=type.name;}
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%s / %s\n%s",key.c_str(),type.name.c_str(),state,type.kind==ecs::AssetKind::LuaScript?"Double-click: open in Visual Studio 2022. Drag: assign script.":"Double-click material: open editor. Drag: assign compatible slot.");
            if((type.kind!=ecs::AssetKind::None||type.name=="Imported Scene")&&ImGui::BeginDragDropSource()){AssetPayload payload{};payload.kind=type.kind;std::memcpy(payload.key,key.data(),std::min(key.size(),sizeof(payload.key)-1));ImGui::SetDragDropPayload("WHISP_ASSET",&payload,sizeof(payload));ImGui::Text("%s %s",type.icon.c_str(),name.c_str());ImGui::EndDragDropSource();}
            if(ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0)&&type.kind==ecs::AssetKind::Material)openMaterial(key);
            if(ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0)&&type.kind==ecs::AssetKind::LuaScript)OpenLuaScriptInVisualStudio(key,&m_EditorError);
            if(ImGui::BeginPopupContextItem("AssetActions")){
                if(type.kind==ecs::AssetKind::LuaScript&&ImGui::MenuItem("Open in Visual Studio"))OpenLuaScriptInVisualStudio(key,&m_EditorError);
                if(type.kind==ecs::AssetKind::Prefab&&ImGui::MenuItem("Instantiate Prefab",nullptr,false,!app.IsEditorPlayMode())){
                    try{FinishTransaction(app);auto e=scripting::SpawnPrefab(app.GetWorld(),key,{});EntitySnapshot before;before.entity=e;before.world=e.world;SelectEntityFromUser(app,e);app.NotifyEditorMutation(e);CommitUndo(app,"Instantiate Prefab",before);}catch(const std::exception& error){m_EditorError=error.what();}
                }
                if(type.name=="Imported Scene"&&ImGui::MenuItem("Instantiate imported scene"))InstantiateImportedScene(app,key);
                if(type.kind==ecs::AssetKind::Mesh&&ImGui::MenuItem("Create model entity"))CreateModelEntity(app,key,name);
                if(type.kind==ecs::AssetKind::Material&&ImGui::MenuItem("Open Material Editor"))openMaterial(key);
                if(type.kind!=ecs::AssetKind::None&&ImGui::MenuItem("Assign to Active Entity"))AssignAsset(app,m_Selection.active,type.kind,key);
                std::string blocked;
                if(app.IsEditorPlayMode())blocked="Stop Play before modifying asset files";
                else if(m_MaterialDirty)blocked="Save or Revert material edits first";
                else try{(void)AssetCatalog::EditablePath(key);}catch(const std::exception& e){blocked=e.what();}
                if(ImGui::MenuItem("Rename...","F2",false,blocked.empty())){SelectAsset(key,type.name);BeginAssetFileAction(app,2,key);}
                if(!blocked.empty()&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",blocked.c_str());
                if(ImGui::MenuItem("Delete...","Delete",false,blocked.empty())){SelectAsset(key,type.name);BeginAssetFileAction(app,3,key);}
                if(!blocked.empty()&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",blocked.c_str());
                ImGui::EndPopup();
            }ImGui::PopID();
        }}ImGui::EndTable();
    }
    if(ImGui::BeginPopupContextWindow("ContentCreate",ImGuiPopupFlags_MouseButtonRight|ImGuiPopupFlags_NoOpenOverItems)){creationMenu(m_AssetDirectory);ImGui::EndPopup();}
    ImGui::Separator();ImGui::TextWrapped("Selected: %s",m_SelectedAsset.empty()?"None":m_SelectedAsset.c_str());
    if(!m_SelectedAsset.empty())for(auto& type:types)if(type.name==m_SelectedAssetKind){ImGui::Text("%s %s / %s",type.icon.c_str(),type.name.c_str(),AssetLoadState(resources,type.kind,m_SelectedAsset));if(type.kind!=ecs::AssetKind::None&&ImGui::Button("Assign selected to entity"))AssignAsset(app,m_Selection.active,type.kind,m_SelectedAsset);}
    if(m_SelectedAssetKind=="Mesh"&&!m_SelectedAsset.empty()&&!app.IsEditorPlayMode()&&ImGui::Button("Create Model Entity")){CreateModelEntity(app,m_SelectedAsset,AssetPaths::ToUtf8String(std::filesystem::u8path(m_SelectedAsset).stem()));}
    if(m_SelectedAssetKind=="Imported Scene"&&!m_SelectedAsset.empty()&&!app.IsEditorPlayMode()&&ImGui::Button("Instantiate imported scene"))InstantiateImportedScene(app,m_SelectedAsset);
    if(m_SelectedAssetKind=="Material"&&!m_SelectedAsset.empty()&&ImGui::Button("Open selected material"))openMaterial(m_SelectedAsset);
    if(resources&&!m_SelectedAsset.empty()){
        if(ImGui::Button("Load preview metadata")){
            if(m_SelectedAssetKind=="Material")resources->LoadAsync<MaterialResource>(std::filesystem::u8path(m_SelectedAsset));else if(m_SelectedAssetKind=="Mesh")resources->LoadAsync<MeshResource>(std::filesystem::u8path(m_SelectedAsset));else if(m_SelectedAssetKind=="Texture")resources->LoadAsync<TextureResource>(std::filesystem::u8path(m_SelectedAsset));else if(m_SelectedAssetKind=="Environment")resources->LoadAsync<EnvironmentResource>(std::filesystem::u8path(m_SelectedAsset));
        }
        if(m_SelectedAssetKind=="Material"){auto r=resources->Get<MaterialResource>(std::filesystem::u8path(m_SelectedAsset));if(r&&r->IsLoaded()){const auto& d=r->GetData();ImGui::Text("%s / Metallic %.2f / Roughness %.2f",d.shadingModel==ShadingModel::PBR?"PBR":"Lambert",d.metallic,d.roughness);}}
        if(m_SelectedAssetKind=="Mesh"){auto r=resources->Get<MeshResource>(std::filesystem::u8path(m_SelectedAsset));if(r&&r->IsLoaded()){const auto& d=r->GetData().meshData;ImGui::Text("%zu vertices / %zu indices / %zu material ranges",d.vertices.size(),d.indices.size(),d.submeshes.size());}}
        if(m_SelectedAssetKind=="Texture"){auto r=resources->Get<TextureResource>(std::filesystem::u8path(m_SelectedAsset));if(r&&r->IsLoaded()){const auto& d=r->GetData().textureData;ImGui::Text("%d x %d / RGBA / %.2f MiB decoded",d.width,d.height,d.pixels.size()/1048576.);}}
        if(m_SelectedAssetKind=="Environment"){auto r=resources->Get<EnvironmentResource>(std::filesystem::u8path(m_SelectedAsset));if(r&&r->IsLoaded()){const auto& d=r->GetData();ImGui::Text("HDR %u x %u / sky %u / IBL %u",d.width,d.height,d.sky.resolution,d.environment.resolution);}}
    }
    DrawAssetPreview(app);
    ImGui::TextDisabled("Type previews; content is indexed on Refresh, never every frame.");
    if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::EndChild();
    const auto& io=ImGui::GetIO();
    if(AssetBrowserOwnsKeyboard()&&!app.IsEditorPlayMode()&&!m_SelectedAsset.empty()&&!io.WantTextInput&&!ImGui::IsAnyItemActive()&&!ImGui::IsDragDropActive()&&!io.KeyCtrl&&!io.KeyAlt&&!io.KeyShift&&!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)){
        if(ImGui::IsKeyPressed(ImGuiKey_F2,false))BeginAssetFileAction(app,2,m_SelectedAsset);
        else if(ImGui::IsKeyPressed(ImGuiKey_Delete,false))BeginAssetFileAction(app,3,m_SelectedAsset);
    }
    if(m_AssetActionRequested){ImGui::OpenPopup(m_AssetAction==1?"Material Asset Action":m_AssetAction==2?"Rename Asset":"Delete Asset?");m_AssetActionRequested=false;}
    if(ImGui::BeginPopupModal("Material Asset Action",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextUnformatted("Create a PBR material (schema v4)");
        ImGui::InputText("Asset key / directory",&m_AssetActionKey);
        ImGui::BeginDisabled(app.IsEditorPlayMode()||m_MaterialDirty);
        if(ImGui::Button("Create")){
            if(CreateMaterialAsset(app,m_AssetActionKey,&m_EditorError)){openMaterial(m_AssetActionKey);m_AssetListsDirty=true;ImGui::CloseCurrentPopup();}
        }ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::EndPopup();
    }
    const char* fileDialog=m_AssetAction==2?"Rename Asset":"Delete Asset?";
    if(ImGui::BeginPopupModal(fileDialog,nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        const bool rename=m_AssetAction==2;
        const auto filename=AssetPaths::ToUtf8String(std::filesystem::u8path(m_AssetActionSource).filename());
        if(!rename)ImGui::TextWrapped("Are you sure you want to delete \"%s\"?",filename.c_str());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+520*ImGui::GetStyle().FontScaleDpi);
        ImGui::TextWrapped("File: %s",AssetPaths::ToUtf8String(m_AssetActionPath).c_str());
        if(std::filesystem::weakly_canonical(AssetPaths::ResolveAssetRoot())!=std::filesystem::weakly_canonical(AssetImport::ProjectRoot()))
            ImGui::TextWrapped("Active staged copy only. The next build may restore the original from source assets. Source files are not changed.");
        ImGui::TextWrapped("Referenced assets are blocked. File changes are outside scene Undo.");
        if(!m_AssetActionBlockers.empty()){
            ImGui::BeginChild("Dependencies",ImVec2(520*ImGui::GetStyle().FontScaleDpi,120*ImGui::GetStyle().FontScaleDpi),ImGuiChildFlags_Borders);
            for(const auto& reason:m_AssetActionBlockers)ImGui::TextWrapped("Blocked: %s",reason.c_str());
            ImGui::EndChild();
        }
        if(!m_AssetActionWarnings.empty()){
            ImGui::TextWrapped("Potential dynamic Lua dependencies (not confirmed references to this asset):");
            ImGui::BeginChild("Dynamic Lua risk",ImVec2(520*ImGui::GetStyle().FontScaleDpi,120*ImGui::GetStyle().FontScaleDpi),ImGuiChildFlags_Borders);
            for(const auto& warning:m_AssetActionWarnings)ImGui::TextWrapped("Warning: %s",warning.c_str());ImGui::EndChild();
            ImGui::TextWrapped("Computed paths may break. Unknown Lua paths are NOT automatically updated. Known references remain protected.");
            ImGui::Checkbox("I accept the risk of unknown dynamic Lua paths",&m_AssetDynamicRiskAccepted);
        }
        bool enter=false;
        if(rename){
            if(m_AssetActionFocus)ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(300*ImGui::GetStyle().FontScaleDpi);
            enter=ImGui::InputText("Name",&m_AssetActionKey,ImGuiInputTextFlags_AutoSelectAll|ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();ImGui::TextUnformatted(AssetCatalog::FileSuffix(m_AssetActionSource).c_str());
        }
        m_AssetActionFocus=false;
        const bool dynamicAllowed=m_AssetActionWarnings.empty()||m_AssetDynamicRiskAccepted;
        ImGui::BeginDisabled(app.IsEditorPlayMode()||!m_AssetActionBlockers.empty()||!dynamicAllowed);
        const bool clicked=ImGui::Button(rename?"Rename":"Delete");
        if((clicked||enter)&&!app.IsEditorPlayMode()&&m_AssetActionBlockers.empty()&&dynamicAllowed){
            bool ok=false;
            try{
                const auto path=AssetCatalog::EditablePath(m_AssetActionSource);
                if(path!=m_AssetActionPath||std::filesystem::last_write_time(path)!=m_AssetActionTime||std::filesystem::file_size(path)!=m_AssetActionSize)
                    throw std::runtime_error("File changed since this dialog opened. Cancel and review it again.");
                ok=rename?RenameAssetFile(app,m_AssetActionSource,m_AssetActionKey,true,&m_EditorError,m_AssetDynamicRiskAccepted):DeleteAssetFile(app,m_AssetActionSource,true,&m_EditorError,m_AssetDynamicRiskAccepted);
            }catch(const std::exception& e){m_EditorError=e.what();}
            if(ok)ImGui::CloseCurrentPopup();
            else {
                // A dependency may have appeared while the modal was open.
                // Refresh only on failure, not by scanning the project each frame.
                try{auto warnings=AssetCatalog::AuditDependencies(m_AssetActionSource).dynamic;if(warnings!=m_AssetActionWarnings)m_AssetDynamicRiskAccepted=false;m_AssetActionWarnings=std::move(warnings);m_AssetActionBlockers=AssetFileBlockers(app,m_AssetActionSource);}catch(const std::exception& failure){m_AssetActionBlockers.push_back(failure.what());}
            }
        }
        ImGui::EndDisabled();ImGui::SameLine();
        if(ImGui::Button("Cancel")||ImGui::IsKeyPressed(ImGuiKey_Escape,false))ImGui::CloseCurrentPopup();
        if(!rename)ImGui::SetItemDefaultFocus();
        if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());
        ImGui::PopTextWrapPos();ImGui::EndPopup();
    }
    DrawImportDialogs(app);
    ImGui::End();
}
}
