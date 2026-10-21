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
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#endif
#include "../scene/SceneSerializer.h"
namespace editor {
bool EditorLayer::OpenMaterial(Application& app,const std::string& key){
    return OpenDocument(app,key);
}
bool EditorLayer::AssignAsset(Application& app,ecs::Entity e,ecs::AssetKind kind,const std::string& key){
    if(kind==ecs::AssetKind::UIDocument)return SetProperty(app,e,ecs::ComponentId("UICanvas"),"document",key);
    if(kind==ecs::AssetKind::PostFx)return AddCustomPostFx(app,e,key);
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
    auto snapshot=[&](auto&& self,const EntitySnapshot& s)->bool{if(contains(s.components)||(s.settings&&contains(SceneSettingsToJson(*s.settings))))return true;for(const auto& file:s.files)if(file.key==key||(file.contents&&contains(*file.contents))||(file.expected&&contains(*file.expected)))return true;for(auto& r:s.related)if(self(self,r))return true;return false;};
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
bool EditorLayer::MoveAssetFile(Application& app,const std::string& key,const std::string& destination,bool confirmed,std::string* error,bool acceptDynamicRisk){
    try{
        if(app.IsEditorPlayMode()||!confirmed)throw std::runtime_error("Stop Play and confirm Move first");
        if(!AssetFileBlockers(app,key).empty())throw std::runtime_error("Move blocked by scene/history or disk dependencies");
        if(!m_AssetCatalog.MoveAssetFile(key,destination,error,acceptDynamicRisk))return false;
        InvalidateAssetFileUi(app,key,destination);return true;
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
    if(app.IsPrefabEditing()){m_EditorError="Return from Prefab Editor before modifying asset files";return;}
    m_AssetAction=action;m_AssetActionSource=key;m_AssetActionRequested=true;m_AssetActionFocus=true;m_EditorError.clear();
    m_AssetDynamicRiskAccepted=false;m_AssetActionWarnings.clear();
    try{m_AssetActionWarnings=AssetCatalog::AuditDependencies(key).dynamic;}catch(const std::exception& e){m_EditorError=e.what();}
    auto filename=AssetPaths::ToUtf8String(std::filesystem::u8path(key).filename());m_AssetActionKey=filename.substr(0,filename.size()-AssetCatalog::FileSuffix(key).size());
    if(action==4)m_AssetActionKey=key;
    m_AssetActionBlockers=AssetFileBlockers(app,key);m_AssetActionPath.clear();
    try{m_AssetActionPath=AssetImport::SafeTarget(AssetPaths::ResolveAssetRoot(),key);const auto checked=AssetCatalog::EditablePath(key);m_AssetActionTime=std::filesystem::last_write_time(checked);m_AssetActionSize=std::filesystem::file_size(checked);}catch(const std::exception& e){m_EditorError=e.what();}
}
namespace {
// These small vector glyphs use the current text colour and DPI, including the fallback.
void BrowserIcon(ImDrawList* d,ImVec2 p,float size,const AssetType& type){
    const auto c=ImGui::GetColorU32(ImGuiCol_TextDisabled);const float u=size/16;
    auto line=[&](float a,float b,float x,float y){d->AddLine({p.x+a*u,p.y+b*u},{p.x+x*u,p.y+y*u},c,1.3f*u);};
    auto rect=[&](float a,float b,float x,float y){d->AddRect({p.x+a*u,p.y+b*u},{p.x+x*u,p.y+y*u},c,1*u);};
    switch(type.kind){
    case ecs::AssetKind::Prefab:rect(3,4,13,13);line(3,4,8,1);line(8,1,13,4);line(8,4,8,13);break;
    case ecs::AssetKind::Mesh:line(2,13,8,2);line(8,2,14,13);line(14,13,2,13);line(2,13,11,8);break;
    case ecs::AssetKind::Material:d->AddCircle({p.x+8*u,p.y+8*u},6*u,c,20,1.5f*u);line(4,12,12,4);break;
    case ecs::AssetKind::Texture:rect(1,2,15,14);line(2,12,6,7);line(6,7,10,12);line(9,10,12,6);break;
    case ecs::AssetKind::LuaScript:line(5,4,1,8);line(1,8,5,12);line(11,4,15,8);line(15,8,11,12);line(9,3,7,13);break;
    case ecs::AssetKind::UIDocument:rect(1,2,15,14);line(1,6,15,6);line(6,6,6,14);break;
    case ecs::AssetKind::PostFx:line(8,1,8,15);line(1,8,15,8);line(3,3,13,13);line(3,13,13,3);break;
    default:
        if(type.name=="Folder"){rect(1,5,15,13);line(1,5,1,2);line(1,2,7,2);line(7,2,9,5);}
        else if(type.name=="Scene Document"){rect(1,2,15,14);line(3,12,8,5);line(8,5,13,12);}
        else if(type.name=="UI Style"){rect(1,2,15,14);line(3,5,9,5);line(3,8,7,8);line(8,12,13,7);}
        else if(type.name=="Audio"){line(6,12,6,3);line(6,3,13,2);line(13,2,13,11);d->AddCircleFilled({p.x+4*u,p.y+12*u},2*u,c);d->AddCircleFilled({p.x+11*u,p.y+11*u},2*u,c);}
        else {rect(3,1,13,15);line(6,6,10,6);line(6,9,10,9);}
    }
}
int BrowserCategory(const AssetType& type){
    if(type.name=="Scene Document")return 1;
    switch(type.kind){case ecs::AssetKind::Prefab:return 2;case ecs::AssetKind::Mesh:return 3;case ecs::AssetKind::Material:return 4;case ecs::AssetKind::Texture:return 5;case ecs::AssetKind::LuaScript:return 6;case ecs::AssetKind::UIDocument:return 7;default:return type.name=="UI Style"?7:8;}
}
template<class T>const char* LoadState(ResourceManager* resources,const std::string& key){
    auto asset=resources?resources->PeekIndexed<T>(key):nullptr;if(!asset)return "Not requested";if(asset->IsLoading())return "Loading";if(asset->IsFailed())return "Failed";return "Ready";
}
const char* AssetLoadState(ResourceManager* resources,ecs::AssetKind kind,const std::string& key){
    switch(kind){case ecs::AssetKind::UIDocument:return LoadState<UIResource>(resources,key);case ecs::AssetKind::PostFx:return LoadState<PostFxResource>(resources,key);case ecs::AssetKind::Mesh:return LoadState<MeshResource>(resources,key);case ecs::AssetKind::Texture:return LoadState<TextureResource>(resources,key);case ecs::AssetKind::Material:return LoadState<MaterialResource>(resources,key);case ecs::AssetKind::Shader:return LoadState<ShaderResource>(resources,key);case ecs::AssetKind::Environment:return LoadState<EnvironmentResource>(resources,key);default:return "Indexed";}
}
template<class T>std::string LoadError(ResourceManager* resources,const std::string& key){
    auto asset=resources?resources->PeekIndexed<T>(key):nullptr;return asset&&asset->IsFailed()?asset->GetErrorMessage():"";
}
std::string AssetLoadError(ResourceManager* resources,ecs::AssetKind kind,const std::string& key){
    switch(kind){case ecs::AssetKind::UIDocument:return LoadError<UIResource>(resources,key);case ecs::AssetKind::PostFx:return LoadError<PostFxResource>(resources,key);case ecs::AssetKind::Mesh:return LoadError<MeshResource>(resources,key);case ecs::AssetKind::Texture:return LoadError<TextureResource>(resources,key);case ecs::AssetKind::Material:return LoadError<MaterialResource>(resources,key);case ecs::AssetKind::Shader:return LoadError<ShaderResource>(resources,key);case ecs::AssetKind::Environment:return LoadError<EnvironmentResource>(resources,key);default:return {};}
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
            std::string key=directory+"/entity_"+ecs::NewSceneIdentity()+".prefab.json";
            if(!CreatePrefab(app,key))throw std::runtime_error(m_EditorError);
            m_SelectedAsset=key;m_SelectedAssetKind="Prefab";m_AssetListsDirty=true;NavigateAssetFolder(directory);m_EditorError.clear();return key;
        }catch(const std::exception& error){m_EditorError=error.what();}
    return {};
}
void EditorLayer::DrawAssetBrowser(Application& app){
    const auto start=std::chrono::steady_clock::now();
    struct Timing {double& ms;std::chrono::steady_clock::time_point start;~Timing(){ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}} timing{m_Statistics.assetBrowserMs,start};
    ImFont* browserFont=nullptr;for(auto* font:ImGui::GetIO().Fonts->Fonts)if(std::strcmp(font->GetDebugName(),"Browser UI")==0){browserFont=font;break;}
    if(browserFont)ImGui::PushFont(browserFont,0.f);
    const float browserDpi=ImGui::GetStyle().FontScaleDpi;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(8*browserDpi,6*browserDpi));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(5*browserDpi,3*browserDpi));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(6*browserDpi,4*browserDpi));
    struct FontScope {bool pushed;~FontScope(){ImGui::PopStyleVar(3);if(pushed)ImGui::PopFont();}} fontScope{browserFont!=nullptr};
    ImGui::SetNextWindowSizeConstraints(ImVec2(280,140),ImVec2(FLT_MAX,FLT_MAX));if(!ImGui::Begin("Asset Browser",&m_ShowAssetBrowser)){ImGui::End();return;}
    const auto& types=m_AssetCatalog.Types();auto* resources=app.GetResourceManager();
    const auto activeIdentity=[&]{return m_SelectedAssetKind=="Folder"?AssetContentItem::FolderId(m_SelectedAsset):m_SelectedAsset;};
    if(!m_SelectedAsset.empty()&&!m_AssetSelection.contains(activeIdentity())){m_AssetSelection={activeIdentity()};m_AssetAnchor=activeIdentity();}
    BeginAssetThumbnailFrame(app);m_BrowserStatistics={};m_BrowserStatistics.catalogRevision=m_AssetCatalog.Revision();
    struct Probes {BrowserStatistics& result;std::uint64_t before=ProjectContext::LinkProbeCount();~Probes(){result.pathProbes=ProjectContext::LinkProbeCount()-before;}} probes{m_BrowserStatistics};
    const std::string project=app.GetProject()?AssetPaths::ToUtf8String(app.GetProject()->descriptorPath):"<legacy>";
    if(project!=m_BrowserProject){
        m_BrowserProject=project;
        if(auto saved=m_BrowserSessions.find(project);saved!=m_BrowserSessions.end()&&saved->is_object()){
            auto folder=saved->contains("folder")&&(*saved)["folder"].is_string()?(*saved)["folder"].get<std::string>():std::string();
            if(ecs::ComponentRegistry::ValidateAssetKey(folder,ecs::AssetKind::None)&&(folder.empty()||m_AssetCatalog.Folders().contains(folder)))NavigateAssetFolder(folder);
            if(saved->contains("expanded")&&(*saved)["expanded"].is_array())for(const auto& item:(*saved)["expanded"])if(item.is_string())m_AssetExpanded.insert(item.get<std::string>());
        }
    }
    const float dpi=ImGui::GetStyle().FontScaleDpi;
    const float available=ImGui::GetContentRegionAvail().x;
    const bool shortDock=ImGui::GetContentRegionAvail().y<220*dpi;
    auto openAsset=[&](const AssetEntry& entry){
        const auto& type=types[entry.type];SelectAsset(entry.key,type.name);
        if(type.kind==ecs::AssetKind::Prefab)m_PrefabActionKey=entry.key;
        else if(type.kind==ecs::AssetKind::UIDocument)OpenUIDesigner(app,entry.key);
        else if(type.kind==ecs::AssetKind::Material||type.kind==ecs::AssetKind::LuaScript||type.name=="Scene Document")OpenDocument(app,entry.key);
        else m_AssetDetails=true;
    };
    auto openMaterial=[&](const std::string& key){OpenDocument(app,key);};
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
            if(item("Prefab","prefabs",app.GetWorld().IsAlive(m_Selection.active))){m_PrefabNewKey=(directory.empty()?"prefabs":directory)+"/new.prefab.json";m_PrefabCreatePopup=true;}
            if(item("UI Document","ui")){auto key=directory+"/Canvas-"+ecs::NewSceneIdentity()+".ui.json";if(UILoader::Save(key,ui::EmptyDocument(),&m_EditorError)){m_AssetListsDirty=true;OpenUIDesigner(app,key);}}
            if(item("Material","materials"))action(1,directory+"/new.material.json");
            ImGui::EndMenu();
        }
        if(ImGui::MenuItem("New Folder",nullptr,false,edit)){m_FolderParent=directory;m_FolderPopup=true;}
        if(ImGui::MenuItem("Import Asset...",nullptr,false,edit)){BeginAssetImport();m_ImportTargetFolder=directory;}
        if(ImGui::MenuItem("Refresh"))m_AssetListsDirty=true;
    };
    // Three compact rows; optional controls move into Options at narrow widths.
    ImGui::SetNextItemWidth(90*dpi);
    const char* views[]={"Grid","List","Compact"};
    if(ImGui::Combo("##View",&m_AssetView,views,3))m_AssetGrid=m_AssetView==0;
    if(available>440*dpi&&(!shortDock||available>800*dpi)){
        ImGui::SameLine();int size=m_AssetCardSize<150?0:m_AssetCardSize<230?1:2;
        ImGui::BeginDisabled(!m_AssetGrid);ImGui::SetNextItemWidth(94*dpi);
        const char* sizes[]={"Small","Medium","Large"};if(ImGui::Combo("##Thumbnail size",&size,sizes,3))m_AssetCardSize=size==0?120.f:size==1?180.f:260.f;
        ImGui::EndDisabled();
    }
    ImGui::SameLine();if(ImGui::Button("Create"))ImGui::OpenPopup("BrowserCreate");
    if(ImGui::BeginPopup("BrowserCreate")){creationMenu(m_AssetDirectory);ImGui::EndPopup();}
    ImGui::SameLine();if(ImGui::Button("..."))ImGui::OpenPopup("Asset settings");
    if(ImGui::BeginPopup("Asset settings")){
        if(shortDock&&available<430*dpi){
            if(ImGui::MenuItem("Back",nullptr,false,m_AssetNavigation.cursor>0))NavigateAssetFolder(m_AssetNavigation.Back());
            if(ImGui::MenuItem("Forward",nullptr,false,m_AssetNavigation.cursor+1<m_AssetNavigation.history.size()))NavigateAssetFolder(m_AssetNavigation.Forward());
            if(ImGui::MenuItem("Up",nullptr,false,!m_AssetDirectory.empty()))NavigateAssetFolder(AssetNavigation::Parent(m_AssetDirectory));
            if(ImGui::MenuItem("Project root"))NavigateAssetFolder("");
            std::string path;const auto folder=m_AssetDirectory;for(const auto& part:std::filesystem::u8path(folder)){if(!path.empty())path+="/";path+=AssetPaths::ToUtf8String(part);if(ImGui::MenuItem(path.c_str()))NavigateAssetFolder(path);}
            ImGui::Separator();
        }
        ImGui::Checkbox("Folder tree",&m_AssetTree);ImGui::Checkbox("Details",&m_AssetDetails);
        ImGui::Checkbox("Project Assets",&m_ProjectAssets);
        if(available<=440*dpi||(shortDock&&available<=800*dpi)){int size=m_AssetCardSize<150?0:m_AssetCardSize<230?1:2;const char* sizes[]={"Small","Medium","Large"};if(ImGui::Combo("Thumbnail size",&size,sizes,3))m_AssetCardSize=size==0?120.f:size==1?180.f:260.f;}
        if(ImGui::MenuItem("Refresh"))m_AssetListsDirty=true;
        if(ImGui::MenuItem("Locate selected",nullptr,false,!m_SelectedAsset.empty())){NavigateAssetFolder(AssetNavigation::Parent(m_SelectedAsset));m_AssetFilter.clear();m_AssetKindFilter=m_AssetCategory=0;}
        ImGui::EndPopup();
    }
    m_AssetNavigation.Build(m_AssetCatalog.Folders(),m_AssetCatalog.Entries(),m_AssetCatalog.Revision());
    // Breadcrumb overflow scrolls inside this row instead of widening the dock.
    if(!shortDock||available>=430*dpi){
    if(shortDock)ImGui::SameLine();
    ImGui::BeginChild("Navigation",ImVec2(0,ImGui::GetFrameHeightWithSpacing()),ImGuiChildFlags_None,ImGuiWindowFlags_HorizontalScrollbar|ImGuiWindowFlags_NoScrollbar);
    auto nav=[&](const char* label,const char* tip,bool enabled,auto go){ImGui::BeginDisabled(!enabled);if(ImGui::SmallButton(label))go();ImGui::EndDisabled();if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",tip);ImGui::SameLine();};
    nav("<","Back",m_AssetNavigation.cursor>0,[&]{NavigateAssetFolder(m_AssetNavigation.Back());});
    nav(">","Forward",m_AssetNavigation.cursor+1<m_AssetNavigation.history.size(),[&]{NavigateAssetFolder(m_AssetNavigation.Forward());});
    nav("Up","Parent folder",!m_AssetDirectory.empty(),[&]{NavigateAssetFolder(AssetNavigation::Parent(m_AssetDirectory));});
    if(ImGui::SmallButton("Project"))NavigateAssetFolder("");std::string breadcrumb;
    const auto currentFolder=m_AssetDirectory;
    for(const auto& part:std::filesystem::u8path(currentFolder)){
        const auto name=AssetPaths::ToUtf8String(part);if(!breadcrumb.empty())breadcrumb+="/";breadcrumb+=name;
        if(ImGui::CalcTextSize(name.c_str()).x+ImGui::GetStyle().FramePadding.x*2+ImGui::GetStyle().ItemSpacing.x*4+30*dpi>ImGui::GetContentRegionAvail().x){
            ImGui::SameLine();if(ImGui::SmallButton("...##path"))ImGui::OpenPopup("Breadcrumb folders");
            if(ImGui::BeginPopup("Breadcrumb folders")){std::string path;for(const auto& segment:std::filesystem::u8path(currentFolder)){if(!path.empty())path+="/";path+=AssetPaths::ToUtf8String(segment);if(ImGui::MenuItem(path.c_str()))NavigateAssetFolder(path);}ImGui::EndPopup();}break;
        }
        ImGui::SameLine();ImGui::TextDisabled("/");ImGui::SameLine();ImGui::PushID(breadcrumb.c_str());if(ImGui::SmallButton(name.c_str()))NavigateAssetFolder(breadcrumb);ImGui::PopID();
    }
    ImGui::EndChild();
    }
    const float scopeWidth=available<430*dpi?75*dpi:115*dpi,typeWidth=available<430*dpi?70*dpi:100*dpi;
    const float clearWidth=ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(std::max(30.f,available-scopeWidth-typeWidth-clearWidth-ImGui::GetStyle().ItemSpacing.x*3));
    ImGui::InputTextWithHint("##search","Search name or key",&m_AssetFilter);ImGui::SameLine();
    if(ImGui::Button("x##clear",ImVec2(clearWidth,0)))m_AssetFilter.clear();ImGui::SameLine();
    const char* categories[]={"All types","Scene","Prefab","Model","Material","Texture","Script","UI","Other"};
    ImGui::SetNextItemWidth(typeWidth);if(ImGui::Combo("##type",&m_AssetCategory,categories,9))m_AssetKindFilter=0;ImGui::SameLine();
    int scope=m_AssetNavigation.projectSearch?1:0;const char* scopes[]={"Folder","All assets"};ImGui::SetNextItemWidth(scopeWidth);
    if(ImGui::Combo("##Scope",&scope,scopes,2))m_AssetNavigation.projectSearch=scope==1;
    const bool showTree=m_AssetTree&&available>=430*dpi;
    if(showTree){
        const float width=std::clamp(m_AssetTreeWidth*dpi,90*dpi,std::max(90*dpi,available-220*dpi));
        ImGui::BeginChild("Directories",ImVec2(width,0),ImGuiChildFlags_Borders);
        if(ImGui::Selectable("Project root",m_AssetDirectory.empty()))NavigateAssetFolder("");
        if(ImGui::BeginPopupContextItem("ProjectActions")){creationMenu("");ImGui::EndPopup();}
        auto tree=[&](auto&& self,const std::string& parent)->void {
            for(const auto& folder:m_AssetNavigation.children[parent]){
                const auto name=folder.substr(parent.empty()?0:parent.size()+1);ImGui::PushID(folder.c_str());
                const bool leaf=m_AssetNavigation.children[folder].empty();auto flags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_SpanAvailWidth;
                if(m_AssetDirectory==folder)flags|=ImGuiTreeNodeFlags_Selected;if(leaf)flags|=ImGuiTreeNodeFlags_Leaf;
                ImGui::SetNextItemOpen(m_AssetExpanded.contains(folder),ImGuiCond_Always);
                bool open=ImGui::TreeNodeEx("Folder",flags,"[+] %s",name.c_str());
                if(ImGui::IsItemToggledOpen()){if(open)m_AssetExpanded.insert(folder);else m_AssetExpanded.erase(folder);}
                if(m_AssetDirectory==folder&&m_AssetTreeReveal){ImGui::SetScrollHereY(.5f);m_AssetTreeReveal=false;}
                if(ImGui::IsItemClicked()&&!ImGui::IsItemToggledOpen())NavigateAssetFolder(folder);
                if(ImGui::BeginPopupContextItem("FolderActions")){creationMenu(folder);ImGui::EndPopup();}
                if(open){self(self,folder);ImGui::TreePop();}ImGui::PopID();
            }
        };tree(tree,"");ImGui::EndChild();ImGui::SameLine(0,0);
        ImGui::InvisibleButton("Tree splitter",ImVec2(6*dpi,ImGui::GetContentRegionAvail().y));
        if(ImGui::IsItemHovered()||ImGui::IsItemActive())ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if(ImGui::IsItemActive())m_AssetTreeWidth=std::clamp(m_AssetTreeWidth+ImGui::GetIO().MouseDelta.x/dpi,90.f,std::min(480.f,available/dpi-220));
        ImGui::SameLine(0,0);
    }
    ImGui::BeginChild("Content");
    auto* contentWindow=ImGui::GetCurrentWindow();
    const auto contentOwnsKeyboard=[&]{return GImGui->NavWindow==contentWindow||GImGui->NavWindow==contentWindow->ParentWindow;};
    const bool keyboardOpen=contentOwnsKeyboard()&&!ImGui::GetIO().WantTextInput&&!ImGui::IsAnyItemActive()&&!ImGui::IsDragDropActive()&&!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)&&ImGui::IsKeyPressed(ImGuiKey_Enter,false);
    // Both folders and assets share one cached ordering and one clipped table.
    const AssetType folderType{"Folder","[Folder]","",ecs::AssetKind::None,{}};
    std::vector<const AssetContentItem*> rows;
    const auto& source=m_AssetNavigation.projectSearch?m_AssetNavigation.allAssets:m_AssetNavigation.content[m_AssetDirectory];
    for(const auto& item:source){
        if(item.folder){if(m_AssetFilter.empty())rows.push_back(&item);continue;}
        const auto& row=m_AssetCatalog.Entries()[item.assetIndex];
        if(m_ProjectAssets&&types[row.type].shaderRoot)continue;
        if(m_AssetCategory&&BrowserCategory(types[row.type])!=m_AssetCategory)continue;
        if(m_AssetKindFilter&&int(row.type)+1!=m_AssetKindFilter)continue;
        if(!m_AssetFilter.empty()&&std::search(item.key.begin(),item.key.end(),m_AssetFilter.begin(),m_AssetFilter.end(),[](unsigned char a,unsigned char b){return std::tolower(a)==std::tolower(b);})==item.key.end())continue;
        rows.push_back(&item);
    }
    const auto& entries=m_AssetCatalog.Entries();
    auto itemType=[&](const AssetContentItem& item)->const AssetType&{return item.folder?folderType:types[entries[item.assetIndex].type];};
    auto selectIdentity=[&](const std::string& identity){
        m_SelectedAssetKind.clear();m_SelectedAsset.clear();
        if(identity.starts_with("folder:")){m_SelectedAsset=identity.substr(7);m_SelectedAssetKind="Folder";return;}
        for(const auto& entry:entries)if(entry.key==identity){m_SelectedAsset=entry.key;m_SelectedAssetKind=types[entry.type].name;break;}
    };
    auto openItem=[&](const AssetContentItem& item){if(item.folder){NavigateAssetFolder(item.key);SelectAsset("","");ImGui::SetScrollY(0);}else openAsset(entries[item.assetIndex]);};
    const int columns=m_AssetGrid?std::max(1,int(ImGui::GetContentRegionAvail().x/(m_AssetCardSize*dpi))):1;
    m_BrowserStatistics.matched=unsigned(rows.size());
    if(rows.empty())ImGui::TextWrapped("%s",app.IsProjectBrowserActive()?"No project open. Open a project to browse content.":!m_AssetFilter.empty()||m_AssetKindFilter||m_AssetCategory?"No search results. Clear search or change filters.":"No assets in folder. Create or import an asset.");
    // Leave room for name/state even in a short dock; the size setting still
    // controls column width and the preferred image size.
    const bool compactCards=shortDock&&ImGui::GetWindowHeight()<140*dpi;
    const float cardImage=compactCards?std::clamp((m_AssetCardSize-60)*.4f*dpi,20*dpi,80*dpi):m_AssetCardSize*dpi-24*dpi;
    const float cardHeight=compactCards?std::max(cardImage,ImGui::GetTextLineHeight()*3)+8*dpi:cardImage+ImGui::GetTextLineHeight()*3+12*dpi;
    const float rowHeight=m_AssetGrid?cardHeight:ImGui::GetTextLineHeight()+4*dpi;
    const float assetRowOffset=ImGui::GetCursorPosY();
    if(ImGui::BeginTable("ContentItems",columns,ImGuiTableFlags_SizingStretchSame,ImVec2(m_AssetGrid?std::min(ImGui::GetContentRegionAvail().x,columns*m_AssetCardSize*dpi):0,0))){
        ImGuiListClipper clipper;clipper.Begin(int((rows.size()+columns-1)/columns),rowHeight+ImGui::GetStyle().CellPadding.y*2);
        while(clipper.Step())for(int rowIndex=clipper.DisplayStart;rowIndex<clipper.DisplayEnd;++rowIndex){ImGui::TableNextRow();for(int col=0;col<columns;++col){auto index=rowIndex*columns+col;if(index>=int(rows.size()))break;ImGui::TableSetColumnIndex(col);const auto& item=*rows[index];const AssetEntry folderEntry{};const auto& entry=item.folder?folderEntry:entries[item.assetIndex];const auto& type=itemType(item);const auto& key=item.key;const auto& name=item.name;ImGui::PushID(item.identity.c_str());
            ++m_BrowserStatistics.visible;
            const AssetThumbnail* thumbnail=nullptr;
            if(m_AssetGrid&&(type.kind==ecs::AssetKind::Texture||type.kind==ecs::AssetKind::Mesh))thumbnail=&RequestAssetThumbnail(app,type.kind,key);
            const char* state=item.folder?"Directory":thumbnail?thumbnail->state.c_str():AssetLoadState(resources,type.kind,key);
            const auto origin=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x;
            const bool selected=m_AssetSelection.contains(item.identity);
            if(ImGui::Selectable("##card",selected,ImGuiSelectableFlags_None,ImVec2(0,rowHeight))&&!ImGui::IsKeyPressed(ImGuiKey_Enter,false)){
                const auto& io=ImGui::GetIO();
                if(io.KeyShift&&!m_AssetAnchor.empty()){
                    auto anchor=std::find_if(rows.begin(),rows.end(),[&](const auto* r){return r->identity==m_AssetAnchor;});
                    if(!io.KeyCtrl)m_AssetSelection.clear();
                    const int from=anchor==rows.end()?index:int(anchor-rows.begin());for(int i=std::min(from,index);i<=std::max(from,index);++i)m_AssetSelection.insert(rows[i]->identity);
                }else if(io.KeyCtrl){if(!m_AssetSelection.erase(item.identity))m_AssetSelection.insert(item.identity);m_AssetAnchor=item.identity;}
                else {m_AssetSelection={item.identity};m_AssetAnchor=item.identity;}
                selectIdentity(m_AssetSelection.contains(item.identity)?item.identity:m_AssetSelection.empty()?std::string():*m_AssetSelection.begin());
            }
            if(m_AssetScrollSelection&&item.identity==activeIdentity()){ImGui::SetScrollHereY();m_AssetScrollSelection=false;}
            if(!m_AssetGrid){
                auto* draw=ImGui::GetWindowDrawList();BrowserIcon(draw,{origin.x+2*dpi,origin.y+2*dpi},ImGui::GetTextLineHeight(),type);
                const bool details=m_AssetView==1&&width>370*dpi;const bool sizeColumn=details&&width>650*dpi;
                const float nameEnd=origin.x+width-(details?(sizeColumn?290*dpi:210*dpi):2*dpi);
                draw->PushClipRect({origin.x+22*dpi,origin.y},{nameEnd,origin.y+rowHeight},true);
                draw->AddText({origin.x+24*dpi,origin.y+2*dpi},ImGui::GetColorU32(ImGuiCol_Text),name.c_str());draw->PopClipRect();
                if(details){draw->AddText({nameEnd+6*dpi,origin.y+2*dpi},ImGui::GetColorU32(ImGuiCol_TextDisabled),type.name.c_str());draw->AddText({origin.x+width-(sizeColumn?165:85)*dpi,origin.y+2*dpi},ImGui::GetColorU32(ImGuiCol_TextDisabled),state);if(sizeColumn&&entry.bytes){const auto bytes=*entry.bytes<1024?std::to_string(*entry.bytes)+" B":std::to_string(*entry.bytes/1024)+" KB";draw->AddText({origin.x+width-75*dpi,origin.y+2*dpi},ImGui::GetColorU32(ImGuiCol_TextDisabled),bytes.c_str());}}
            }
            if(m_AssetGrid){
                auto* draw=ImGui::GetWindowDrawList();
                const float side=std::min(cardImage,std::max(1.f,width-8*dpi));
                const ImVec2 p{origin.x+(compactCards?4*dpi:(width-side)*.5f),origin.y+4*dpi};
                draw->AddRectFilled(p,{p.x+side,p.y+cardImage},IM_COL32(22,25,30,255),4*dpi);
                const auto id=thumbnail&&app.GetRenderAdapter()?app.GetRenderAdapter()->GetTexturePreviewId(thumbnail->texture):0;
                if(id&&thumbnail->width>0&&thumbnail->height>0){
                    const float scale=std::min(side/thumbnail->width,cardImage/thumbnail->height);
                    const ImVec2 size{thumbnail->width*scale,thumbnail->height*scale};
                    const ImVec2 start{p.x+(side-size.x)*.5f,p.y+(cardImage-size.y)*.5f};
                    draw->AddImage(ImTextureID(id),start,{start.x+size.x,start.y+size.y});
                }else {const float icon=std::min(40*dpi,cardImage*.6f);BrowserIcon(draw,{p.x+(side-icon)*.5f,p.y+(cardImage-icon)*.5f},icon,type);}
                const float textX=compactCards?p.x+side+6*dpi:origin.x+4*dpi;
                const float textY=compactCards?origin.y+4*dpi:p.y+cardImage+4*dpi;
                draw->PushClipRect({textX,origin.y},{origin.x+width,origin.y+cardHeight},true);
                draw->PushClipRect({textX,textY},{origin.x+width,textY+ImGui::GetTextLineHeight()*2},true);
                draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),{textX,textY},ImGui::GetColorU32(ImGuiCol_Text),name.c_str(),nullptr,std::max(1.f,origin.x+width-textX-4*dpi));draw->PopClipRect();
                const std::string caption=item.folder?"Folder":type.name+" / "+state;
                draw->AddText({textX,textY+ImGui::GetTextLineHeight()*2},ImGui::GetColorU32(ImGuiCol_TextDisabled),caption.c_str());
                draw->PopClipRect();
            }
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)&&!ImGui::IsDragDropActive()){
                ImGui::BeginTooltip();ImGui::TextUnformatted(name.c_str());
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0],0.f);ImGui::TextUnformatted(key.c_str());ImGui::PopFont();
                ImGui::Text("%s / %s",type.name.c_str(),state);if(entry.bytes)ImGui::Text("Size: %llu bytes",static_cast<unsigned long long>(*entry.bytes));
                const auto error=thumbnail?thumbnail->error:AssetLoadError(resources,type.kind,key);if(!error.empty())ImGui::TextWrapped("%s",error.c_str());
                ImGui::TextDisabled("%s",item.folder?"Double-click or Enter: open folder.":"Double-click: open. Drag: assign or place.");ImGui::EndTooltip();
            }
            if((type.kind!=ecs::AssetKind::None||type.name=="Imported Scene")&&ImGui::BeginDragDropSource()){AssetPayload payload{};payload.kind=type.kind;std::memcpy(payload.key,key.data(),std::min(key.size(),sizeof(payload.key)-1));ImGui::SetDragDropPayload("WHISP_ASSET",&payload,sizeof(payload));ImGui::Text("%s %s",type.icon.c_str(),name.c_str());ImGui::EndDragDropSource();}
            if(ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0))m_AssetDoubleClick=item.identity;
            if(item.folder&&ImGui::BeginPopupContextItem("ContentFolderActions")){
                if(!m_AssetSelection.contains(item.identity))SelectAsset(key,"Folder");
                if(ImGui::MenuItem("Open"))openItem(item);creationMenu(key);ImGui::EndPopup();
            }
            if(!item.folder&&ImGui::BeginPopupContextItem("AssetActions")){
                if(!m_AssetSelection.contains(key))SelectAsset(key,type.name);
                if(ImGui::MenuItem("Open"))openAsset(entry);
                if(ImGui::MenuItem("Copy Resource Key"))ImGui::SetClipboardText(key.c_str());
                auto resolved=[&]{return type.name=="Scene Document"&&app.GetProject()?app.GetProject()->ResolveScene(key):type.shaderRoot?AssetPaths::ResolveShaderPath(std::filesystem::u8path(key)):AssetPaths::ResolveAssetPath(std::filesystem::u8path(key));};
                if(ImGui::MenuItem("Copy Path")){try{ImGui::SetClipboardText(AssetPaths::ToUtf8String(resolved()).c_str());}catch(const std::exception& e){m_EditorError=e.what();}}
                if(ImGui::MenuItem("Show in Explorer")){
#ifdef _WIN32
                    try{const auto args=L"/select,\""+resolved().wstring()+L"\"";if(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",L"explorer.exe",args.c_str(),nullptr,SW_SHOWNORMAL))<=32)m_EditorError="Could not open Explorer";}catch(const std::exception& e){m_EditorError=e.what();}
#endif
                }
                ImGui::Separator();
                if(type.kind==ecs::AssetKind::UIDocument&&ImGui::MenuItem("Open in Widget Designer"))OpenUIDesigner(app,key);
                if(thumbnail&&thumbnail->state=="Failed"&&ImGui::MenuItem("Retry thumbnail")){
                    if(thumbnail->texture.IsValid()&&app.GetRenderAdapter())app.GetRenderAdapter()->DestroyTexture(thumbnail->texture);
                    m_AssetThumbnails.erase(key);
                    if(resources){
                        if(type.kind==ecs::AssetKind::Mesh){auto r=resources->PeekIndexed<MeshResource>(key);if(r&&r->IsFailed())resources->Unload<MeshResource>(std::filesystem::u8path(key));}
                        else{auto r=resources->PeekIndexed<TextureResource>(key);if(r&&r->IsFailed())resources->Unload<TextureResource>(std::filesystem::u8path(key));}
                    }
                }
                if(type.kind==ecs::AssetKind::LuaScript&&ImGui::MenuItem("Open in Visual Studio"))OpenLuaScriptInVisualStudio(key,&m_EditorError);
                if(type.kind==ecs::AssetKind::Prefab){
                if(ImGui::MenuItem("Open Prefab Editor"))m_PrefabActionKey=key;
                    if(ImGui::MenuItem("Create Instance",nullptr,false,!app.IsEditorPlayMode()&&!app.IsPrefabEditing()))InstantiatePrefab(app,key);
                    if(ImGui::MenuItem("Update Linked Instances...",nullptr,false,!app.IsEditorPlayMode()&&!app.IsPrefabEditing())){m_PrefabActionKey=key;m_PrefabAction=5;m_PrefabConfirm=true;}
                }
                if(type.name=="Imported Scene"&&ImGui::MenuItem("Instantiate imported scene"))InstantiateImportedScene(app,key);
                if(type.kind==ecs::AssetKind::Mesh&&ImGui::MenuItem("Create model entity"))CreateModelEntity(app,key,name);
                if(type.kind==ecs::AssetKind::Material&&ImGui::MenuItem("Open Material Editor"))openMaterial(key);
                if(type.kind!=ecs::AssetKind::None&&ImGui::MenuItem("Assign to Active Entity"))AssignAsset(app,m_Selection.active,type.kind,key);
                std::string blocked;
                if(m_AssetSelection.size()>1)blocked="Select one asset for file operations";
                else if(app.IsEditorPlayMode())blocked="Stop Play before modifying asset files";
                else if(app.IsPrefabEditing())blocked="Return from Prefab Editor before modifying asset files";
                else if(m_MaterialDirty)blocked="Save or Revert material edits first";
                else try{(void)AssetCatalog::EditablePath(key);}catch(const std::exception& e){blocked=e.what();}
                if(ImGui::MenuItem("Rename...","F2",false,blocked.empty())){SelectAsset(key,type.name);BeginAssetFileAction(app,2,key);}
                if(!blocked.empty()&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",blocked.c_str());
                if(ImGui::MenuItem("Delete...","Delete",false,blocked.empty())){SelectAsset(key,type.name);BeginAssetFileAction(app,3,key);}
                if(ImGui::MenuItem("Move...",nullptr,false,blocked.empty())){SelectAsset(key,type.name);BeginAssetFileAction(app,4,key);}
                if(!blocked.empty()&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",blocked.c_str());
                ImGui::EndPopup();
            }ImGui::PopID();
        }}ImGui::EndTable();
    }
    if(ImGui::BeginPopupContextWindow("ContentCreate",ImGuiPopupFlags_MouseButtonRight|ImGuiPopupFlags_NoOpenOverItems)){creationMenu(m_AssetDirectory);ImGui::EndPopup();}
    if(ImGui::IsWindowHovered()&&!ImGui::IsAnyItemHovered()&&ImGui::IsMouseClicked(0))SelectAsset("","");
    if(m_AssetDetails){
    // Keep explicit previews reachable even when the asset list has thousands
    // of rows or the browser occupies a short bottom dock.
    ImGui::SetNextWindowSize(ImVec2(340*dpi,320*dpi),ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Asset Details",&m_AssetDetails)){
    ImGui::Separator();ImGui::TextWrapped("Selected: %s",m_SelectedAsset.empty()?"None":m_SelectedAsset.c_str());
    if(!m_SelectedAsset.empty())for(auto& type:types)if(type.name==m_SelectedAssetKind){ImGui::Text("%s %s / %s",type.icon.c_str(),type.name.c_str(),AssetLoadState(resources,type.kind,m_SelectedAsset));if(type.kind!=ecs::AssetKind::None&&ImGui::Button("Assign selected to entity"))AssignAsset(app,m_Selection.active,type.kind,m_SelectedAsset);}
    if(m_SelectedAssetKind=="Mesh"&&!m_SelectedAsset.empty()&&!app.IsEditorPlayMode()&&ImGui::Button("Create Model Entity")){CreateModelEntity(app,m_SelectedAsset,AssetPaths::ToUtf8String(std::filesystem::u8path(m_SelectedAsset).stem()));}
    if(m_SelectedAssetKind=="Imported Scene"&&!m_SelectedAsset.empty()&&!app.IsEditorPlayMode()&&ImGui::Button("Instantiate imported scene"))InstantiateImportedScene(app,m_SelectedAsset);
    if(m_SelectedAssetKind=="Material"&&!m_SelectedAsset.empty()&&ImGui::Button("Open selected material"))openMaterial(m_SelectedAsset);
    if(resources&&!m_SelectedAsset.empty()&&m_SelectedAssetKind!="Folder"){
        if(ImGui::Button("Load preview metadata")){
            if(m_SelectedAssetKind=="Material")resources->LoadAsync<MaterialResource>(std::filesystem::u8path(m_SelectedAsset));else if(m_SelectedAssetKind=="Mesh")resources->LoadAsync<MeshResource>(std::filesystem::u8path(m_SelectedAsset));else if(m_SelectedAssetKind=="Texture")resources->LoadAsync<TextureResource>(std::filesystem::u8path(m_SelectedAsset));else if(m_SelectedAssetKind=="Environment")resources->LoadAsync<EnvironmentResource>(std::filesystem::u8path(m_SelectedAsset));
        }
        if(m_SelectedAssetKind=="Material"){auto r=resources->PeekIndexed<MaterialResource>(m_SelectedAsset);if(r&&r->IsLoaded()){const auto& d=r->GetData();ImGui::Text("%s / Metallic %.2f / Roughness %.2f",d.shadingModel==ShadingModel::PBR?"PBR":"Lambert",d.metallic,d.roughness);}}
        if(m_SelectedAssetKind=="Mesh"){auto r=resources->PeekIndexed<MeshResource>(m_SelectedAsset);if(r&&r->IsLoaded()){const auto& d=r->GetData().meshData;ImGui::Text("%zu vertices / %zu indices / %zu material ranges",d.vertices.size(),d.indices.size(),d.submeshes.size());}}
        if(m_SelectedAssetKind=="Texture"){auto r=resources->PeekIndexed<TextureResource>(m_SelectedAsset);if(r&&r->IsLoaded()){const auto& d=r->GetData().textureData;ImGui::Text("%d x %d / RGBA / %.2f MiB decoded",d.width,d.height,d.pixels.size()/1048576.);}}
        if(m_SelectedAssetKind=="Environment"){auto r=resources->PeekIndexed<EnvironmentResource>(m_SelectedAsset);if(r&&r->IsLoaded()){const auto& d=r->GetData();ImGui::Text("HDR %u x %u / sky %u / IBL %u",d.width,d.height,d.sky.resolution,d.environment.resolution);}}
    }
    DrawAssetPreview(app);
    ImGui::TextDisabled("Type previews; content is indexed on Refresh, never every frame.");
    if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());
    }
    ImGui::End();
    }
    if(m_AssetScrollSelection){auto found=std::find_if(rows.begin(),rows.end(),[&](auto* row){return row->identity==activeIdentity();});if(found!=rows.end()){const auto row=int(found-rows.begin())/columns;ImGui::SetScrollY(assetRowOffset+row*(rowHeight+ImGui::GetStyle().CellPadding.y*2));}}
    if(keyboardOpen)for(const auto* item:rows)if(item->identity==activeIdentity()){openItem(*item);break;}
    const auto& keyboard=ImGui::GetIO();
    if(contentOwnsKeyboard()&&!keyboard.WantTextInput&&!ImGui::IsAnyItemActive()&&!ImGui::IsDragDropActive()&&!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)){
        if(keyboard.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_C,false)){std::string keys;for(const auto& key:m_AssetSelection){if(!keys.empty())keys+="\n";keys+=key.starts_with("folder:")?key.substr(7):key;}if(!keys.empty())ImGui::SetClipboardText(keys.c_str());}
        if(!keyboard.KeyCtrl&&!keyboard.KeyAlt){
            if(ImGui::IsKeyPressed(ImGuiKey_Backspace,false))NavigateAssetFolder(AssetNavigation::Parent(m_AssetDirectory));
            auto found=std::find_if(rows.begin(),rows.end(),[&](auto* r){return r->identity==activeIdentity();});int index=found==rows.end()?-1:int(found-rows.begin());int next=index;
            if(ImGui::IsKeyPressed(ImGuiKey_DownArrow))next+=columns;else if(ImGui::IsKeyPressed(ImGuiKey_UpArrow))next-=columns;
            else if(ImGui::IsKeyPressed(ImGuiKey_RightArrow))++next;else if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow))--next;
            if(next!=index&&!rows.empty()){
                next=std::clamp(next,0,int(rows.size())-1);SelectAsset(rows[next]->key,itemType(*rows[next]).name);m_AssetScrollSelection=true;
                // Our row/column navigation owns the move; keep native focus on
                // that same item so Enter cannot reactivate a different card.
                ImGui::NavMoveRequestCancel();const auto tableID=ImGui::GetID("ContentItems");
                ImGui::SetFocusID(ImHashStr("##card",0,ImHashStr(rows[next]->identity.c_str(),0,tableID)),ImGui::GetCurrentWindow());
            }

        }
    }
    m_BrowserStatistics.requests=m_ThumbnailRequests;m_BrowserStatistics.uploads=m_ThumbnailUploads;
    ImGui::EndChild();
    const auto& io=ImGui::GetIO();
    if(contentOwnsKeyboard()&&!app.IsEditorPlayMode()&&m_SelectedAssetKind!="Folder"&&m_AssetSelection.size()<=1&&!m_SelectedAsset.empty()&&!io.WantTextInput&&!ImGui::IsAnyItemActive()&&!ImGui::IsDragDropActive()&&!io.KeyCtrl&&!io.KeyAlt&&!io.KeyShift&&!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)){
        if(ImGui::IsKeyPressed(ImGuiKey_F2,false))BeginAssetFileAction(app,2,m_SelectedAsset);
        else if(ImGui::IsKeyPressed(ImGuiKey_Delete,false))BeginAssetFileAction(app,3,m_SelectedAsset);
    }
    if(m_AssetActionRequested){ImGui::OpenPopup(m_AssetAction==1?"Material Asset Action":m_AssetAction==2?"Rename Asset":m_AssetAction==4?"Move Asset":"Delete Asset?");m_AssetActionRequested=false;}
    if(ImGui::BeginPopupModal("Material Asset Action",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextUnformatted("Create a PBR material (schema v4)");
        ImGui::InputText("Asset key / directory",&m_AssetActionKey);
        ImGui::BeginDisabled(app.IsEditorPlayMode()||m_MaterialDirty);
        if(ImGui::Button("Create")){
            if(CreateMaterialAsset(app,m_AssetActionKey,&m_EditorError)){openMaterial(m_AssetActionKey);m_AssetListsDirty=true;ImGui::CloseCurrentPopup();}
        }ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::EndPopup();
    }
    const char* fileDialog=m_AssetAction==2?"Rename Asset":m_AssetAction==4?"Move Asset":"Delete Asset?";
    if(ImGui::BeginPopupModal(fileDialog,nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        const bool rename=m_AssetAction==2||m_AssetAction==4;
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
            enter=ImGui::InputText(m_AssetAction==4?"Destination asset key":"Name",&m_AssetActionKey,ImGuiInputTextFlags_AutoSelectAll|ImGuiInputTextFlags_EnterReturnsTrue);
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
                ok=m_AssetAction==4?MoveAssetFile(app,m_AssetActionSource,m_AssetActionKey,true,&m_EditorError,m_AssetDynamicRiskAccepted):rename?RenameAssetFile(app,m_AssetActionSource,m_AssetActionKey,true,&m_EditorError,m_AssetDynamicRiskAccepted):DeleteAssetFile(app,m_AssetActionSource,true,&m_EditorError,m_AssetDynamicRiskAccepted);
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
    // A fast retry after cancelling a drag can look like a double click on
    // mouse-down. Wait for release so a second drag never opens a document or preview.
    if(!m_AssetDoubleClick.empty()&&!ImGui::IsMouseDown(0)){
        if(ImGui::IsMouseReleased(0)&&!ImGui::IsMouseDragPastThreshold(0)){if(m_AssetDoubleClick.starts_with("folder:")){NavigateAssetFolder(m_AssetDoubleClick.substr(7));SelectAsset("","");ImGui::SetScrollY(contentWindow,0);}else for(const auto& entry:m_AssetCatalog.Entries())if(entry.key==m_AssetDoubleClick){openAsset(entry);break;}}
        m_AssetDoubleClick.clear();
    }
    DrawImportDialogs(app);
    ImGui::End();
}
}
