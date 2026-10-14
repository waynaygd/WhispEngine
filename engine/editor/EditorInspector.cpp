#include "EditorLayer.h"
#include "EditorWidgets.h"
#include "../core/Application.h"
#include "../resources/ResourceManager.h"
#include <imgui.h>
#include <chrono>
#include <misc/cpp/imgui_stdlib.h>
namespace editor {
namespace {
ecs::PropertyValue Initial(const ecs::PropertyDescriptor& p){
    using K=ecs::PropertyKind;
    switch(p.kind){case K::Boolean:return false;case K::Integer:return 0;case K::Number:return p.key.find("roughness")!=std::string::npos?.5f:p.key.find("Scale")!=std::string::npos||p.key.find("Strength")!=std::string::npos?1.f:std::max(p.minimum,0.f);case K::String:case K::Asset:return std::string();case K::Vector:return ecs::Vec3{};case K::Color:return std::array<float,4>{1,1,1,1};case K::AssetList:return std::vector<std::string>{};}return {};
}
bool Drawer(const ecs::PropertyDescriptor& p,ecs::PropertyValue& v){
    using K=ecs::PropertyKind;bool changed=false;
    if(p.nullable){bool enabled=!std::holds_alternative<std::monostate>(v);if(ImGui::Checkbox("Override",&enabled)){v=enabled?Initial(p):ecs::PropertyValue{};changed=true;}if(!enabled){ImGui::SameLine();ImGui::Text("%s (inherit)",p.label.c_str());return changed;}ImGui::SameLine();}
    switch(p.kind){
    case K::Boolean:changed|=ImGui::Checkbox(p.label.c_str(),&std::get<bool>(v));break;
    case K::Integer:{auto& x=std::get<int>(v);if(p.choices.empty())changed|=ImGui::DragInt(p.label.c_str(),&x,1,int(p.minimum),int(p.maximum));else if(ImGui::BeginCombo(p.label.c_str(),x>=0&&x<int(p.choices.size())?p.choices[x].c_str():"Invalid")){for(int i=0;i<int(p.choices.size());++i)if(ImGui::Selectable(p.choices[i].c_str(),x==i)){x=i;changed=true;}ImGui::EndCombo();}break;}
    case K::Number:changed|=ImGui::DragFloat(p.label.c_str(),&std::get<float>(v),p.speed,p.minimum,p.maximum);break;
    case K::Vector:{auto& x=std::get<ecs::Vec3>(v);changed|=ImGui::DragFloat3(p.label.c_str(),&x.x,p.speed,p.minimum,p.maximum);break;}
    case K::Color:{auto& color=std::get<std::array<float,4>>(v);if(p.key=="tint")changed|=ImGui::ColorEdit4(p.label.c_str(),color.data());else changed|=LinearColorEdit(p.label.c_str(),color.data(),p.colorComponents);break;}
    case K::String:case K::Asset:changed|=ImGui::InputText(p.label.c_str(),&std::get<std::string>(v));break;
    case K::AssetList:{auto& list=std::get<std::vector<std::string>>(v);ImGui::TextUnformatted(p.label.c_str());for(int i=0;i<int(list.size());++i){ImGui::PushID(i);changed|=ImGui::InputText("Slot",&list[i]);if(ImGui::BeginDragDropTarget()){if(auto* payload=ImGui::AcceptDragDropPayload("WHISP_ASSET"))if(payload->DataSize==sizeof(AssetPayload)){auto* data=static_cast<const AssetPayload*>(payload->Data);if(data->kind==p.asset){list[i]=data->key;changed=true;}}ImGui::EndDragDropTarget();}ImGui::SameLine();if(ImGui::SmallButton("X")){list.erase(list.begin()+i);changed=true;ImGui::PopID();break;}ImGui::PopID();}if(ImGui::SmallButton("Add slot")){list.emplace_back();changed=true;}break;}
    }
    if(p.kind==K::Asset&&ImGui::BeginDragDropTarget()){if(auto* payload=ImGui::AcceptDragDropPayload("WHISP_ASSET"))if(payload->DataSize==sizeof(AssetPayload)){auto* data=static_cast<const AssetPayload*>(payload->Data);if(data->kind==p.asset){v=std::string(data->key);changed=true;}}ImGui::EndDragDropTarget();}
    return changed;
}
template<class T>void AssetState(ResourceManager* resources,const std::string& key){
    if(!resources||key.empty())return;auto resource=resources->Get<T>(key);if(!resource){ImGui::TextDisabled("Not requested yet; renderer resolves asynchronously");return;}
    if(resource->IsLoading())ImGui::TextDisabled("Loading...");else if(resource->IsFailed())ImGui::TextWrapped("Missing/invalid asset: %s",resource->GetErrorMessage().c_str());
}
}
void EditorLayer::DrawInspector(Application& app){
    const auto start=std::chrono::steady_clock::now();
    struct Finish {double& value;std::chrono::steady_clock::time_point start;~Finish(){value=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}} finish{m_Statistics.inspectorMs,start};
    ImGui::SetNextWindowSizeConstraints(ImVec2(270,180),ImVec2(FLT_MAX,FLT_MAX));
    if(!ImGui::Begin("Inspector",&m_ShowInspector)){ImGui::End();return;}
    auto& world=app.GetWorld();auto e=m_SelectedEntity;
    if(!world.IsAlive(e)){ImGui::TextUnformatted("Select an entity");ImGui::End();return;}
    ImGui::Text("%s World %llu / Entity %u:%u",app.IsEditorPlayMode()?"Runtime":"Editor",static_cast<unsigned long long>(world.GetIdentity()),e.index,e.generation);
    if(app.IsEditorPlayMode())ImGui::TextWrapped("Runtime edits are discarded on Stop.");
    const auto& registry=ecs::ComponentRegistry::Builtins();ImGui::PushID(int(e.index));ImGui::PushID(int(e.generation));
    if(ImGui::Button("Add Component"))ImGui::OpenPopup("AddComponent");
    if(ImGui::BeginPopup("AddComponent")){for(const auto& d:registry.Components())if(d.addable&&!d.has(world,e))if(ImGui::MenuItem(d.label.c_str()))AddComponent(app,e,d.id);ImGui::EndPopup();}
    ImGui::PushItemWidth(std::max(90.f,ImGui::GetContentRegionAvail().x*.55f));
    for(const auto& d:registry.Components()){
        if(!d.has(world,e))continue;ImGui::PushID(d.key.c_str());
        bool open=ImGui::CollapsingHeader(d.label.c_str(),d.key=="SceneIdentity"?ImGuiTreeNodeFlags_None:ImGuiTreeNodeFlags_DefaultOpen);
        if(ImGui::BeginPopupContextItem("ComponentActions")){if(ImGui::MenuItem("Reset",nullptr,false,d.removable))ResetComponent(app,e,d.id);if(ImGui::MenuItem("Remove",nullptr,false,d.removable)){RemoveComponent(app,e,d.id);ImGui::EndPopup();ImGui::PopID();continue;}ImGui::EndPopup();}
        if(open){for(const auto& p:d.properties){
            ImGui::PushID(p.key.c_str());const auto id=ImGui::GetID("PropertyDraft");auto draft=m_PropertyDrafts.find(id);
            auto value=draft!=m_PropertyDrafts.end()?draft->second:p.get(world,e);
            ImGui::BeginDisabled(p.readOnly);bool changed=Drawer(p,value);bool activated=ImGui::IsItemActivated();bool deactivated=ImGui::IsItemDeactivatedAfterEdit();ImGui::EndDisabled();
            if(p.readOnly&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Readonly: %s",p.tooltip.c_str());else if(ImGui::IsItemHovered()&&!p.tooltip.empty())ImGui::SetTooltip("%s",p.tooltip.c_str());
            if(activated||changed){if(!m_Transaction)PushUndoBeforeChange(app,"Edit "+d.label+" / "+p.label);m_PropertyDrafts[id]=value;}
            if(changed){if(SetProperty(app,e,d.id,p.key,value,false))m_EditorError.clear();}
            if(deactivated){FinishTransaction(app);m_PropertyDrafts.erase(id);}
            // Combo/checkbox/drop actions are discrete; numeric/text drag owns a pending transaction.
            if(changed&&!ImGui::IsAnyItemActive()){FinishTransaction(app);m_PropertyDrafts.erase(id);}
            if(p.kind==ecs::PropertyKind::Asset&&!std::holds_alternative<std::monostate>(value)){
                const auto& key=std::get<std::string>(value);switch(p.asset){case ecs::AssetKind::Mesh:AssetState<MeshResource>(app.GetResourceManager(),key);break;case ecs::AssetKind::Texture:AssetState<TextureResource>(app.GetResourceManager(),key);break;case ecs::AssetKind::Material:AssetState<MaterialResource>(app.GetResourceManager(),key);break;case ecs::AssetKind::Shader:AssetState<ShaderResource>(app.GetResourceManager(),key);break;default:break;}
            }
            ImGui::PopID();
        }
        if(d.key=="PointLight"||d.key=="SpotLight")ImGui::TextWrapped("Point/Spot shadows are a reserved flag. Range is in world units; Transform scale is ignored.");
        if(d.key=="DirectionalLight"||d.key=="SpotLight")ImGui::TextWrapped("Outgoing rays follow Transform local +Z; rotation is in radians.");
        if(d.key=="Material"){if(ImGui::Button("Open Material Asset Editor")){auto* material=world.GetComponent<ecs::MaterialComponent>(e);m_SelectedAsset=material->materialPath;m_SelectedAssetKind="Material";m_ShowMaterialEditor=true;}}
        }
        ImGui::PopID();
    }
    ImGui::PopItemWidth();if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::PopID();ImGui::PopID();ImGui::End();
}
}
