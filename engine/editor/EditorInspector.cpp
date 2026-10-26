#include "../ecs/FogGather.h"
#include "../ecs/PostProcessEvaluation.h"
#include "../render/IRenderAdapter.h"
#include "PrecisionInputs.h"
#include "InspectorControls.h"
#include "SunSynchronization.h"
#include "EditorLayer.h"
#include "ScriptEditor.h"
#include "../core/AssetPaths.h"
#include "EditorWidgets.h"
#include "../core/Application.h"
#include "../ecs/components/LuaScriptComponent.h"
#include "../scripting/EntityReference.h"
#include "../scripting/Prefab.h"
#include "../ecs/components/PrefabInstanceComponent.h"
#include "../ecs/PlayerSpawn.h"
#include "../resources/ResourceManager.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <charconv>
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
nlohmann::json PropertyJson(const ecs::PropertyValue& value){
    return std::visit([](const auto& v)->nlohmann::json{using T=std::decay_t<decltype(v)>;if constexpr(std::is_same_v<T,std::monostate>)return nullptr;else if constexpr(std::is_same_v<T,ecs::Vec3>)return nlohmann::json::array({v.x,v.y,v.z});else return v;},value);
}
std::string PropertyGroup(const ecs::ComponentDescriptor& d,const ecs::PropertyDescriptor& p){
    if(d.key=="Transform")return p.key=="pivot"?"Advanced":"";
    if(d.key=="MeshRenderer")return p.advanced?"Advanced":p.kind==ecs::PropertyKind::Asset?"Resources":"Rendering";
    if(d.key=="DirectionalLight"||d.key=="PointLight"||d.key=="SpotLight")return p.key=="castShadows"?"Shadows":p.key=="range"||p.key=="attenuation"||p.key=="innerAngle"||p.key=="outerAngle"?"Shape / Range":"General";
    if(d.key=="Rigidbody")return p.advanced?"Advanced":"Body";
    if(d.key=="Collider")return p.advanced?"Advanced":"Shape / Collision";
    if(d.key=="LuaScript")return p.kind==ecs::PropertyKind::Asset?"Resources":"Permissions";
    return p.advanced?"Advanced":p.section;
}
ecs::PropertyValue Initial(const ecs::PropertyDescriptor& p){
    using K=ecs::PropertyKind;
    const PostProcessSettings postDefaults;
#define PP_INITIAL(name,target,lo,hi,section,label) if(p.key==#name)return postDefaults.target;
    WHISP_PP_FLOATS(PP_INITIAL)
#undef PP_INITIAL
    if(p.key=="fogEnabled"||p.key=="sceneLighting"||p.key=="toneMapping")return true;
    if(p.key=="fogMode")return int(postDefaults.fog.mode);
    if(p.key=="fogQuality")return int(postDefaults.fog.quality);
    if(p.key=="tint")return postDefaults.screenEffects.tint;
    if(p.key=="fogColor")return postDefaults.fog.color;
    if(p.key=="scatteringColor")return postDefaults.fog.scatteringColor;
    switch(p.kind){case K::Boolean:return false;case K::Integer:return 0;case K::Number:return p.key.find("roughness")!=std::string::npos?.5f:p.key.find("Scale")!=std::string::npos||p.key.find("Strength")!=std::string::npos?1.f:std::max(p.minimum,0.f);case K::String:case K::Asset:return std::string();case K::Vector:return ecs::Vec3{};case K::Color:return std::array<float,4>{1,1,1,1};case K::AssetList:return std::vector<std::string>{};}return {};
}
bool Drawer(const ecs::PropertyDescriptor& p,ecs::PropertyValue& v,const char* vectorFormat="%.3g",bool degrees=false,const std::function<bool(const ecs::PropertyDescriptor&,std::string&)>& resource={}){
    using K=ecs::PropertyKind;bool changed=false;
    if(p.nullable){bool enabled=!std::holds_alternative<std::monostate>(v);if(ImGui::Checkbox("##override",&enabled)){v=enabled?Initial(p):ecs::PropertyValue{};changed=true;}if(ImGui::IsItemHovered())ImGui::SetTooltip("Override this property for the selected entity; unchecked inherits the underlying asset or evaluated scene state.");if(!enabled){ImGui::SameLine();ImGui::TextDisabled("Inherit");return changed;}ImGui::SameLine();}
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
    case K::Color:{auto& color=std::get<std::array<float,4>>(v);if(p.key=="tint"){if(p.colorComponents==3)changed|=ImGui::ColorEdit3("##value",color.data(),ImGuiColorEditFlags_NoInputs);else changed|=ImGui::ColorEdit4("##value",color.data(),ImGuiColorEditFlags_NoInputs);}else changed|=LinearColorEdit("##value",color.data(),p.colorComponents,ImGuiColorEditFlags_NoInputs);break;}
    case K::String:changed|=ImGui::InputText("##value",&std::get<std::string>(v));break;
    case K::Asset:changed|=resource?resource(p,std::get<std::string>(v)):ImGui::InputText("##value",&std::get<std::string>(v));break;
    case K::AssetList:{auto& list=std::get<std::vector<std::string>>(v);for(int i=0;i<int(list.size());++i){ImGui::PushID(i);changed|=resource?resource(p,list[i]):ImGui::InputText("Slot",&list[i]);if(!resource)changed|=AcceptAssetDrop(p.asset,list[i]);if(ImGui::SmallButton("Remove slot")){list.erase(list.begin()+i);changed=true;ImGui::PopID();break;}ImGui::PopID();}if(ImGui::SmallButton("Add slot")){list.emplace_back();changed=true;}break;}
    }
    if(!resource&&p.kind==K::Asset&&ImGui::BeginDragDropTarget()){if(auto* payload=ImGui::AcceptDragDropPayload("WHISP_ASSET"))if(payload->DataSize==sizeof(AssetPayload)){auto* data=static_cast<const AssetPayload*>(payload->Data);if(data->kind==p.asset){v=std::string(data->key);changed=true;}}ImGui::EndDragDropTarget();}
    return changed;
}
template<class T>void AssetState(ResourceManager* resources,const std::string& key){
    if(!resources||key.empty())return;auto resource=resources->PeekIndexed<T>(key);if(!resource){ImGui::TextDisabled("Not requested yet; renderer resolves asynchronously");return;}
    if(resource->IsLoading())ImGui::TextDisabled("Loading...");else if(resource->IsFailed())ImGui::TextWrapped("Missing/invalid asset: %s",resource->GetErrorMessage().c_str());
}
}
bool EditorLayer::DrawInspectorResource(const ecs::PropertyDescriptor& p,std::string& key){
    bool changed=false;ImGui::BeginGroup();
    const float available=ImGui::GetContentRegionAvail().x,dpi=ImGui::GetStyle().FontScaleDpi;
    const std::string type=[&]{if(p.asset==ecs::AssetKind::None)return std::string("Resource");for(const auto& t:m_AssetCatalog.Types())if(t.kind==p.asset)return t.name;return std::string("Resource");}();
    const auto slash=key.find_last_of('/');const std::string name=key.empty()?"None":key.substr(slash==std::string::npos?0:slash+1);
    ImGui::SetNextItemWidth(std::max(30.f,available-54*dpi));
    if(p.asset==ecs::AssetKind::None)changed=ImGui::InputText("##value",&key);
    else if(ImGui::BeginCombo("##value",name.c_str())){
        ImGui::TextDisabled("%s",type.c_str());
        if(ImGui::Selectable("None",key.empty())){key.clear();changed=true;}
        for(const auto& a:m_AssetCatalog.Entries())if(m_AssetCatalog.Types()[a.type].kind==p.asset&&ImGui::Selectable(a.key.c_str(),a.key==key)){key=a.key;changed=true;}
        ImGui::EndCombo();
    }
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%s\nDrop a matching asset or choose an indexed resource.",type.c_str(),key.empty()?"None":key.c_str());
    changed|=AcceptAssetDrop(p.asset,key);
    ImGui::SameLine(0,3*dpi);ImGui::BeginDisabled(key.empty());
    if(ImGui::SmallButton("X")){key.clear();changed=true;}
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Clear reference");
    ImGui::SameLine(0,3*dpi);
    if(ImGui::SmallButton(">")){m_ShowAssetBrowser=true;m_AssetFilter.clear();m_AssetKindFilter=m_AssetCategory=0;m_AssetNavigation.projectSearch=false;NavigateAssetFolder(AssetNavigation::Parent(key));std::string selectedType=type;for(const auto& a:m_AssetCatalog.Entries())if(a.key==key){selectedType=m_AssetCatalog.Types()[a.type].name;break;}SelectAsset(key,selectedType);m_AssetScrollSelection=true;}
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Find in Asset Browser");
    ImGui::EndDisabled();
    if(p.asset!=ecs::AssetKind::None&&!key.empty()&&std::none_of(m_AssetCatalog.Entries().begin(),m_AssetCatalog.Entries().end(),[&](const auto& a){return a.key==key&&m_AssetCatalog.Types()[a.type].kind==p.asset;}))ImGui::TextWrapped("Missing from catalog: %s",key.c_str());
    ImGui::EndGroup();return changed;
}
void EditorLayer::DrawBulkInspector(Application& app){
    auto resource=[&](const auto& p,std::string& key){return DrawInspectorResource(p,key);};
    auto& world=app.GetWorld();const auto& registry=ecs::ComponentRegistry::Builtins();
    if(m_BulkUiSelection!=m_Selection.items){FinishTransaction(app);m_BulkText.clear();m_PropertyDrafts.clear();m_BulkUiSelection=m_Selection.items;}
    ImGui::Text("%zu Entities Selected",m_Selection.items.size());ImGui::TextWrapped("Changes apply to all selected entities.");
    if(app.IsPrefabEditing())ImGui::TextDisabled("Editing the open prefab; Save is explicit.");
    else if(std::any_of(m_Selection.items.begin(),m_Selection.items.end(),[&](auto e){return scripting::PrefabRoot(world,e).IsValid();}))
        ImGui::TextWrapped("Prefab sources remain unchanged. Select one instance for Apply/Revert.");
    if(ImGui::IsKeyPressed(ImGuiKey_Escape,false)&&m_Transaction&&m_Transaction->before.bulk){CancelTransaction(app);m_BulkText.clear();return;}
    if(app.IsEditorPlayMode())ImGui::TextWrapped("Stop Play to bulk edit. Runtime changes are discarded on Stop.");
    ImGui::BeginDisabled(app.IsEditorPlayMode());ImGui::PushID("BulkInspector");
    const auto common=CommonInspectorComponents(app);if(common.empty())ImGui::TextWrapped("No shared components. Missing components are never added automatically.");
    auto commit=[&](const ecs::ComponentDescriptor& d,const ecs::PropertyDescriptor& p,const ecs::PropertyValue& value,unsigned mask,bool finish){
        if(SetBulkProperty(app,d.id,p.key,value,mask,m_InspectorWorldTransform,d.key=="Transform"&&m_BulkRelative,false)&&finish)FinishTransaction(app);
    };
    for(auto id:common){const auto& d=*registry.Find(id);if(!inspector::Matches(d.label,m_InspectorSearch)&&!inspector::Matches(d.key,m_InspectorSearch)&&!std::any_of(d.properties.begin(),d.properties.end(),[&](const auto& p){return !p.internal&&(inspector::Matches(p.label,m_InspectorSearch)||inspector::Matches(p.key,m_InspectorSearch));}))continue;
        if(!m_InspectorSearch.empty())ImGui::SetNextItemOpen(true,ImGuiCond_Always);ImGui::PushID(d.key.c_str());
        if(!ImGui::CollapsingHeader(d.label.c_str(),d.key=="Tag"?0:ImGuiTreeNodeFlags_DefaultOpen)){ImGui::PopID();continue;}
        if(d.key=="Transform"){
            if(ImGui::Checkbox("World coordinates",&m_InspectorWorldTransform)){FinishTransaction(app);m_PropertyDrafts.clear();m_BulkText.clear();}
            ImGui::SameLine();if(ImGui::Checkbox("Relative",&m_BulkRelative)){FinishTransaction(app);m_PropertyDrafts.clear();m_BulkText.clear();}
            ImGui::TextDisabled(m_BulkRelative?"Relative: offset / scale factor":"Absolute: replace edited axes only");
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Absolute sets the edited axes for every entity in Local or World coordinates. Local fields are independent of Parent; World fields are solved together. Relative adds Position/Rotation (Euler) offsets and multiplies Scale. Selected descendants follow their selected ancestor once; relative edits affect only selected roots.");
        }
        if(d.key=="LuaScript")ImGui::TextWrapped("Script fields keep their individual values. Select one entity to edit behavior fields.");
        if(inspector::BeginProperties("BulkProperties")){
            std::string section;
            for(const auto& p:d.properties){if(p.internal||(!inspector::Matches(d.label,m_InspectorSearch)&&!inspector::Matches(d.key,m_InspectorSearch)&&!inspector::Matches(p.label,m_InspectorSearch)&&!inspector::Matches(p.key,m_InspectorSearch)))continue;
                if(!p.section.empty()&&p.section!=section){section=p.section;ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::TextDisabled("%s",section.c_str());}
                auto state=InspectBulkProperty(app,d.id,p.key,m_InspectorWorldTransform);if(!state.common)continue;
                inspector::PropertyRow(d.key=="Transform"&&p.key=="rotation"?"Rotation (deg)":p.label.c_str(),p.tooltip.c_str());
                ImGui::PushID(p.key.c_str());const auto draftId=ImGui::GetID("BulkDraft");
                auto draft=m_PropertyDrafts.find(draftId);auto value=draft==m_PropertyDrafts.end()?state.value:draft->second;
                const bool relative=d.key=="Transform"&&m_BulkRelative;
                if(relative&&draft==m_PropertyDrafts.end())value=p.key=="scale"?ecs::PropertyValue(ecs::Vec3{1,1,1}):ecs::PropertyValue(ecs::Vec3{});
                const bool drivenRotation=d.key=="Transform"&&p.key=="rotation"&&app.GetSceneBackground().sunSync==SunSyncMode::SkyDrivesLight&&app.GetSceneBackground().sunMetadata&&m_Selection.Contains(PrimarySun(world));
                const bool drivenLight=d.key=="DirectionalLight"&&(p.key=="color"||p.key=="intensity")&&app.GetSceneBackground().background==EditorBackground::DayNight&&app.GetSceneBackground().driveLightProperties&&m_Selection.Contains(PrimarySun(world));
                const bool readOnly=p.readOnly||((d.key=="PointLight"||d.key=="SpotLight")&&p.key=="castShadows")||drivenRotation||drivenLight||(d.key=="DirectionalLight"&&p.key=="primary");
                ImGui::BeginDisabled(readOnly);bool changed=false,finished=false;unsigned mask=15;
                if(p.nullable){
                    bool enabled=!std::holds_alternative<std::monostate>(state.value);bool mixedEnabled=false;
                    for(auto e:m_Selection.items)mixedEnabled|=(!std::holds_alternative<std::monostate>(p.get(world,e)))!=enabled;
                    if(mixedEnabled)ImGui::PushItemFlag(ImGuiItemFlags_MixedValue,true);
                    bool toggled=ImGui::Checkbox("Override",&enabled);if(mixedEnabled)ImGui::PopItemFlag();
                    if(toggled){commit(d,p,enabled?Initial(p):ecs::PropertyValue{},15,true);ImGui::EndDisabled();ImGui::PopID();continue;}
                    if(!enabled&&!mixedEnabled){ImGui::SameLine();ImGui::TextDisabled("Inherit");ImGui::EndDisabled();ImGui::PopID();continue;}
                }
                if(std::holds_alternative<std::monostate>(value))value=Initial(p);
                auto numeric=[&](float& number,bool mixed,const char* format)->bool{
                    const auto textId=ImGui::GetID("NumberText");ImGui::SetNextItemWidth(-1);
                    if(mixed){
                        auto& buffer=m_BulkText[textId];bool enter=ImGui::InputTextWithHint("##number","Mixed",&buffer,ImGuiInputTextFlags_EnterReturnsTrue);
                        if((enter||ImGui::IsItemDeactivatedAfterEdit())&&!buffer.empty()){
                            float parsed=0;const auto result=std::from_chars(buffer.data(),buffer.data()+buffer.size(),parsed);
                            if(result.ec==std::errc{}&&result.ptr==buffer.data()+buffer.size()&&std::isfinite(parsed)){number=parsed;finished=true;m_BulkText.erase(textId);return true;}
                            m_EditorError="Enter a finite number; the selection was not changed.";
                        }return false;
                    }
                    bool edited=PrecisionDragFloat("##number",&number,p.speed,p.minimum,p.maximum,format);finished|=ImGui::IsItemDeactivatedAfterEdit();return edited;
                };
                using K=ecs::PropertyKind;
                if(p.kind==K::Vector){
                    auto v=std::get<ecs::Vec3>(value);float numbers[]={v.x,v.y,v.z};const char* axes[]={"X","Y","Z"};mask=0;
                    for(int axis=0;axis<3;++axis){ImGui::PushID(axis);ImGui::TextDisabled("%s",axes[axis]);ImGui::SameLine();float display=d.key=="Transform"&&p.key=="rotation"?Degrees(numbers[axis]):numbers[axis];
                        if(numeric(display,!relative&&state.mixed[axis],TransformFormat(m_TransformPrecision))){numbers[axis]=d.key=="Transform"&&p.key=="rotation"?Radians(display):display;mask|=1u<<axis;changed=true;}ImGui::PopID();}
                    value=ecs::Vec3{numbers[0],numbers[1],numbers[2]};
                }else if(p.kind==K::Number){float number=std::get<float>(value);changed=numeric(number,state.IsMixed(),"%.6g");value=number;
                }else if(p.kind==K::Integer){
                    int number=std::get<int>(value);ImGui::SetNextItemWidth(-1);
                    if(!p.choices.empty()){
                        const char* label=state.IsMixed()?"Mixed":number>=0&&number<int(p.choices.size())?p.choices[number].c_str():"Invalid";
                        if(ImGui::BeginCombo("##enum",label)){for(int choice=0;choice<int(p.choices.size());++choice)if(ImGui::Selectable(p.choices[choice].c_str(),!state.IsMixed()&&choice==number)){number=choice;changed=finished=true;}ImGui::EndCombo();}
                    }else if(state.IsMixed()){
                        auto& buffer=m_BulkText[draftId];bool enter=ImGui::InputTextWithHint("##integer","Mixed",&buffer,ImGuiInputTextFlags_EnterReturnsTrue);
                        if((enter||ImGui::IsItemDeactivatedAfterEdit())&&!buffer.empty()){auto result=std::from_chars(buffer.data(),buffer.data()+buffer.size(),number);if(result.ec==std::errc{}&&result.ptr==buffer.data()+buffer.size()){changed=finished=true;m_BulkText.erase(draftId);}else m_EditorError="Enter a whole number; the selection was not changed.";}
                    }else {changed=PrecisionDragInt("##integer",&number,1,int(p.minimum),int(p.maximum));finished=ImGui::IsItemDeactivatedAfterEdit();}value=number;
                }else if(p.kind==K::Boolean){bool enabled=state.IsMixed()?false:std::get<bool>(value);if(state.IsMixed())ImGui::PushItemFlag(ImGuiItemFlags_MixedValue,true);changed=ImGui::Checkbox("##boolean",&enabled);if(state.IsMixed())ImGui::PopItemFlag();value=enabled;finished=changed;
                }else if(p.kind==K::Color){
                    if(state.IsMixed()||ImGui::IsPopupOpen("BulkColor")){
                        if(ImGui::Button(state.IsMixed()?"Mixed":"Choose color"))ImGui::OpenPopup("BulkColor");
                        if(ImGui::BeginPopup("BulkColor")){
                            ImGui::TextUnformatted("Choose a color for all selected entities");auto color=draft!=m_PropertyDrafts.end()?std::get<std::array<float,4>>(draft->second):state.IsMixed()?std::array<float,4>{1,1,1,1}:std::get<std::array<float,4>>(state.value);
                            float display[4];for(int i=0;i<4;++i)display[i]=i<3&&p.key!="tint"?LinearToSrgb(color[i]):color[i];
                            changed=p.colorComponents==3?ImGui::ColorPicker3("##color",display):ImGui::ColorPicker4("##color",display);
                            if(changed){for(int i=0;i<4;++i)color[i]=i<3&&p.key!="tint"?SrgbToLinear(display[i]):display[i];value=color;}
                            finished=ImGui::IsItemDeactivatedAfterEdit();ImGui::EndPopup();
                        }
                    }else {changed=Drawer(p,value,"%.6g",false,resource);finished=ImGui::IsItemDeactivatedAfterEdit();}
                }else if(p.kind==K::String||p.kind==K::Asset){
                    auto it=m_BulkText.find(draftId);if(it==m_BulkText.end())it=m_BulkText.emplace(draftId,state.IsMixed()?"":std::get<std::string>(value)).first;
                    ImGui::SetNextItemWidth(-1);bool enter=ImGui::InputTextWithHint("##text",state.IsMixed()?"Mixed":"",&it->second,ImGuiInputTextFlags_EnterReturnsTrue);
                    if(enter||ImGui::IsItemDeactivatedAfterEdit()){value=it->second;changed=finished=true;m_BulkText.erase(draftId);}
                    if(p.kind==K::Asset){std::string asset;if(AcceptAssetDrop(p.asset,asset)){value=asset;changed=finished=true;m_BulkText.erase(draftId);}}
                }else ImGui::TextDisabled(state.IsMixed()?"Mixed slots (single selection)":"Slots: edit with single selection");
                if(changed){m_PropertyDrafts[draftId]=value;commit(d,p,value,mask,finished);}
                if(finished){FinishTransaction(app);m_PropertyDrafts.erase(draftId);}
                ImGui::EndDisabled();if(readOnly&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip(drivenRotation||drivenLight?"Controlled by the sky/day-night settings.":"Read-only for multiple selection.");
                ImGui::PopID();
            }ImGui::EndTable();
        }ImGui::PopID();
    }
    if(m_Transaction&&m_Transaction->before.bulk&&!ImGui::IsAnyItemActive()){FinishTransaction(app);m_PropertyDrafts.clear();}
    ImGui::PopID();ImGui::EndDisabled();if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());
}
void EditorLayer::DrawInspector(Application& app){
    auto resource=[&](const auto& p,std::string& key){return DrawInspectorResource(p,key);};
    const auto start=std::chrono::steady_clock::now();
    struct Finish {Statistics& value;std::chrono::steady_clock::time_point start;std::uint64_t probes=ProjectContext::LinkProbeCount();~Finish(){value.inspectorMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();value.inspectorPathProbes=ProjectContext::LinkProbeCount()-probes;}} finish{m_Statistics,start};
    ImFont* font=nullptr;for(auto* candidate:ImGui::GetIO().Fonts->Fonts)if(std::strcmp(candidate->GetDebugName(),"Browser UI")==0){font=candidate;break;}
    if(font)ImGui::PushFont(font,0.f);
    const float dpi=ImGui::GetStyle().FontScaleDpi;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(6*dpi,5*dpi));
    struct StyleScope{bool font;~StyleScope(){ImGui::PopStyleVar();if(font)ImGui::PopFont();}} styleScope{font!=nullptr};
    ImGui::SetNextWindowSizeConstraints(ImVec2(270*dpi,180*dpi),ImVec2(FLT_MAX,FLT_MAX));
    if(!ImGui::Begin("Inspector",&m_ShowInspector)){ImGui::End();return;}
    auto& world=app.GetWorld();auto e=m_Selection.active;
    if(!world.IsAlive(e)){ImGui::TextUnformatted("No entity selected");ImGui::End();return;}
    ImGui::SetNextItemWidth(std::max(60.f,ImGui::GetContentRegionAvail().x-55*dpi));
    ImGui::InputTextWithHint("##PropertySearch","Search properties",&m_InspectorSearch);
    if(ImGui::IsItemActivated()){FinishTransaction(app);m_PropertyDrafts.clear();}
    ImGui::SameLine();if(ImGui::SmallButton("Clear"))m_InspectorSearch.clear();
    if(m_BulkSession&&m_BulkSession->selection!=m_Selection.items)FinishTransaction(app);
    if(m_Selection.items.size()>1){DrawBulkInspector(app);ImGui::End();return;}
    m_BulkUiSelection.clear();m_BulkText.clear();
    const auto* tag=world.GetComponent<ecs::TagComponent>(e);if(m_Selection.items.size()>1)ImGui::TextUnformatted("Active Entity:");ImGui::SeparatorText(tag?tag->name.c_str():"Entity");
    DrawCameraPreview(app,e);
    const nlohmann::json* prefabProperties=nullptr;bool prefabPlacement=false;
    if(auto root=scripting::PrefabRoot(world,e);root.IsValid()){
        const auto* sourceLink=world.GetComponent<ecs::PrefabInstanceComponent>(root);
        const auto cacheKey=(app.GetProject()?AssetPaths::ToUtf8String(app.GetProject()->descriptorPath):"legacy")+"|"+sourceLink->asset+"|"+std::to_string(m_AssetCatalog.Revision());
        if(cacheKey!=m_InspectorPrefabCacheKey){m_InspectorPrefabCacheKey=cacheKey;m_InspectorPrefabSource=nullptr;m_InspectorPrefabError.clear();try{m_InspectorPrefabSource=scripting::ReadPrefab(sourceLink->asset);}catch(const std::exception& failure){m_InspectorPrefabError=failure.what();}}
        if(!m_InspectorPrefabError.empty())ImGui::TextWrapped("Missing / invalid Prefab source: %s",m_InspectorPrefabError.c_str());
        else if(auto* identity=world.GetComponent<ecs::SceneIdentityComponent>(e)){
            for(auto member=sourceLink->members.begin();member!=sourceLink->members.end();++member)if(member.value().value("$entity",std::string())==identity->value)
                for(const auto& node:m_InspectorPrefabSource.at("entities"))if(node.at("id")==member.key()){prefabProperties=&node.at("components");break;}
        }
        prefabPlacement=e==root;
        if(ImGui::SmallButton("Refresh Source"))m_InspectorPrefabCacheKey.clear();
        auto* link=world.GetComponent<ecs::PrefabInstanceComponent>(root);ImGui::TextWrapped("Prefab: %s",link->asset.c_str());
        if(!app.IsPrefabEditing()&&!app.IsEditorPlayMode()){
            if(ImGui::Button("Open Prefab"))m_PrefabActionKey=link->asset;
            ImGui::SameLine();if(ImGui::Button("Apply...")){m_PrefabAction=3;m_PrefabActionEntity=root;m_PrefabConfirm=true;}
            ImGui::SameLine();if(ImGui::Button("Revert...")){m_PrefabAction=4;m_PrefabActionEntity=root;m_PrefabConfirm=true;}
        }
    }
    if(app.IsEditorPlayMode())ImGui::TextWrapped("Runtime edits are discarded on Stop.");
    if(auto parent=world.Parent(e);parent.IsValid()){auto* name=world.GetComponent<ecs::TagComponent>(parent);ImGui::Text("Parent: %s",name?name->name.c_str():"Entity");}
    if(ImGui::Checkbox("World coordinates",&m_InspectorWorldTransform)){FinishTransaction(app);m_PropertyDrafts.clear();}
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Transform fields use World coordinates when enabled, otherwise Local coordinates relative to Parent.");
    const auto& registry=ecs::ComponentRegistry::Builtins();ImGui::PushID(int(e.index));ImGui::PushID(int(e.generation));
    if(ImGui::Button("Add Component"))ImGui::OpenPopup("AddComponent");
    if(ImGui::BeginPopup("AddComponent")){for(const auto& d:registry.Components())if(d.addable&&!d.has(world,e))if(ImGui::MenuItem(d.label.c_str()))AddComponent(app,e,d.id);ImGui::EndPopup();}
    ImGui::PushItemWidth(std::max(90.f,ImGui::GetContentRegionAvail().x*.55f));
    for(const auto& d:registry.Components()){
        if(!d.has(world,e)||d.key=="SceneIdentity")continue;
        const bool componentMatch=inspector::Matches(d.label,m_InspectorSearch)||inspector::Matches(d.key,m_InspectorSearch);
        if(!componentMatch&&!std::any_of(d.properties.begin(),d.properties.end(),[&](const auto& p){return !p.internal&&(inspector::Matches(p.label,m_InspectorSearch)||inspector::Matches(p.key,m_InspectorSearch));})&&d.key!="LuaScript")continue;
        if(!m_InspectorSearch.empty())ImGui::SetNextItemOpen(true,ImGuiCond_Always);ImGui::PushID(d.key.c_str());
        bool open=ImGui::CollapsingHeader(d.label.c_str(),d.key=="Tag"||d.key=="Velocity"||d.key=="BoundsBounce"?ImGuiTreeNodeFlags_None:ImGuiTreeNodeFlags_DefaultOpen);
        if(ImGui::BeginPopupContextItem("ComponentActions")){if(ImGui::MenuItem("Reset",nullptr,false,d.removable))ResetComponent(app,e,d.id);if(ImGui::MenuItem("Remove",nullptr,false,d.removable)){RemoveComponent(app,e,d.id);ImGui::EndPopup();ImGui::PopID();continue;}ImGui::EndPopup();}
        if(open){auto drawProperty=[&](const ecs::PropertyDescriptor& p){
            if(p.internal||(!componentMatch&&!inspector::Matches(p.label,m_InspectorSearch)&&!inspector::Matches(p.key,m_InspectorSearch)))return;
            std::string propertyLabel=d.key=="Transform"&&p.key=="rotation"?"Rotation (deg)":p.label;
            std::string propertyTip=p.tooltip;
            if(prefabProperties&&prefabProperties->contains(d.key)&&d.key!="PrefabInstance"){
                const auto& source=prefabProperties->at(d.key);
                if(d.key=="Transform"&&prefabPlacement){propertyTip+="\nInstance placement is retained by Prefab Revert.";}
                else if(source.contains(p.key)){auto current=PropertyJson(p.get(world,e));if(p.kind==ecs::PropertyKind::Color&&source.at(p.key).is_array()&&source.at(p.key).size()==3&&current.is_array()&&current.size()==4)current.erase(current.begin()+3);const bool overridden=source.at(p.key)!=current;propertyLabel+=overridden?" *":"";propertyTip+=overridden?"\nLocal override differs from Prefab source. Revert uses the existing instance operation.":"\nInherited: matches Prefab source.";}
            }
            inspector::PropertyRow(propertyLabel.c_str(),propertyTip.c_str());

            ImGui::PushID(p.key.c_str());const auto id=ImGui::GetID("PropertyDraft");auto draft=m_PropertyDrafts.find(id);
            auto value=draft!=m_PropertyDrafts.end()?draft->second:p.get(world,e);
            if(draft==m_PropertyDrafts.end()&&m_InspectorWorldTransform&&d.key=="Transform"){auto t=world.WorldTransform(e);if(p.key=="position")value=t.position;else if(p.key=="rotation")value=t.rotation;else if(p.key=="scale")value=t.scale;}
            const bool skyDriven=d.key=="Transform"&&p.key=="rotation"&&app.GetSceneBackground().sunSync==SunSyncMode::SkyDrivesLight&&app.GetSceneBackground().sunMetadata&&world.HasComponent<ecs::DirectionalLightComponent>(e)&&PrimarySun(world)==e;
            const bool unsupportedShadow=(d.key=="PointLight"||d.key=="SpotLight")&&p.key=="castShadows";
            const bool readOnly=p.readOnly||skyDriven||unsupportedShadow;
            ImGui::BeginDisabled(readOnly);bool changed=Drawer(p,value,d.key=="Transform"?TransformFormat(m_TransformPrecision):"%.6g",d.key=="Transform"&&p.key=="rotation",resource);bool activated=ImGui::IsItemActivated();bool deactivated=ImGui::IsItemDeactivatedAfterEdit();ImGui::EndDisabled();
            if(unsupportedShadow&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Unsupported: only the primary Directional light has a shadow pass. The reserved stored flag is preserved.");else if(skyDriven&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Sky Drives Light controls this orientation. Select Independent to edit it manually.");else if(p.readOnly&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Readonly: %s",p.tooltip.c_str());else if(ImGui::IsItemHovered()&&!p.tooltip.empty())ImGui::SetTooltip("%s",p.tooltip.c_str());
            if(activated||changed){if(!m_Transaction)PushUndoBeforeChange(app,"Edit "+d.label+" / "+p.label);m_PropertyDrafts[id]=value;}
            if(changed){
                bool applied=false;
                if(m_InspectorWorldTransform&&d.key=="Transform"){auto target=world.WorldTransform(e);if(p.key=="position")target.position=std::get<ecs::Vec3>(value);else if(p.key=="rotation")target.rotation=std::get<ecs::Vec3>(value);else if(p.key=="scale")target.scale=std::get<ecs::Vec3>(value);else if(p.key=="pivot")target=ecs::RebasePivot(target,std::get<ecs::Vec3>(value));applied=world.SetWorldTransform(e,target,&m_EditorError);if(applied)app.NotifyEditorMutation(e);}
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
            if(inspector::BeginProperties("Properties")){
                for(const auto& p:d.properties)if(p.section==section&&p.advanced==advanced)drawProperty(p);ImGui::EndTable();
            }
        };
        if(d.key=="Material"){
            for(const char* section:{"Surface","Base Color","Metallic / Roughness","Normal Map","AO","Rendering","Advanced"}){
                bool primary=std::string(section)=="Surface"||std::string(section)=="Base Color"||std::string(section)=="Metallic / Roughness";
                if(!m_InspectorSearch.empty())ImGui::SetNextItemOpen(true,ImGuiCond_Always);
                if(ImGui::TreeNodeEx(section,primary?ImGuiTreeNodeFlags_DefaultOpen:0)){drawSection(section,false);ImGui::TreePop();}
            }
        }else if(d.key=="PostProcessVolume"){
            ImGui::TextWrapped("Unchecked parameters inherit the current scene/volume stack. Half extents use World Transform; blend radius is outside the box in world units.");
            for(const char* section:{"Volume","Fog","Height Fog","Volumetric Fog","Environment","Atmosphere","Color Grading","Vignette","Film Grain","Chromatic Aberration"})if(ImGui::TreeNodeEx(section,ImGuiTreeNodeFlags_DefaultOpen)){drawSection(section,false);ImGui::TreePop();}
            DrawCustomPostFxInspector(app,e);
        }else if(d.key=="UICanvas"){drawSection("",false);DrawUICanvasInspector(app,e);
        }else if(d.key=="Fog"){
            drawSection("",false);auto mode=world.GetComponent<ecs::FogComponent>(e)->mode;
            if(mode!=FogMode::Distance){ImGui::SeparatorText("Height profile (world space)");drawSection("Medium",false);}
            ImGui::SeparatorText(mode==FogMode::Distance?"Distance":mode==FogMode::Height?"Height":"Volumetric");
            drawSection(mode==FogMode::Distance?"Distance":mode==FogMode::Height?"Height":"Volumetric",false);
            if(mode==FogMode::Volumetric){const auto& f=*world.GetComponent<ecs::FogComponent>(e);const float sigma=f.density*f.extinction;ImGui::TextWrapped("Extinction is per world unit. At/below Base Height, transmittance: %.1f%% over 1 unit, %.3f%% over 10 units. Above Base Height the height profile reduces absorption.",100*std::exp(-sigma),100*std::exp(-sigma*10));}
            const auto fog=ecs::GatherFog(world);if(fog.activeCount>1)ImGui::TextWrapped("Conflict: %u enabled Fog components. Lowest Entity index wins.",fog.activeCount);
            if(fog.invalidCount)ImGui::TextWrapped("Invalid Fog components ignored: %u",fog.invalidCount);
            ImGui::TextWrapped("Global environment; Transform is ignored. Colors are linear. Height density is constant below Base Height and decreases above it.");
            if(auto* renderer=app.GetRenderAdapter()){auto stats=renderer->GetFogDiagnostics();if(!stats.error.empty())ImGui::TextWrapped("%s",stats.error.c_str());ImGui::Text("Fog: %u steps, CPU %.3f / GPU %.3f ms, %.2f MiB",stats.samples,stats.cpuMs,stats.gpuMs,double(stats.gpuBytes)/(1024*1024));}
        }else{
            std::vector<std::string> groups;for(const auto& p:d.properties)if(!p.internal){auto group=PropertyGroup(d,p);if(std::find(groups.begin(),groups.end(),group)==groups.end())groups.push_back(group);}
            for(const auto& group:groups){
                bool hasMatch=componentMatch||std::any_of(d.properties.begin(),d.properties.end(),[&](const auto& p){return PropertyGroup(d,p)==group&&(inspector::Matches(p.label,m_InspectorSearch)||inspector::Matches(p.key,m_InspectorSearch));});if(!hasMatch)continue;
                const bool advanced=group=="Advanced";if(advanced&&!m_InspectorSearch.empty())ImGui::SetNextItemOpen(true,ImGuiCond_Always);
                if(advanced&&!ImGui::TreeNode("Advanced"))continue;
                if(!group.empty()&&!advanced)inspector::Section(group.c_str());
                const bool unusedLegacy=d.key=="PlayerStart"&&group=="Legacy (no Player Prefab)"&&world.GetComponent<ecs::PlayerStartComponent>(e)->spawnMode!=1&&!world.GetComponent<ecs::PlayerStartComponent>(e)->playerPrefab.empty();
                if(unusedLegacy)ImGui::TextDisabled("Unused while Player Prefab is assigned.");
                ImGui::BeginDisabled(unusedLegacy);
                if(inspector::BeginProperties("Properties")){for(const auto& p:d.properties)if(PropertyGroup(d,p)==group){if(d.key=="PlayerStart"){const auto& start=*world.GetComponent<ecs::PlayerStartComponent>(e);const bool legacy=start.spawnMode==1||(start.spawnMode==0&&start.playerPrefab.empty());if(p.key=="playerPrefab"&&(start.spawnMode==1||start.spawnMode==3))continue;if(p.section=="Legacy (no Player Prefab)"&&!legacy)continue;}drawProperty(p);}ImGui::EndTable();}
                ImGui::EndDisabled();
                if(advanced)ImGui::TreePop();
            }
        }
        if(d.key=="LuaScript"){
            auto* script=world.GetComponent<ecs::LuaScriptComponent>(e);
            auto& runtime=app.GetLuaRuntime();
            if(ImGui::Button("Reload Script"))runtime.ReloadScript(script->scriptPath);
            ImGui::SameLine();ImGui::BeginDisabled(script->scriptPath.empty());
            if(ImGui::Button("Open Script"))m_ScriptKey=script->scriptPath; // Deferred until Inspector releases World/component references.
            ImGui::EndDisabled();
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Open project script in Script Document: %s",script->scriptPath.empty()?"Assign a script first":script->scriptPath.c_str());
            auto schema=runtime.Fields(script->scriptPath);
            auto error=runtime.Error(script->scriptPath);
            if(error.empty())ImGui::TextDisabled("Loaded: %zu fields",schema.size());else ImGui::TextWrapped("%s",error.c_str());
            for(auto field=schema.begin();field!=schema.end();++field){
                if(!inspector::Matches(d.label,m_InspectorSearch)&&!inspector::Matches(field.key(),m_InspectorSearch)&&!inspector::Matches(field.value().value("label",field.key()),m_InspectorSearch))continue;
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
                    if(!inspector::BeginProperties("LuaProperties")){ImGui::PopID();continue;}
                    inspector::PropertyRow(p.label.c_str());bool changed=Drawer(p,v,"%.6g",false,resource);
                    if(changed){
                        PushUndoBeforeChange(app,"Edit Lua / "+p.label);
                        if(type=="bool")value=std::get<bool>(v);else if(type=="integer")value=std::get<int>(v);else if(type=="number")value=std::get<float>(v);else if(type=="vec3"){auto x=std::get<ecs::Vec3>(v);value=nlohmann::json::array({x.x,x.y,x.z});}else value=std::get<std::string>(v);
                        script->properties[field.key()]=value;app.NotifyEditorMutation(e);
                    }
                    if(ImGui::IsItemDeactivatedAfterEdit()||(changed&&!ImGui::IsAnyItemActive()))FinishTransaction(app);
                    ImGui::EndTable();
                }catch(const std::exception&){ImGui::TextWrapped("Override incompatible with schema: %s",field.key().c_str());if(ImGui::SmallButton("Use default")){PushUndoBeforeChange(app,"Reset Lua field");script->properties.erase(field.key());app.NotifyEditorMutation(e);FinishTransaction(app);}}
                ImGui::PopID();
            }
        }
        if(d.key=="Transform"&&ImGui::TreeNode("Transform Options")){
            ImGui::SetNextItemWidth(-1);ImGui::Combo("Precision",&m_TransformPrecision,"Compact\0Precise\0Full Precision (9 significant digits)\0");
            ImGui::Checkbox("Rotation Snap",&m_RotationSnapping);ImGui::SameLine();
            if(ImGui::BeginCombo("Step",std::to_string(m_SnapRotation).c_str())){for(float step:{15.f,30.f,45.f,90.f})if(ImGui::Selectable((std::to_string(int(step))+" deg").c_str(),m_SnapRotation==step))m_SnapRotation=step;ImGui::EndCombo();}
            PrecisionDragFloat("Custom step (deg)",&m_SnapRotation,.1f,.1f,180);
            ImGui::Combo("Quarter-turn axis",&m_RotateAxis,"X\0Y\0Z\0");
            ImGui::BeginDisabled(app.IsEditorPlayMode()||app.GetSceneBackground().sunSync==SunSyncMode::SkyDrivesLight&&PrimarySun(world)==e);
            if(ImGui::Button("-90 deg"))RotateSelectedQuarter(app,m_RotateAxis,-1,m_GizmoMode==1);ImGui::SameLine();if(ImGui::Button("+90 deg"))RotateSelectedQuarter(app,m_RotateAxis,1,m_GizmoMode==1);ImGui::EndDisabled();
            ImGui::TextWrapped("Rz*Ry*Rx; Local: R*Q, World: Q*R. Euler gimbal lock remains.");ImGui::TreePop();
        }
        if(d.key=="CameraAttachment")ImGui::TextWrapped("Play-only visual attachment to the final camera. Descendant meshes follow; physics, raycasts and collider debug shapes keep their ECS Transform. Edit Mode shows the authored pose.");
        if(d.key=="PlayerStart"){
            if(world.GetComponent<ecs::PlayerStartComponent>(e)->spawnMode==3)ImGui::TextWrapped("GameMode Default: resolves mode prefab, then project fallback before Lua starts. Requires an enabled marker and Auto Spawn.");
            else if(world.GetComponent<ecs::PlayerStartComponent>(e)->spawnMode!=1&&!world.GetComponent<ecs::PlayerStartComponent>(e)->playerPrefab.empty())ImGui::TextWrapped("Prefab workflow: the prefab defines components, controller and camera. World position replaces root placement; full marker rotation precedes prefab rotation. Prefab scale/pivot are retained; marker scale is ignored. Legacy settings below are unused.");
            else{
            auto half=ecs::PlayerBodyHalfExtents(*world.GetComponent<ecs::PlayerStartComponent>(e));
            ImGui::TextWrapped("Position marks the feet. Player size: %.2f x %.2f x %.2f. Scale is ignored. Y rotation sets facing; X sets camera pitch. The body stays upright.",half.x*2,half.y*2,half.z*2);
            }
        }
        if(d.key=="PointLight"||d.key=="SpotLight")ImGui::TextWrapped("Point/Spot shadows are a reserved flag. Range is in world units; Transform scale is ignored.");
        if(d.key=="DirectionalLight"||d.key=="SpotLight")ImGui::TextWrapped("Outgoing rays follow Transform local +Z; Inspector rotation is in degrees (stored radians).");
        if(d.key=="Material"){if(ImGui::Button("Open Material Asset Editor")){auto* material=world.GetComponent<ecs::MaterialComponent>(e);OpenDocument(app,material->materialPath);}}
        }
        ImGui::PopID();
    }
    if(m_Transaction&&!ImGui::IsAnyItemActive()){FinishTransaction(app);m_PropertyDrafts.clear();}
    ImGui::PopItemWidth();if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::PopID();ImGui::PopID();ImGui::End();
}
}
