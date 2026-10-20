#include "EditorLayer.h"
#include "EditorWidgets.h"
#include "LightGizmoMath.h"
#include "../core/Application.h"
#include "../ecs/PostProcessEvaluation.h"
#include <imgui.h>
#include "../resources/ResourceManager.h"
#include "../render/IRenderAdapter.h"
namespace editor {
ecs::Entity EditorLayer::CreatePostProcessVolume(Application& app,bool global){
    if(app.IsEditorPlayMode())return {};FinishTransaction(app);auto previous=m_Selection.active;
    auto entity=app.CreateEmptyEntity(global?"Global Post Process Volume":"Local Post Process Volume");
    auto& volume=app.GetWorld().AddComponent<ecs::PostProcessVolumeComponent>(entity);volume.global=global;
    // Minimal working starting point: existing Distance Fog, not a second fog implementation.
    volume.fogEnabled=true;volume.fogDensity=.65f;volume.fogStart=2.f;volume.fogEnd=30.f;
    SelectEntityFromUser(app,entity);EntitySnapshot before;before.entity=entity;before.world=entity.world;before.previousSelection=previous;CommitUndo(app,"Create Post Process Volume",before);return entity;
}
void EditorLayer::DrawPostProcessGizmos(Application& app,const ImVec2& origin,const ImVec2& size){
    if(app.IsEditorPlayMode()||m_GameView||!m_VisualSettings.enabled||size.x<=1||size.y<=1)return;
    auto& world=app.GetWorld();auto camera=GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),size.x/size.y,app.GetCameraNearPlane(),app.GetCameraFarPlane());
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
    for(auto entity:m_Selection.items){const auto* volume=world.GetComponent<ecs::PostProcessVolumeComponent>(entity);if(!volume||volume->global||!EditorEntityVisible(app,entity))continue;auto box=ecs::GetPostProcessBox(world,entity,*volume);if(!box)continue;
        std::array<ecs::Vec3,8> points;for(int corner=0;corner<8;++corner){points[corner]=box->center;for(int axis=0;axis<3;++axis){float sign=corner&(1<<axis)?1.f:-1.f;const auto& v=box->axes[axis];points[corner].x+=v.x*box->half[axis]*sign;points[corner].y+=v.y*box->half[axis]*sign;points[corner].z+=v.z*box->half[axis]*sign;}}
        for(int corner=0;corner<8;++corner)for(int axis=0;axis<3;++axis)if(!(corner&(1<<axis))){auto a=ProjectGizmo(points[corner],camera),b=ProjectGizmo(points[corner|(1<<axis)],camera);if(!a||!b)continue;
            draw->AddLine({origin.x+(*a)[0]*size.x,origin.y+(*a)[1]*size.y},{origin.x+(*b)[0]*size.x,origin.y+(*b)[1]*size.y},volume->enabled?IM_COL32(240,190,70,225):IM_COL32(130,130,130,180),1.5f);
        }
    }
    draw->PopClipRect();
}
}

