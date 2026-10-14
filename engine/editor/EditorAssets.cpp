#include "EditorLayer.h"
#include "EditorWidgets.h"
#include "../core/Application.h"
#include "../resources/ResourceManager.h"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <set>
#include <cstring>
namespace editor {
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
void EditorLayer::DrawAssetBrowser(Application& app){
    ImGui::SetNextWindowSizeConstraints(ImVec2(260,140),ImVec2(FLT_MAX,FLT_MAX));if(!ImGui::Begin("Asset Browser",&m_ShowAssetBrowser)){ImGui::End();return;}
    if(ImGui::Button("Refresh project assets"))m_AssetListsDirty=true;ImGui::SameLine();ImGui::InputTextWithHint("##search","Search resource keys",&m_AssetFilter);ImGui::SameLine();
    const char* kinds[]={"All","Mesh","Texture","Material","Shader","Environment"};ImGui::SetNextItemWidth(130);ImGui::Combo("Type",&m_AssetKindFilter,kinds,6);
    auto openMaterial=[&](const std::string& key){if(m_MaterialDirty&&key!=m_EditingMaterial){m_EditorError="Save or revert the current material before opening another asset";Logger::Get().Warn(m_EditorError);return;}m_EditingMaterial=key;m_ShowMaterialEditor=true;if(auto* r=app.GetResourceManager())r->LoadAsync<MaterialResource>(key);};
    struct Row {const std::string* key;ecs::AssetKind kind;};std::vector<Row> rows;
    auto add=[&](const auto& keys,ecs::AssetKind kind){for(const auto& key:keys)rows.push_back({&key,kind});};
    add(m_MeshAssets,ecs::AssetKind::Mesh);add(m_TextureAssets,ecs::AssetKind::Texture);add(m_MaterialAssets,ecs::AssetKind::Material);add(m_ShaderAssets,ecs::AssetKind::Shader);add(m_EnvironmentAssets,ecs::AssetKind::Environment);
    ImGui::BeginChild("Directories",ImVec2(std::clamp(ImGui::GetContentRegionAvail().x*.2f,100.f,200.f),0),ImGuiChildFlags_Borders);
    if(ImGui::Selectable("Project",m_AssetDirectory.empty()))m_AssetDirectory.clear();std::set<std::string> directories;
    for(const auto& row:rows){auto slash=row.key->find_last_of('/');if(slash!=std::string::npos)directories.insert(row.key->substr(0,slash));}
    for(const auto& folder:directories)if(ImGui::Selectable(folder.c_str(),m_AssetDirectory==folder))m_AssetDirectory=folder;ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("Assets");
    std::erase_if(rows,[&](const Row& row){if(m_AssetKindFilter&&int(row.kind)!=m_AssetKindFilter)return true;if(!m_AssetDirectory.empty()&&!row.key->starts_with(m_AssetDirectory+"/"))return true;
        return !m_AssetFilter.empty()&&std::search(row.key->begin(),row.key->end(),m_AssetFilter.begin(),m_AssetFilter.end(),[](unsigned char a,unsigned char b){return std::tolower(a)==std::tolower(b);})==row.key->end();});
    ImGuiListClipper clipper;clipper.Begin(int(rows.size()));while(clipper.Step())for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i){auto& row=rows[i];const auto& key=*row.key;ImGui::PushID(key.c_str());
        if(ImGui::Selectable(key.c_str(),m_SelectedAsset==key)){m_SelectedAsset=key;m_SelectedAssetKind=kinds[int(row.kind)];}
        if(ImGui::BeginDragDropSource()){AssetPayload payload{};payload.kind=row.kind;std::memcpy(payload.key,key.data(),std::min(key.size(),sizeof(payload.key)-1));ImGui::SetDragDropPayload("WHISP_ASSET",&payload,sizeof(payload));ImGui::Text("%s: %s",kinds[int(row.kind)],key.c_str());ImGui::EndDragDropSource();}
        if(ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0)){
            if(row.kind==ecs::AssetKind::Material){m_SelectedAsset=key;m_SelectedAssetKind="Material";openMaterial(key);}
            else AssignAsset(app,m_SelectedEntity,row.kind,key);
        }
        if(ImGui::BeginPopupContextItem("AssetActions")){if(ImGui::MenuItem("Assign to selection"))AssignAsset(app,m_SelectedEntity,row.kind,key);if(row.kind==ecs::AssetKind::Material&&ImGui::MenuItem("Open Material Editor")){openMaterial(key);}ImGui::EndPopup();}
        ImGui::PopID();
    }
    ImGui::Separator();ImGui::TextWrapped("Selected: %s",m_SelectedAsset.c_str());if(!m_SelectedAsset.empty()&&ImGui::Button("Assign selected asset")){
        for(const auto& row:rows)if(*row.key==m_SelectedAsset){AssignAsset(app,m_SelectedEntity,row.kind,m_SelectedAsset);break;}
    }
    if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::TextDisabled("Relative resource keys; large resources load asynchronously.");ImGui::EndChild();ImGui::End();
}
}
