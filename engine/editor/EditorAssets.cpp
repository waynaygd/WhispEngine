#include "EditorLayer.h"
#include "EditorWidgets.h"
#include "PrecisionInputs.h"
#include "AssetImport.h"
#include "../core/Application.h"
#include "../core/AssetPaths.h"
#include "../resources/ResourceManager.h"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <set>
#include <cstring>
#include <chrono>
#include "../scene/SceneSerializer.h"
namespace editor {
bool EditorLayer::OpenMaterial(Application& app,const std::string& key){
    m_ShowMaterialEditor=m_FocusMaterial=true;
    if(m_MaterialDirty&&key!=m_EditingMaterial){m_MaterialError="Save or Revert the current material before switching assets";return false;}
    m_EditingMaterial=key;m_SelectedAsset=key;m_SelectedAssetKind="Material";if(auto* r=app.GetResourceManager())r->LoadAsync<MaterialResource>(key);return true;
}
bool EditorLayer::AssignAsset(Application& app,ecs::Entity e,ecs::AssetKind kind,const std::string& key){
    if(!ecs::ComponentRegistry::ValidateAssetKey(key,kind,&m_EditorError))return false;
    FinishTransaction(app);
    if(kind==ecs::AssetKind::Environment){BeginSceneTransaction(app,"Assign environment");app.GetEnvironmentSettings().assetPath=key;app.GetEnvironmentSettings().enabled=true;FinishTransaction(app);return true;}
    auto& w=app.GetWorld();if(!w.IsAlive(e))return false;auto before=CaptureSelectedEntity(app,e);const auto& r=ecs::ComponentRegistry::Builtins();
    if(kind==ecs::AssetKind::Mesh){if(!w.HasComponent<ecs::MeshRendererComponent>(e)&&!r.Add(w,e,ecs::ComponentId("MeshRenderer"),&m_EditorError))return false;w.GetComponent<ecs::MeshRendererComponent>(e)->meshPath=key;}
    else if(kind==ecs::AssetKind::Material){if(!w.HasComponent<ecs::MaterialComponent>(e))w.AddComponent<ecs::MaterialComponent>(e);auto& m=*w.GetComponent<ecs::MaterialComponent>(e);m.materialPath=key;m.shaderPath.clear();m.texturePath.clear();m.overrides={};m.slotMaterials.clear();if(auto* mesh=w.GetComponent<ecs::MeshRendererComponent>(e)){mesh->shaderPath.clear();mesh->texturePath.clear();}}
    else if(kind==ecs::AssetKind::Texture||kind==ecs::AssetKind::Shader){if(!w.HasComponent<ecs::MaterialComponent>(e))w.AddComponent<ecs::MaterialComponent>(e);auto& m=*w.GetComponent<ecs::MaterialComponent>(e);(kind==ecs::AssetKind::Texture?m.texturePath:m.shaderPath)=key;}
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
    const bool ok=m_AssetCatalog.RenameMaterial(from,to,error);if(ok)m_AssetListsDirty=true;return ok;
}
bool EditorLayer::DeleteMaterialAsset(Application& app,const std::string& key,bool confirmed,std::string* error){
    if(!confirmed||app.IsEditorPlayMode()||m_MaterialDirty||AssetReferenced(app,key)){if(error)*error="Confirm action in Edit mode; asset must have no scene/history references or unsaved edits";return false;}
    const bool ok=m_AssetCatalog.DeleteMaterial(key,error);if(ok)m_AssetListsDirty=true;return ok;
}
namespace {
template<class T>const char* LoadState(ResourceManager* resources,const std::string& key){
    auto asset=resources?resources->Get<T>(key):nullptr;if(!asset)return "Not requested";if(asset->IsLoading())return "Loading";if(asset->IsFailed())return "Failed";return "Ready";
}
const char* AssetLoadState(ResourceManager* resources,ecs::AssetKind kind,const std::string& key){
    switch(kind){case ecs::AssetKind::Mesh:return LoadState<MeshResource>(resources,key);case ecs::AssetKind::Texture:return LoadState<TextureResource>(resources,key);case ecs::AssetKind::Material:return LoadState<MaterialResource>(resources,key);case ecs::AssetKind::Shader:return LoadState<ShaderResource>(resources,key);case ecs::AssetKind::Environment:return LoadState<EnvironmentResource>(resources,key);default:return "Indexed";}
}
}
void EditorLayer::DrawAssetBrowser(Application& app){
    const auto start=std::chrono::steady_clock::now();
    struct Timing {double& ms;std::chrono::steady_clock::time_point start;~Timing(){ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}} timing{m_Statistics.assetBrowserMs,start};
    ImGui::SetNextWindowSizeConstraints(ImVec2(280,180),ImVec2(FLT_MAX,FLT_MAX));if(!ImGui::Begin("Asset Browser",&m_ShowAssetBrowser)){ImGui::End();return;}
    const auto& types=m_AssetCatalog.Types();auto* resources=app.GetResourceManager();
    auto openMaterial=[&](const std::string& key){OpenMaterial(app,key);};
    auto action=[&](int kind,const std::string& key){m_AssetAction=kind;m_AssetActionSource=key;m_AssetActionKey=key;m_AssetActionRequested=true;m_EditorError.clear();};
    ImGui::BeginDisabled(app.IsEditorPlayMode());if(ImGui::Button("Create Material")){std::string directory=m_AssetDirectory.starts_with("materials")?m_AssetDirectory:"materials";action(1,directory+"/new.material.json");}ImGui::EndDisabled();
    ImGui::SameLine();ImGui::BeginDisabled(app.IsEditorPlayMode());if(ImGui::Button("Import Asset...")){BeginAssetImport();}ImGui::EndDisabled();
    ImGui::SameLine();if(ImGui::Button("Refresh"))m_AssetListsDirty=true;ImGui::SameLine();ImGui::Checkbox("Grid",&m_AssetGrid);ImGui::SameLine();ImGui::Checkbox("Project Assets",&m_ProjectAssets);
    if(m_AssetGrid){ImGui::SetNextItemWidth(160);PrecisionSliderFloat("Card size",&m_AssetCardSize,120,320,"%.0f px");}
    if(!m_SelectedAsset.empty()&&ImGui::SmallButton("Locate selected")){NavigateAssetFolder(AssetPaths::ToUtf8String(std::filesystem::u8path(m_SelectedAsset).parent_path()));m_AssetFilter.clear();m_AssetKindFilter=0;}
    ImGui::SetNextItemWidth(std::max(100.f,ImGui::GetContentRegionAvail().x*.55f));ImGui::InputTextWithHint("##search","Search assets",&m_AssetFilter);ImGui::SameLine();ImGui::SetNextItemWidth(130);
    if(ImGui::BeginCombo("##type",m_AssetKindFilter>0&&m_AssetKindFilter<=int(types.size())?types[m_AssetKindFilter-1].name.c_str():"All types")){
        if(ImGui::Selectable("All types",m_AssetKindFilter==0))m_AssetKindFilter=0;for(int i=0;i<int(types.size());++i)if(ImGui::Selectable(types[i].name.c_str(),m_AssetKindFilter==i+1))m_AssetKindFilter=i+1;ImGui::EndCombo();
    }
    m_AssetNavigation.Build(m_AssetCatalog.Folders(),m_AssetCatalog.Entries(),m_AssetCatalog.Revision());
    ImGui::BeginDisabled(m_AssetNavigation.cursor==0);if(ImGui::SmallButton("Back"))m_AssetDirectory=m_AssetNavigation.Back();ImGui::EndDisabled();ImGui::SameLine();
    ImGui::BeginDisabled(m_AssetNavigation.cursor+1>=m_AssetNavigation.history.size());if(ImGui::SmallButton("Forward"))m_AssetDirectory=m_AssetNavigation.Forward();ImGui::EndDisabled();ImGui::SameLine();
    if(ImGui::SmallButton("Up"))NavigateAssetFolder(AssetNavigation::Parent(m_AssetDirectory));ImGui::SameLine();ImGui::Checkbox("Search project",&m_AssetNavigation.projectSearch);
    ImGui::SameLine();if(ImGui::SmallButton("Project"))NavigateAssetFolder("");std::string breadcrumb;
    for(const auto& part:std::filesystem::u8path(m_AssetDirectory)){if(!breadcrumb.empty())breadcrumb+="/";breadcrumb+=AssetPaths::ToUtf8String(part);ImGui::SameLine();ImGui::TextDisabled("/");ImGui::SameLine();ImGui::PushID(breadcrumb.c_str());if(ImGui::SmallButton(AssetPaths::ToUtf8String(part).c_str()))NavigateAssetFolder(breadcrumb);ImGui::PopID();}
    ImGui::BeginChild("Directories",ImVec2(std::clamp(ImGui::GetContentRegionAvail().x*.2f,100.f,210.f),0),ImGuiChildFlags_Borders);
    if(ImGui::Selectable("Project root",m_AssetDirectory.empty()))NavigateAssetFolder("");
    auto tree=[&](auto&& self,const std::string& parent)->void {
        for(auto& folder:m_AssetNavigation.children[parent]){
            const auto name=folder.substr(parent.empty()?0:parent.size()+1);ImGui::PushID(folder.c_str());
            const bool leaf=m_AssetNavigation.children[folder].empty();auto flags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_SpanAvailWidth;
            if(m_AssetDirectory==folder)flags|=ImGuiTreeNodeFlags_Selected;if(leaf)flags|=ImGuiTreeNodeFlags_Leaf;
            bool open=ImGui::TreeNodeEx("Folder",flags,"%s (%zu)",name.c_str(),m_AssetNavigation.counts[folder]);
            if(ImGui::IsItemClicked()&&!ImGui::IsItemToggledOpen())NavigateAssetFolder(folder);
            if(ImGui::BeginPopupContextItem("FolderActions")){if(ImGui::MenuItem("New Folder",nullptr,false,!app.IsEditorPlayMode())){m_FolderParent=folder;m_FolderPopup=true;}ImGui::EndPopup();}
            if(open){self(self,folder);ImGui::TreePop();}ImGui::PopID();
        }
    };tree(tree,"");
    if(ImGui::BeginPopupContextWindow("FolderRoot",ImGuiPopupFlags_MouseButtonRight|ImGuiPopupFlags_NoOpenOverItems)){if(ImGui::MenuItem("New Folder",nullptr,false,!app.IsEditorPlayMode())){m_FolderParent=m_AssetDirectory;m_FolderPopup=true;}ImGui::EndPopup();}
    ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("Content");
    std::vector<const AssetEntry*> rows;for(const auto& row:m_AssetCatalog.Entries()){
        if(m_ProjectAssets&&types[row.type].shaderRoot)continue;
        if(m_AssetKindFilter&&int(row.type)+1!=m_AssetKindFilter)continue;if((m_AssetFilter.empty()||!m_AssetNavigation.projectSearch)&&!AssetNavigation::InFolder(row.key,m_AssetDirectory))continue;
        if(!m_AssetFilter.empty()&&std::search(row.key.begin(),row.key.end(),m_AssetFilter.begin(),m_AssetFilter.end(),[](unsigned char a,unsigned char b){return std::tolower(a)==std::tolower(b);})==row.key.end())continue;rows.push_back(&row);
    }
    const int columns=m_AssetGrid?std::max(1,int(ImGui::GetContentRegionAvail().x/(m_AssetCardSize*ImGui::GetStyle().FontScaleDpi))):1;
    for(const auto& folder:m_AssetNavigation.children[m_AssetDirectory]){
        auto name=folder.substr(m_AssetDirectory.empty()?0:m_AssetDirectory.size()+1);ImGui::PushID(folder.c_str());
        if(ImGui::Selectable(("[Folder] "+name).c_str(),false,ImGuiSelectableFlags_AllowDoubleClick)&&ImGui::IsMouseDoubleClicked(0)){NavigateAssetFolder(folder);ImGui::PopID();break;}ImGui::PopID();
    }
    if(rows.empty())ImGui::TextWrapped("No assets match. Choose another folder or filter, Refresh, or Create Material.");
    if(ImGui::BeginTable("ContentItems",columns,ImGuiTableFlags_SizingStretchSame)){
        ImGuiListClipper clipper;clipper.Begin(int((rows.size()+columns-1)/columns),m_AssetGrid?92.f*ImGui::GetStyle().FontScaleDpi:ImGui::GetFrameHeightWithSpacing());
        while(clipper.Step())for(int rowIndex=clipper.DisplayStart;rowIndex<clipper.DisplayEnd;++rowIndex){ImGui::TableNextRow();for(int col=0;col<columns;++col){auto index=rowIndex*columns+col;if(index>=int(rows.size()))break;ImGui::TableSetColumnIndex(col);const auto& entry=*rows[index];const auto& type=types[entry.type];const auto& key=entry.key;auto name=AssetPaths::ToUtf8String(std::filesystem::u8path(key).filename());ImGui::PushID(key.c_str());
            const char* state=AssetLoadState(resources,type.kind,key);std::string label=m_AssetGrid?type.icon+"\n"+name+"\n"+state:type.icon+" "+name+"  |  "+type.name+"  |  "+state;
            if(ImGui::Selectable(label.c_str(),m_SelectedAsset==key,ImGuiSelectableFlags_None,ImVec2(0,m_AssetGrid?84.f*ImGui::GetStyle().FontScaleDpi:0))){m_SelectedAsset=key;m_SelectedAssetKind=type.name;}
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%s / %s\nDouble-click material: open editor. Drag: assign compatible slot.",key.c_str(),type.name.c_str(),state);
            if((type.kind!=ecs::AssetKind::None||type.name=="Imported Scene")&&ImGui::BeginDragDropSource()){AssetPayload payload{};payload.kind=type.kind;std::memcpy(payload.key,key.data(),std::min(key.size(),sizeof(payload.key)-1));ImGui::SetDragDropPayload("WHISP_ASSET",&payload,sizeof(payload));ImGui::Text("%s %s",type.icon.c_str(),name.c_str());ImGui::EndDragDropSource();}
            if(ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0)&&type.kind==ecs::AssetKind::Material)openMaterial(key);
            if(ImGui::BeginPopupContextItem("AssetActions")){
                if(type.name=="Imported Scene"&&ImGui::MenuItem("Instantiate imported scene"))InstantiateImportedScene(app,key);
                if(type.kind==ecs::AssetKind::Mesh&&ImGui::MenuItem("Create model entity"))CreateModelEntity(app,key,name);
                if(type.kind==ecs::AssetKind::Material&&ImGui::MenuItem("Open Material Editor"))openMaterial(key);
                if(type.kind!=ecs::AssetKind::None&&ImGui::MenuItem("Assign to selection"))AssignAsset(app,m_SelectedEntity,type.kind,key);
                ImGui::BeginDisabled(app.IsEditorPlayMode()||m_MaterialDirty||!m_AssetCatalog.CreatedHere(key));
                if(ImGui::MenuItem("Rename..."))action(2,key);if(ImGui::MenuItem("Delete..."))action(3,key);ImGui::EndDisabled();
                ImGui::TextDisabled("Rename/delete: session-created, unreferenced materials only.");ImGui::EndPopup();
            }ImGui::PopID();
        }}ImGui::EndTable();
    }
    ImGui::Separator();ImGui::TextWrapped("Selected: %s",m_SelectedAsset.empty()?"None":m_SelectedAsset.c_str());
    if(!m_SelectedAsset.empty())for(auto& type:types)if(type.name==m_SelectedAssetKind){ImGui::Text("%s %s / %s",type.icon.c_str(),type.name.c_str(),AssetLoadState(resources,type.kind,m_SelectedAsset));if(type.kind!=ecs::AssetKind::None&&ImGui::Button("Assign selected to entity"))AssignAsset(app,m_SelectedEntity,type.kind,m_SelectedAsset);}
    if(m_SelectedAssetKind=="Mesh"&&!m_SelectedAsset.empty()&&!app.IsEditorPlayMode()&&ImGui::Button("Create Model Entity")){CreateModelEntity(app,m_SelectedAsset,AssetPaths::ToUtf8String(std::filesystem::u8path(m_SelectedAsset).stem()));}
    if(m_SelectedAssetKind=="Imported Scene"&&!m_SelectedAsset.empty()&&!app.IsEditorPlayMode()&&ImGui::Button("Instantiate imported scene"))InstantiateImportedScene(app,m_SelectedAsset);
    if(m_SelectedAssetKind=="Material"&&!m_SelectedAsset.empty()&&ImGui::Button("Open selected material"))openMaterial(m_SelectedAsset);
    if(resources&&!m_SelectedAsset.empty()){
        if(ImGui::Button("Load preview metadata")){
            if(m_SelectedAssetKind=="Material")resources->LoadAsync<MaterialResource>(m_SelectedAsset);else if(m_SelectedAssetKind=="Mesh")resources->LoadAsync<MeshResource>(m_SelectedAsset);else if(m_SelectedAssetKind=="Texture")resources->LoadAsync<TextureResource>(m_SelectedAsset);else if(m_SelectedAssetKind=="Environment")resources->LoadAsync<EnvironmentResource>(m_SelectedAsset);
        }
        if(m_SelectedAssetKind=="Material"){auto r=resources->Get<MaterialResource>(m_SelectedAsset);if(r&&r->IsLoaded()){const auto& d=r->GetData();ImGui::Text("%s / Metallic %.2f / Roughness %.2f",d.shadingModel==ShadingModel::PBR?"PBR":"Lambert",d.metallic,d.roughness);}}
        if(m_SelectedAssetKind=="Mesh"){auto r=resources->Get<MeshResource>(m_SelectedAsset);if(r&&r->IsLoaded()){const auto& d=r->GetData().meshData;ImGui::Text("%zu vertices / %zu indices / %zu material ranges",d.vertices.size(),d.indices.size(),d.submeshes.size());}}
        if(m_SelectedAssetKind=="Texture"){auto r=resources->Get<TextureResource>(m_SelectedAsset);if(r&&r->IsLoaded()){const auto& d=r->GetData().textureData;ImGui::Text("%d x %d / RGBA / %.2f MiB decoded",d.width,d.height,d.pixels.size()/1048576.);}}
        if(m_SelectedAssetKind=="Environment"){auto r=resources->Get<EnvironmentResource>(m_SelectedAsset);if(r&&r->IsLoaded()){const auto& d=r->GetData();ImGui::Text("HDR %u x %u / sky %u / IBL %u",d.width,d.height,d.sky.resolution,d.environment.resolution);}}
    }
    DrawAssetPreview(app);
    ImGui::TextDisabled("Type previews; content is indexed on Refresh, never every frame.");
    if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::EndChild();
    if(m_AssetActionRequested){ImGui::OpenPopup("Material Asset Action");m_AssetActionRequested=false;}
    if(ImGui::BeginPopupModal("Material Asset Action",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextUnformatted(m_AssetAction==1?"Create a PBR material (schema v4)":m_AssetAction==2?"Rename session-created material":"Delete session-created material permanently");
        if(m_AssetAction!=3)ImGui::InputText("Asset key / directory",&m_AssetActionKey);else ImGui::TextWrapped("%s",m_AssetActionSource.c_str());
        ImGui::TextWrapped("References in scene, Undo/Redo and saved project JSON block rename/delete. Reassign and save them first. Asset file operations are outside scene Undo.");
        ImGui::BeginDisabled(app.IsEditorPlayMode()||m_MaterialDirty);
        if(ImGui::Button(m_AssetAction==1?"Create":m_AssetAction==2?"Confirm Rename":"Confirm Delete")){
            bool ok=false;
            if(m_AssetAction!=1&&AssetReferenced(app,m_AssetActionSource))m_EditorError="Asset is referenced by this scene or its Undo/Redo history";
            else if(m_AssetAction==1)ok=CreateMaterialAsset(app,m_AssetActionKey,&m_EditorError);
            else if(m_AssetAction==2)ok=RenameMaterialAsset(app,m_AssetActionSource,m_AssetActionKey,true,&m_EditorError);
            else ok=DeleteMaterialAsset(app,m_AssetActionSource,true,&m_EditorError);
            if(ok){if(resources&&m_AssetAction!=1)resources->Unload<MaterialResource>(m_AssetActionSource);if(m_AssetAction!=3)openMaterial(m_AssetActionKey);else{m_SelectedAsset.clear();if(m_EditingMaterial==m_AssetActionSource)m_EditingMaterial.clear();}m_AssetListsDirty=true;ImGui::CloseCurrentPopup();}
        }ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::EndPopup();
    }
    DrawImportDialogs(app);
    ImGui::End();
}
}