namespace editor {
bool EditorLayer::AddCustomPostFx(Application& app,ecs::Entity entity,const std::string& key){
    if(app.IsEditorPlayMode()||!app.GetWorld().IsAlive(entity))return false;auto* volume=app.GetWorld().GetComponent<ecs::PostProcessVolumeComponent>(entity);if(!volume){m_EditorError="Select a Post Process Volume";return false;}
    auto* resources=app.GetResourceManager();if(!resources)return false;auto resource=resources->Get<PostFxResource>(key);if(!resource)resource=resources->Load<PostFxResource>(key);auto d=resource->GetData().definition;
    if(!d||resources->PostFxDuplicate(d->id)){m_EditorError=d?"Duplicate effect ID":resource->GetErrorMessage();return false;}
    if(volume->customEffects.size()>=32||std::any_of(volume->customEffects.begin(),volume->customEffects.end(),[&](auto& e){return e.id==d->id;})){m_EditorError="Effect already present or Volume effect limit reached";return false;}
    FinishTransaction(app);auto before=CaptureSelectedEntity(app,entity);volume->customEffects.push_back({key,d->id,true,1});app.NotifyEditorMutation(entity);CommitUndo(app,"Add Custom PostFX",std::move(before));resources->WatchForHotReload<PostFxResource>(key);return true;
}
void EditorLayer::DrawCustomPostFxInspector(Application& app,ecs::Entity entity){
    if(!ImGui::TreeNodeEx("Custom PostFX",ImGuiTreeNodeFlags_DefaultOpen))return;
    auto* resources=app.GetResourceManager();auto* volume=app.GetWorld().GetComponent<ecs::PostProcessVolumeComponent>(entity);if(!resources||!volume){ImGui::TreePop();return;}
    ImGui::BeginDisabled(app.IsEditorPlayMode());
    if(ImGui::Button("Refresh Effects"))resources->PostFxAssets(true);ImGui::SameLine();
    if(ImGui::BeginCombo("Add Effect","Select .postfx.json")){for(auto& key:resources->PostFxAssets()){auto r=resources->Get<PostFxResource>(key);auto d=r?r->GetData().definition:nullptr;if(ImGui::Selectable((d?d->name+"  ("+d->id+")":key).c_str()))AddCustomPostFx(app,entity,key);}ImGui::EndCombo();}
    std::string droppedEffect;if(AcceptAssetDrop(ecs::AssetKind::PostFx,droppedEffect))AddCustomPostFx(app,entity,droppedEffect);
    for(size_t i=0;i<volume->customEffects.size();++i){ImGui::PushID(int(i));auto edit=volume->customEffects[i];bool changed=false,remove=false;
        if(ImGui::TreeNodeEx("Effect",ImGuiTreeNodeFlags_DefaultOpen,"%s",edit.id.c_str())){
            ImGui::TextWrapped("%s",edit.resource.c_str());auto resource=resources->Get<PostFxResource>(edit.resource);if(!resource)resource=resources->Load<PostFxResource>(edit.resource);auto d=resource->GetData().definition;
            if(d){ImGui::TextWrapped("HLSL: %s",d->shader.c_str());if(!d->description.empty())ImGui::TextWrapped("%s",d->description.c_str());}
            if(resource->HasError())ImGui::TextWrapped("Resource error: %s",resource->GetErrorMessage().c_str());
            if(resources->PostFxDuplicate(edit.id))ImGui::TextWrapped("Duplicate effect ID");if(d&&d->id!=edit.id)ImGui::TextWrapped("Saved Effect ID does not match manifest");
            auto status=app.GetRenderAdapter()?app.GetRenderAdapter()->CustomPostFxStatus(edit.resource):"No renderer";ImGui::TextWrapped("GPU: %s",status.c_str());
            if(ImGui::Button("Reload Effect"))resources->ReloadPostFx(edit.resource);ImGui::SameLine();remove=ImGui::Button("Remove Effect");
            changed|=ImGui::Checkbox("Enabled",&edit.enabled);changed|=ImGui::SliderFloat("Weight",&edit.weight,0,1);
            if(d)for(auto& p:d->parameters){ImGui::PushID(p.name.c_str());bool overridden=edit.parameters.contains(p.name);if(ImGui::Checkbox("##Override",&overridden)){if(overridden)edit.parameters[p.name]=p.initial;else edit.parameters.erase(p.name);changed=true;}ImGui::SameLine();
                if(!overridden)ImGui::TextDisabled("%s (inherit)",p.name.c_str());else if(!p.Valid(edit.parameters[p.name])){ImGui::TextWrapped("%s: saved value incompatible; preserved",p.name.c_str());if(ImGui::SmallButton("Use default")){edit.parameters[p.name]=p.initial;changed=true;}}
                else {auto value=edit.parameters[p.name];bool parameterChanged=false;
                    if(p.type=="bool"){bool v=value.get<bool>();parameterChanged=ImGui::Checkbox(p.name.c_str(),&v);if(parameterChanged)value=v;}
                    else if(p.type=="int"){int v=value.get<int>();parameterChanged=ImGui::SliderInt(p.name.c_str(),&v,int(p.minimum),int(p.maximum));if(parameterChanged)value=v;}
                    else {float v[4]{};for(unsigned c=0;c<p.Components();++c)v[c]=p.Components()==1?value.get<float>():value[c].get<float>();parameterChanged=ImGui::SliderScalarN(p.name.c_str(),ImGuiDataType_Float,v,int(p.Components()),&p.minimum,&p.maximum,"%.3f");if(parameterChanged){if(p.Components()==1)value=v[0];else {value=nlohmann::json::array();for(unsigned c=0;c<p.Components();++c)value.push_back(v[c]);}}}
                    if(parameterChanged){edit.parameters[p.name]=value;changed=true;}
                }ImGui::PopID();
            }
            ImGui::TreePop();
        }
        if(changed||remove){PushUndoBeforeChange(app,"Edit Custom PostFX");if(remove)volume->customEffects.erase(volume->customEffects.begin()+i);else volume->customEffects[i]=std::move(edit);app.NotifyEditorMutation(entity);if(remove||!ImGui::IsAnyItemActive())FinishTransaction(app);}
        if(ImGui::IsItemDeactivatedAfterEdit())FinishTransaction(app);ImGui::PopID();if(remove)break;
    }
    ImGui::EndDisabled();ImGui::TreePop();
}
}
