#include "EditorLayer.h"
#include "EditorWidgets.h"
#include "PlacementMath.h"
#include "LightGizmoMath.h"
#include "../core/Application.h"
#include "../resources/ResourceManager.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>
namespace editor {
namespace {
using V=ecs::Vec3;
float Dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V Add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V Scale(V a,float s){return {a.x*s,a.y*s,a.z*s};}
PlacementBounds Bounds(const ResourceHandle<MeshResource>& resource){
    struct Cached{std::weak_ptr<Resource<MeshResource>> owner;std::uint64_t version=0;PlacementBounds bounds;};
    static thread_local std::unordered_map<const void*,Cached> cache;
    if(cache.size()>256)cache.clear();auto& item=cache[resource.get()];
    if(item.owner.lock()!=resource||item.version!=resource->GetVersion()){item.owner=resource;item.version=resource->GetVersion();item.bounds=MeshPlacementBounds(resource->GetData().meshData);}return item.bounds;
}
}
const EditorLayer::PlacementPreview& EditorLayer::UpdatePlacementPreview(Application& app,const std::string& key,float u,float v){
    m_PlacementPreview={};auto& preview=m_PlacementPreview;preview.active=true;preview.key=key;
    if(app.IsEditorPlayMode()){preview.status="Stop Play to place models";return preview;}
    if(!std::isfinite(u)||!std::isfinite(v)||key.empty()||!ecs::ComponentRegistry::ValidateAssetKey(key,ecs::AssetKind::Mesh)){preview.status="Invalid model asset";return preview;}
    auto* resources=app.GetResourceManager();if(!resources){preview.status="Resource manager unavailable";return preview;}
    auto resource=resources->Get<MeshResource>(std::filesystem::u8path(key));
    if(!resource){(void)resources->LoadAsync<MeshResource>(std::filesystem::u8path(key));preview.status="Loading model...";return preview;}
    if(resource->IsLoading()){preview.status="Loading model...";return preview;}
    if(resource->IsFailed()){preview.status=resource->GetErrorMessage();return preview;}
    const auto& mesh=resource->GetData();
    if(!resource->IsLoaded()||mesh.placeholder||mesh.meshData.vertices.empty()||mesh.meshData.indices.empty()){preview.status="Model has no usable geometry";return preview;}
    const auto eye=app.GetCameraPosition();const auto camera=GizmoCamera(eye,app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),float(std::max(1,m_ViewportPixelWidth))/std::max(1,m_ViewportPixelHeight),app.GetCameraNearPlane(),app.GetCameraFarPlane());
    float x=(std::clamp(u,0.f,1.f)*2-1)/camera.projection[0],y=(1-std::clamp(v,0.f,1.f)*2)/camera.projection[5];
    V ray{camera.view[0]*x+camera.view[1]*y+camera.view[2],camera.view[4]*x+camera.view[5]*y+camera.view[6],camera.view[8]*x+camera.view[9]*y+camera.view[10]};ray=Scale(ray,1/std::sqrt(Dot(ray,ray)));
    auto hit=m_SurfaceSnap?RaycastPlacement(app,eye,ray,app.GetCameraFarPlane()):std::nullopt;
    V normal{0,1,0},contact;bool gridApplied=false;
    if(hit){
        preview.surface=true;normal=hit->normal;contact=hit->position;preview.status="Surface: exact Mesh triangles";
        if(m_Snapping){
            V snapped{std::round(contact.x/m_SnapTranslation)*m_SnapTranslation,std::round(contact.y/m_SnapTranslation)*m_SnapTranslation,std::round(contact.z/m_SnapTranslation)*m_SnapTranslation};
            snapped=Add(snapped,Scale(normal,Dot(normal,Add(contact,Scale(snapped,-1)))));
            // Keep the snapped contact on real triangles, including small/edge faces.
            auto snappedHit=RaycastPlacement(app,Add(snapped,Scale(normal,.01f)),Scale(normal,-1),.02f);
            if(snappedHit){contact=snappedHit->position;normal=snappedHit->normal;gridApplied=true;}
            else preview.status+=" (grid point outside surface; contact retained)";
        }
    }else{
        float plane=std::abs(ray.y)>1e-6f?-eye.y/ray.y:-1;
        const bool ground=plane>app.GetCameraNearPlane()&&plane<app.GetCameraFarPlane();
        const float distance=ground?plane:std::min(10.f,app.GetCameraFarPlane()*.5f);contact=Add(eye,Scale(ray,distance));
        preview.status=ground?"Ground plane Y=0 (no Mesh surface)":"Camera distance (no Mesh surface)";
        if(m_Snapping){contact={std::round(contact.x/m_SnapTranslation)*m_SnapTranslation,std::round(contact.y/m_SnapTranslation)*m_SnapTranslation,std::round(contact.z/m_SnapTranslation)*m_SnapTranslation};gridApplied=true;}
    }
    if(gridApplied)preview.status+=" / Grid Snap";
    preview.transform=ContactPlacement(Bounds(resource),{},contact,normal,m_AlignToNormal&&preview.surface);
    preview.ready=ValidGroupSource(preview.transform);if(!preview.ready)preview.status="Placement Transform exceeds editor limits";
    return preview;
}
ecs::Entity EditorLayer::ConfirmPlacement(Application& app){
    auto preview=m_PlacementPreview;if(app.IsEditorPlayMode()||!preview.active||!preview.ready)return {};
    auto* resources=app.GetResourceManager();auto resource=resources?resources->Get<MeshResource>(std::filesystem::u8path(preview.key)):nullptr;
    if(!resource||!resource->IsLoaded()||resource->GetData().placeholder){m_EditorError="Model is not ready; wait for the preview before dropping";CancelPlacement();return {};}
    auto e=PlaceModel(app,preview.key,preview.transform.position);
    if(e.IsValid()){*app.GetWorld().GetComponent<ecs::TransformComponent>(e)=preview.transform;app.NotifyEditorMutation(e);}
    CancelPlacement();return e;
}
bool EditorLayer::DropSelectionToFloor(Application& app){
    if(app.IsEditorPlayMode())return false;PruneSelection(app);EndGroupTransform(app);FinishTransaction(app);
    auto* resources=app.GetResourceManager();if(!resources||m_Selection.items.empty())return false;
    EntitySnapshot before;before.world=app.GetWorld().GetIdentity();before.selection=m_Selection;
    struct Change{ecs::Entity entity;ecs::TransformComponent target;};std::vector<Change> changes;unsigned loading=0;
    for(auto e:m_Selection.items)if(auto* t=app.GetWorld().GetComponent<ecs::TransformComponent>(e)){
        PlacementBounds bounds;
        if(auto* renderer=app.GetWorld().GetComponent<ecs::MeshRendererComponent>(e)){
            auto mesh=resources->Get<MeshResource>(renderer->meshPath);
            if(!mesh){(void)resources->LoadAsync<MeshResource>(renderer->meshPath);++loading;continue;}
            if(!mesh->IsLoaded()||mesh->GetData().placeholder){++loading;continue;}bounds=Bounds(mesh);
        }
        V lo{INFINITY,INFINITY,INFINITY},hi{-INFINITY,-INFINITY,-INFINITY};
        for(auto p:PlacementCorners(bounds,*t)){lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);}
        auto hit=RaycastPlacement(app,{(lo.x+hi.x)*.5f,lo.y+.001f,(lo.z+hi.z)*.5f},{0,-1,0},2000001.f,m_Selection.items);
        if(!hit||hit->normal.y<.05f)continue;
        float support=INFINITY;for(auto p:PlacementCorners(bounds,*t))support=std::min(support,Dot(hit->normal,Add(p,Scale(hit->position,-1))));
        const float drop=-support/hit->normal.y;if(drop>=-.0001f)continue;
        auto target=*t;target.position.y+=drop;
        if(!ValidGroupSource(target))continue;
        EntitySnapshot item;item.entity=e;item.world=e.world;item.alive=true;item.transform=*t;before.related.push_back(std::move(item));changes.push_back({e,target});
    }
    m_EditorError=loading?std::to_string(loading)+" model bounds unavailable; skipped. Wait for loading and retry.":"";
    if(changes.empty())return false;
    for(auto& change:changes){*app.GetWorld().GetComponent<ecs::TransformComponent>(change.entity)=change.target;app.NotifyEditorMutation(change.entity);}
    CommitUndo(app,"Drop to Floor",std::move(before));return true;
}
bool EditorLayer::DrawPlacementMenu(Application& app){
    if(ImGui::Button("Placement"))ImGui::OpenPopup("PlacementTools");bool owns=ImGui::IsItemHovered()||ImGui::IsPopupOpen("PlacementTools");
    if(ImGui::BeginPopup("PlacementTools")){
        ImGui::BeginDisabled(app.IsEditorPlayMode());ImGui::Checkbox("Surface Snap",&m_SurfaceSnap);
        ImGui::BeginDisabled(!m_SurfaceSnap);ImGui::Checkbox("Align to Normal",&m_AlignToNormal);ImGui::EndDisabled();
        ImGui::Checkbox("Grid Snap",&m_Snapping);ImGui::SetNextItemWidth(130);ImGui::DragFloat("Grid step",&m_SnapTranslation,.05f,.001f,1000,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::BeginDisabled(m_Selection.items.empty());if(ImGui::MenuItem("Drop to Floor"))DropSelectionToFloor(app);ImGui::EndDisabled();ImGui::EndDisabled();
        ImGui::TextDisabled("Surface queries use exact Mesh triangles.");ImGui::TextDisabled("No hit: ground Y=0, then 10 units from camera.");ImGui::TextDisabled("Drop without Mesh: transformed unit-cube bounds.");
        ImGui::EndPopup();
    }return owns;
}
void EditorLayer::DrawPlacementTarget(Application& app,const ImVec2& origin,const ImVec2& size){
    const auto* dragged=ImGui::GetDragDropPayload();
    if(dragged&&dragged->IsDataType("WHISP_ASSET")&&ImGui::IsKeyPressed(ImGuiKey_Escape,false)){m_PlacementCancelled=true;m_EscapeConsumed=true;}
    if(m_PlacementCancelled){ImGui::ClearDragDrop();CancelPlacement();if(!ImGui::IsMouseDown(0))m_PlacementCancelled=false;return;}
    CancelPlacement();
    if(app.IsEditorPlayMode()||m_ModalActive||m_ToolActive||m_LightPointerCaptured||m_PlaneScaleCaptured||app.IsCameraControlActive()||ImGui::IsMouseDown(1))return;
    if(ImGui::BeginDragDropTarget()){
        if(const auto* payload=ImGui::AcceptDragDropPayload("WHISP_ASSET",ImGuiDragDropFlags_AcceptBeforeDelivery))if(payload->DataSize==sizeof(AssetPayload)){
            const auto& asset=*static_cast<const AssetPayload*>(payload->Data);
            if(std::memchr(asset.key,0,sizeof(asset.key))){
                const auto mouse=ImGui::GetMousePos();float u=(mouse.x-origin.x)/size.x,v=(mouse.y-origin.y)/size.y;
                if(asset.kind==ecs::AssetKind::Mesh){
                    UpdatePlacementPreview(app,asset.key,u,v);
                    auto status=m_PlacementPreview.status;
                    if(payload->IsDelivery()){
                        if(!m_PlacementPreview.ready)m_EditorError=status;
                        else ConfirmPlacement(app);
                    }else{
                        auto* draw=ImGui::GetWindowDrawList();const ImVec2 p{origin.x+10,origin.y+size.y-30};
                        draw->AddRectFilled({p.x-4,p.y-3},{std::min(origin.x+size.x-5,p.x+ImGui::CalcTextSize(status.c_str()).x+5),p.y+ImGui::GetTextLineHeight()+4},IM_COL32(12,18,24,230));
                        draw->AddText(p,m_PlacementPreview.ready?IM_COL32(80,235,255,255):IM_COL32(255,190,80,255),status.c_str());
                    }
                }else if(asset.kind==ecs::AssetKind::None&&std::string(asset.key).ends_with(".wscene")&&payload->IsDelivery())PlaceImportedScene(app,asset.key,PlacementPoint(app,u,v));
            }
        }ImGui::EndDragDropTarget();
    }
}
}
