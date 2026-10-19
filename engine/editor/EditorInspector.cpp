#include "../ecs/FogGather.h"
#include "../render/IRenderAdapter.h"
#include "PrecisionInputs.h"
#include "SunSynchronization.h"
#include "EditorLayer.h"
#include "ScriptEditor.h"
#include "../core/AssetPaths.h"
#include "EditorWidgets.h"
#include "../core/Application.h"
#include "../ecs/components/LuaScriptComponent.h"
#include "../scripting/EntityReference.h"
#include "../ecs/PlayerSpawn.h"
#include "../resources/ResourceManager.h"
#include <imgui.h>
#include <chrono>
#include <misc/cpp/imgui_stdlib.h>
namespace editor {
bool EditorLayer::SetLuaEntityReference(Application& app,ecs::Entity owner,const std::string& field,ecs::Entity target){
    auto& world=app.GetWorld();auto* script=world.GetComponent<ecs::LuaScriptComponent>(owner);
    if(!script||(target.IsValid()&&!world.IsAlive(target)))return false;
    auto schema=app.GetLuaRuntime().Fields(script->scriptPath);
    if(!schema.contains(field)||schema[field].value("type","")!="entity")return false;
    FinishTransaction(app);auto before=CaptureSelectedEntity(app,owner);
    nlohmann::json value=nullptr;
    if(target.IsValid()){
        // Legacy/programmatic objects acquire a persistent identity only when
        // referenced; include that metadata in the same Undo transaction.
        auto* id=world.GetComponent<ecs::SceneIdentityComponent>(target);
        if(!id){if(target!=owner)before.related.push_back(CaptureSelectedEntity(app,target));id=&world.AddComponent<ecs::SceneIdentityComponent>(target);}
        value={{"$entity",id->value}};
        if(scripting::ResolveEntityReference(world,value)!=target)return false;
    }
    script->properties[field]=value;app.NotifyEditorMutation(owner);CommitUndo(app,"Edit Lua entity reference",std::move(before));return true;
}
namespace {
ecs::PropertyValue Initial(const ecs::PropertyDescriptor& p){
    using K=ecs::PropertyKind;
    switch(p.kind){case K::Boolean:return false;case K::Integer:return 0;case K::Number:return p.key.find("roughness")!=std::string::npos?.5f:p.key.find("Scale")!=std::string::npos||p.key.find("Strength")!=std::string::npos?1.f:std::max(p.minimum,0.f);case K::String:case K::Asset:return std::string();case K::Vector:return ecs::Vec3{};case K::Color:return std::array<float,4>{1,1,1,1};case K::AssetList:return std::vector<std::string>{};}return {};
}
bool Drawer(const ecs::PropertyDescriptor& p,ecs::PropertyValue& v,const char* vectorFormat="%.3g",bool degrees=false){
    using K=ecs::PropertyKind;bool changed=false;
    if(p.nullable){bool enabled=!std::holds_alternative<std::monostate>(v);if(ImGui::Checkbox("##override",&enabled)){v=enabled?Initial(p):ecs::PropertyValue{};changed=true;}if(ImGui::IsItemHovered())ImGui::SetTooltip("Override this property for the selected entity; unchecked inherits the material asset.");if(!enabled){ImGui::SameLine();ImGui::TextDisabled("Inherit");return changed;}ImGui::SameLine();}
    switch(p.kind){
    case K::Boolean:changed|=ImGui::Checkbox("##value",&std::get<bool>(v));break;
    case K::Integer:{auto& x=std::get<int>(v);if(p.choices.empty())changed|=PrecisionDragInt("##value",&x,1,int(p.minimum),int(p.maximum));else if(ImGui::BeginCombo("##value",x>=0&&x<int(p.choices.size())?p.choices[x].c_str():"Invalid")){for(int i=0;i<int(p.choices.size());++i)if(ImGui::Selectable(p.choices[i].c_str(),x==i)){x=i;changed=true;}ImGui::EndCombo();}break;}
    case K::Number:changed|=PrecisionDragFloat("##value",&std::get<float>(v),p.speed,p.minimum,p.maximum);break;
    case K::Vector:{auto& x=std::get<ecs::Vec3>(v);auto old=x;ecs::Vec3 display=degrees?ecs::Vec3{Degrees(x.x),Degrees(x.y),Degrees(x.z)}:x;auto initialDisplay=display;
        ImGui::BeginGroup();float* values[]={&display.x,&display.y,&display.z};const char* axes[]={"X","Y","Z"};const ImVec4 colors[]={{.85f,.32f,.32f,1},{.35f,.75f,.43f,1},{.36f,.57f,.95f,1}};
        const float available=ImGui::GetContentRegionAvail().x;const float minimumAxisWidth=std::string_view(vectorFormat)=="%.9g"?120.f:std::string_view(vectorFormat)=="%.6f"?100.f:72.f;
        const bool stacked=available<3*minimumAxisWidth*ImGui::GetStyle().FontScaleDpi;const float width=stacked?available:(available-2*ImGui::GetStyle().ItemSpacing.x)/3;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(2,ImGui::GetStyle().FramePadding.y));
        for(int i=0;i<3;++i){ImGui::PushID(i);if(i&&!stacked)ImGui::SameLine();ImGui::BeginGroup();ImGui::AlignTextToFramePadding();ImGui::TextColored(colors[i],"%s",axes[i]);const float axisWidth=ImGui::CalcTextSize(axes[i]).x;ImGui::SameLine(0,2);ImGui::SetNextItemWidth(std::max(20.f,width-axisWidth-2));changed|=PrecisionDragFloat("##axis",values[i],p.speed,p.minimum,p.maximum,vectorFormat,ImGuiSliderFlags_NoRoundToFormat);ImGui::EndGroup();ImGui::PopID();}ImGui::PopStyleVar();ImGui::EndGroup();if(changed){
        if(degrees){x=old;if(display.x!=initialDisplay.x)x.x=Radians(display.x);if(display.y!=initialDisplay.y)x.y=Radians(display.y);if(display.z!=initialDisplay.z)x.z=Radians(display.z);}else x=display;
    }break;}
    case K::Color:{auto& color=std::get<std::array<float,4>>(v);if(p.key=="tint")changed|=ImGui::ColorEdit4("##value",color.data(),ImGuiColorEditFlags_NoInputs);else changed|=LinearColorEdit("##value",color.data(),p.colorComponents,ImGuiColorEditFlags_NoInputs);break;}
    case K::String:case K::Asset:changed|=ImGui::InputText("##value",&std::get<std::string>(v));break;
    case K::AssetList:{auto& list=std::get<std::vector<std::string>>(v);for(int i=0;i<int(list.size());++i){ImGui::PushID(i);changed|=ImGui::InputText("Slot",&list[i]);if(ImGui::BeginDragDropTarget()){if(auto* payload=ImGui::AcceptDragDropPayload("WHISP_ASSET"))if(payload->DataSize==sizeof(AssetPayload)){auto* data=static_cast<const AssetPayload*>(payload->Data);if(data->kind==p.asset){list[i]=data->key;changed=true;}}ImGui::EndDragDropTarget();}ImGui::SameLine();if(ImGui::SmallButton("X")){list.erase(list.begin()+i);changed=true;ImGui::PopID();break;}ImGui::PopID();}if(ImGui::SmallButton("Add slot")){list.emplace_back();changed=true;}break;}
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
    auto& world=app.GetWorld();auto e=m_Selection.active;
    if(!world.IsAlive(e)){ImGui::TextUnformatted("No entity selected");ImGui::End();return;}
    if(m_Selection.items.size()>1){ImGui::Text("%zu entities selected",m_Selection.items.size());ImGui::TextWrapped("Properties below affect only the Active Entity. The shared gizmo transforms the group around its center; Local axes follow the Active Entity.");}
    const auto* tag=world.GetComponent<ecs::TagComponent>(e);if(m_Selection.items.size()>1)ImGui::TextUnformatted("Active Entity:");ImGui::SeparatorText(tag?tag->name.c_str():"Entity");
    if(app.IsEditorPlayMode())ImGui::TextWrapped("Runtime edits are discarded on Stop.");
    if(auto parent=world.Parent(e);parent.IsValid()){auto* name=world.GetComponent<ecs::TagComponent>(parent);ImGui::Text("Parent: %s",name?name->name.c_str():"Entity");}
    if(ImGui::Checkbox("World coordinates",&m_InspectorWorldTransform)){FinishTransaction(app);m_PropertyDrafts.clear();}
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Transform fields use World coordinates when enabled, otherwise Local coordinates relative to Parent.");
    ImGui::Combo("Transform precision",&m_TransformPrecision,"Compact\0Precise\0Full Precision (9 significant digits)\0");
    const auto& registry=ecs::ComponentRegistry::Builtins();ImGui::PushID(int(e.index));ImGui::PushID(int(e.generation));
    if(ImGui::Button("Add Component"))ImGui::OpenPopup("AddComponent");
    if(ImGui::BeginPopup("AddComponent")){for(const auto& d:registry.Components())if(d.addable&&!d.has(world,e))if(ImGui::MenuItem(d.label.c_str()))AddComponent(app,e,d.id);ImGui::EndPopup();}
    ImGui::PushItemWidth(std::max(90.f,ImGui::GetContentRegionAvail().x*.55f));
    for(const auto& d:registry.Components()){
        if(!d.has(world,e)||d.key=="SceneIdentity")continue;ImGui::PushID(d.key.c_str());
        bool open=ImGui::CollapsingHeader(d.label.c_str(),d.key=="Tag"||d.key=="MeshRenderer"||d.key=="Rigidbody"||d.key=="Collider"||d.key=="Velocity"||d.key=="BoundsBounce"?ImGuiTreeNodeFlags_None:ImGuiTreeNodeFlags_DefaultOpen);
        if(ImGui::BeginPopupContextItem("ComponentActions")){if(ImGui::MenuItem("Reset",nullptr,false,d.removable))ResetComponent(app,e,d.id);if(ImGui::MenuItem("Remove",nullptr,false,d.removable)){RemoveComponent(app,e,d.id);ImGui::EndPopup();ImGui::PopID();continue;}ImGui::EndPopup();}
        if(open){auto drawProperty=[&](const ecs::PropertyDescriptor& p){
            if(p.internal)return;
            ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(d.key=="Transform"&&p.key=="rotation"?"Rotation (\xC2\xB0)":p.label.c_str());if(ImGui::IsItemHovered()&&!p.tooltip.empty())ImGui::SetTooltip("%s",p.tooltip.c_str());ImGui::TableSetColumnIndex(1);ImGui::SetNextItemWidth(-1);

            ImGui::PushID(p.key.c_str());const auto id=ImGui::GetID("PropertyDraft");auto draft=m_PropertyDrafts.find(id);
            auto value=draft!=m_PropertyDrafts.end()?draft->second:p.get(world,e);
            if(draft==m_PropertyDrafts.end()&&m_InspectorWorldTransform&&d.key=="Transform"){auto t=world.WorldTransform(e);if(p.key=="position")value=t.position;else if(p.key=="rotation")value=t.rotation;else if(p.key=="scale")value=t.scale;}
            const bool skyDriven=d.key=="Transform"&&p.key=="rotation"&&app.GetSceneBackground().sunSync==SunSyncMode::SkyDrivesLight&&app.GetSceneBackground().sunMetadata&&world.HasComponent<ecs::DirectionalLightComponent>(e)&&PrimarySun(world)==e;
            const bool readOnly=p.readOnly||skyDriven;
            ImGui::BeginDisabled(readOnly);bool changed=Drawer(p,value,d.key=="Transform"?TransformFormat(m_TransformPrecision):"%.6g",d.key=="Transform"&&p.key=="rotation");bool activated=ImGui::IsItemActivated();bool deactivated=ImGui::IsItemDeactivatedAfterEdit();ImGui::EndDisabled();
            if(skyDriven&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Sky Drives Light controls this orientation. Select Independent to edit it manually.");else if(p.readOnly&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Readonly: %s",p.tooltip.c_str());else if(ImGui::IsItemHovered()&&!p.tooltip.empty())ImGui::SetTooltip("%s",p.tooltip.c_str());
            if(activated||changed){if(!m_Transaction)PushUndoBeforeChange(app,"Edit "+d.label+" / "+p.label);m_PropertyDrafts[id]=value;}
            if(changed){
                bool applied=false;
                if(m_InspectorWorldTransform&&d.key=="Transform"){auto target=world.WorldTransform(e);if(p.key=="position")target.position=std::get<ecs::Vec3>(value);else if(p.key=="rotation")target.rotation=std::get<ecs::Vec3>(value);else if(p.key=="scale")target.scale=std::get<ecs::Vec3>(value);applied=world.SetWorldTransform(e,target,&m_EditorError);if(applied)app.NotifyEditorMutation(e);}
                else applied=SetProperty(app,e,d.id,p.key,value,false);
                if(applied)m_EditorError.clear();else ImGui::TextColored(ImVec4(1,.45f,.35f,1),"%s",m_EditorError.c_str());
            }
            if(deactivated){FinishTransaction(app);m_PropertyDrafts.erase(id);}
            // Combo/checkbox/drop actions are discrete; numeric/text drag owns a pending transaction.
            if(changed&&!ImGui::IsAnyItemActive()){FinishTransaction(app);m_PropertyDrafts.erase(id);}
            if(p.kind==ecs::PropertyKind::Asset&&!std::holds_alternative<std::monostate>(value)){
                const auto& key=std::get<std::string>(value);switch(p.asset){case ecs::AssetKind::Mesh:AssetState<MeshResource>(app.GetResourceManager(),key);break;case ecs::AssetKind::Texture:AssetState<TextureResource>(app.GetResourceManager(),key);break;case ecs::AssetKind::Material:AssetState<MaterialResource>(app.GetResourceManager(),key);break;case ecs::AssetKind::Shader:AssetState<ShaderResource>(app.GetResourceManager(),key);break;default:break;}
            }
            ImGui::PopID();
        };
        auto drawSection=[&](const std::string& section,bool advanced){
            if(ImGui::BeginTable("Properties",2,ImGuiTableFlags_SizingStretchProp)){
                ImGui::TableSetupColumn("Property",ImGuiTableColumnFlags_WidthFixed,std::min(110*ImGui::GetStyle().FontScaleDpi,ImGui::GetContentRegionAvail().x*.42f));ImGui::TableSetupColumn("Value",ImGuiTableColumnFlags_WidthStretch);
                for(const auto& p:d.properties)if(p.section==section&&p.advanced==advanced)drawProperty(p);ImGui::EndTable();
            }
        };
        if(d.key=="Material"){
            for(const char* section:{"Surface","Base Color","Metallic / Roughness","Normal Map","AO","Rendering","Advanced"}){
                bool primary=std::string(section)=="Surface"||std::string(section)=="Base Color"||std::string(section)=="Metallic / Roughness";
                if(ImGui::TreeNodeEx(section,primary?ImGuiTreeNodeFlags_DefaultOpen:0)){drawSection(section,false);ImGui::TreePop();}
            }
        }else if(d.key=="Fog"){
            drawSection("",false);auto mode=world.GetComponent<ecs::FogComponent>(e)->mode;
            if(mode!=FogMode::Distance){ImGui::SeparatorText("Height profile (world space)");drawSection("Medium",false);}
            ImGui::SeparatorText(mode==FogMode::Distance?"Distance":mode==FogMode::Height?"Height":"Volumetric");
            drawSection(mode==FogMode::Distance?"Distance":mode==FogMode::Height?"Height":"Volumetric",false);
            const auto fog=ecs::GatherFog(world);if(fog.activeCount>1)ImGui::TextWrapped("Conflict: %u enabled Fog components. Lowest Entity index wins.",fog.activeCount);
            if(fog.invalidCount)ImGui::TextWrapped("Invalid Fog components ignored: %u",fog.invalidCount);
            ImGui::TextWrapped("Global environment; Transform is ignored. Colors are linear. Height density is constant below Base Height and decreases above it.");
            if(auto* renderer=app.GetRenderAdapter()){auto stats=renderer->GetFogDiagnostics();if(!stats.error.empty())ImGui::TextWrapped("%s",stats.error.c_str());ImGui::Text("Fog: %u steps, CPU %.3f / GPU %.3f ms, %.2f MiB",stats.samples,stats.cpuMs,stats.gpuMs,double(stats.gpuBytes)/(1024*1024));}
        }else{drawSection("",false);bool hasAdvanced=std::any_of(d.properties.begin(),d.properties.end(),[](const auto& p){return p.advanced&&!p.internal;});if(hasAdvanced&&ImGui::TreeNode("Advanced")){drawSection("",true);ImGui::TreePop();}}
        if(d.key=="LuaScript"){
            auto* script=world.GetComponent<ecs::LuaScriptComponent>(e);
            if(ImGui::BeginCombo("Choose Script",script->scriptPath.c_str())){
                for(const auto& asset:m_AssetCatalog.Entries())if(m_AssetCatalog.Types()[asset.type].kind==ecs::AssetKind::LuaScript)
                    if(ImGui::Selectable(asset.key.c_str(),script->scriptPath==asset.key))SetProperty(app,e,d.id,"scriptPath",asset.key);
                ImGui::EndCombo();
            }
            auto& runtime=app.GetLuaRuntime();
            if(ImGui::Button("Reload Script"))runtime.ReloadScript(script->scriptPath);
            ImGui::SameLine();ImGui::BeginDisabled(script->scriptPath.empty());
            if(ImGui::Button("Open Script"))OpenLuaScriptInVisualStudio(script->scriptPath,&m_EditorError);
            ImGui::EndDisabled();
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)){
                try{const auto path=AssetPaths::ToUtf8String(ResolveEditableLuaScript(script->scriptPath));ImGui::SetTooltip("Open in Visual Studio 2022:\n%s",path.c_str());}
                catch(const std::exception& failure){ImGui::SetTooltip("%s",script->scriptPath.empty()?"Assign a Lua script first":failure.what());}
            }
            auto schema=runtime.Fields(script->scriptPath);
            auto error=runtime.Error(script->scriptPath);
            if(error.empty())ImGui::TextDisabled("Loaded: %zu fields",schema.size());else ImGui::TextWrapped("%s",error.c_str());
            for(auto field=schema.begin();field!=schema.end();++field){
                ImGui::PushID(field.key().c_str());
                try{
                    const auto& metadata=field.value();const auto type=metadata.at("type").get<std::string>();
                    auto value=script->properties.contains(field.key())?script->properties.at(field.key()):metadata.at("default");
                    if(type=="entity"){
                        if(!scripting::ValidEntityReference(value))throw std::runtime_error("Malformed entity reference");
                        auto target=scripting::ResolveEntityReference(world,value);std::string label=value.is_null()?"None":"Missing entity";
                        if(target.IsValid()){auto* tag=world.GetComponent<ecs::TagComponent>(target);label=tag?tag->name:"Unnamed entity";}
                        ImGui::TextUnformatted(metadata.value("label",field.key()).c_str());
                        if(ImGui::BeginCombo("##entity",label.c_str())){
                            if(ImGui::Selectable("None",value.is_null()))SetLuaEntityReference(app,e,field.key());
                            for(auto candidate:world.Entities()){
                                auto* tag=world.GetComponent<ecs::TagComponent>(candidate);ImGui::PushID(int(candidate.index));
                                if(ImGui::Selectable(tag?tag->name.c_str():"Unnamed entity",candidate==target))SetLuaEntityReference(app,e,field.key(),candidate);
                                ImGui::PopID();
                            }ImGui::EndCombo();
                        }
                        if(ImGui::BeginDragDropTarget()){
                            if(auto* payload=ImGui::AcceptDragDropPayload("WHISP_ENTITY"))if(payload->DataSize==sizeof(ecs::Entity))SetLuaEntityReference(app,e,field.key(),*static_cast<const ecs::Entity*>(payload->Data));
                            ImGui::EndDragDropTarget();
                        }
                        if(!value.is_null()){ImGui::SameLine();if(ImGui::SmallButton("X"))SetLuaEntityReference(app,e,field.key());}
                        ImGui::PopID();continue;
                    }
                    ecs::PropertyDescriptor p;p.key=field.key();p.label=metadata.value("label",field.key());p.minimum=metadata.value("min",-1e6f);p.maximum=metadata.value("max",1e6f);
                    ecs::PropertyValue v;
                    if(type=="bool"){p.kind=ecs::PropertyKind::Boolean;v=value.get<bool>();}
                    else if(type=="integer"){p.kind=ecs::PropertyKind::Integer;v=value.get<int>();}
                    else if(type=="number"){p.kind=ecs::PropertyKind::Number;v=value.get<float>();}
                    else if(type=="vec3"){p.kind=ecs::PropertyKind::Vector;v=ecs::Vec3{value.at(0).get<float>(),value.at(1).get<float>(),value.at(2).get<float>()};}
                    else {p.kind=type=="asset"?ecs::PropertyKind::Asset:ecs::PropertyKind::String;v=value.get<std::string>();if(type=="asset")p.asset=metadata.at("default").get<std::string>().ends_with(".prefab.json")?ecs::AssetKind::Prefab:ecs::AssetKind::None;}
                    ImGui::TextUnformatted(p.label.c_str());bool changed=Drawer(p,v);
                    if(changed){
                        PushUndoBeforeChange(app,"Edit Lua / "+p.label);
                        if(type=="bool")value=std::get<bool>(v);else if(type=="integer")value=std::get<int>(v);else if(type=="number")value=std::get<float>(v);else if(type=="vec3"){auto x=std::get<ecs::Vec3>(v);value=nlohmann::json::array({x.x,x.y,x.z});}else value=std::get<std::string>(v);
                        script->properties[field.key()]=value;app.NotifyEditorMutation(e);
                    }
                    if(ImGui::IsItemDeactivatedAfterEdit()||(changed&&!ImGui::IsAnyItemActive()))FinishTransaction(app);
                }catch(const std::exception&){ImGui::TextWrapped("Override incompatible with schema: %s",field.key().c_str());if(ImGui::SmallButton("Use default")){PushUndoBeforeChange(app,"Reset Lua field");script->properties.erase(field.key());app.NotifyEditorMutation(e);FinishTransaction(app);}}
                ImGui::PopID();
            }
        }
        if(d.key=="Transform"){
            ImGui::Checkbox("Rotation Snap",&m_RotationSnapping);ImGui::SameLine();
            if(ImGui::BeginCombo("Step",std::to_string(m_SnapRotation).c_str())){for(float step:{15.f,30.f,45.f,90.f})if(ImGui::Selectable((std::to_string(int(step))+" deg").c_str(),m_SnapRotation==step))m_SnapRotation=step;ImGui::EndCombo();}
            PrecisionDragFloat("Custom step (deg)",&m_SnapRotation,.1f,.1f,180);
            ImGui::Combo("Quarter-turn axis",&m_RotateAxis,"X\0Y\0Z\0");
            ImGui::BeginDisabled(app.IsEditorPlayMode()||app.GetSceneBackground().sunSync==SunSyncMode::SkyDrivesLight&&PrimarySun(world)==e);
            if(ImGui::Button("-90 deg"))RotateSelectedQuarter(app,m_RotateAxis,-1,m_GizmoMode==1);ImGui::SameLine();if(ImGui::Button("+90 deg"))RotateSelectedQuarter(app,m_RotateAxis,1,m_GizmoMode==1);ImGui::EndDisabled();
            ImGui::TextDisabled("Rz*Ry*Rx; Local: R*Q, World: Q*R. Euler gimbal lock remains.");
        }
        if(d.key=="PlayerStart"){
            auto half=ecs::PlayerBodyHalfExtents(*world.GetComponent<ecs::PlayerStartComponent>(e));
            ImGui::TextWrapped("Position marks the feet. Player size: %.2f x %.2f x %.2f. Scale is ignored. Y rotation sets facing; X sets camera pitch. The body stays upright.",half.x*2,half.y*2,half.z*2);
        }
        if(d.key=="PointLight"||d.key=="SpotLight")ImGui::TextWrapped("Point/Spot shadows are a reserved flag. Range is in world units; Transform scale is ignored.");
        if(d.key=="DirectionalLight"||d.key=="SpotLight")ImGui::TextWrapped("Outgoing rays follow Transform local +Z; Inspector rotation is in degrees (stored radians).");
        if(d.key=="Material"){if(ImGui::Button("Open Material Asset Editor")){auto* material=world.GetComponent<ecs::MaterialComponent>(e);m_SelectedAsset=material->materialPath;m_SelectedAssetKind="Material";m_ShowMaterialEditor=true;}}
        }
        ImGui::PopID();
    }
    if(m_Transaction&&!ImGui::IsAnyItemActive()){FinishTransaction(app);m_PropertyDrafts.clear();}
    ImGui::PopItemWidth();if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::PopID();ImGui::PopID();ImGui::End();
}
}
