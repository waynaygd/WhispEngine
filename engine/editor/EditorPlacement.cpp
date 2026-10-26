#include "EditorLayer.h"
#include "EditorWidgets.h"
#include "PlacementMath.h"
#include "LightGizmoMath.h"
#include "../core/Application.h"
#include "../resources/ResourceManager.h"
#include "../scripting/Prefab.h"
#include "../scene/SceneSerializer.h"
#include <imgui.h>
#include <ImGuizmo.h>
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
    if(key.ends_with(".prefab.json"))return UpdatePrefabPlacementPreview(app,key,u,v);
    m_PrefabPlacement.reset();m_PlacementPreview={};auto& preview=m_PlacementPreview;preview.active=true;preview.key=key;
    if(app.IsEditorPlayMode()){preview.status="Stop Play to place models";return preview;}
    if(!std::isfinite(u)||!std::isfinite(v)||key.empty()||!ecs::ComponentRegistry::ValidateAssetKey(key,ecs::AssetKind::Mesh)){preview.status="Invalid model asset";return preview;}
    auto* resources=app.GetResourceManager();if(!resources){preview.status="Resource manager unavailable";return preview;}
    auto resource=resources->Get<MeshResource>(std::filesystem::u8path(key));
    if(!resource){(void)resources->LoadAsync<MeshResource>(std::filesystem::u8path(key));preview.status="Loading model...";return preview;}
    if(resource->IsLoading()){preview.status="Loading model...";return preview;}
    if(resource->IsFailed()){preview.status=resource->GetErrorMessage();return preview;}
    const auto& mesh=resource->GetData();
    if(!resource->IsLoaded()||mesh.placeholder||mesh.meshData.vertices.empty()||mesh.meshData.indices.empty()){preview.status="Model has no usable geometry";return preview;}
    PositionPlacement(app,u,v,Bounds(resource),{},[&](const auto& visit){for(const auto& vertex:mesh.meshData.vertices)visit({vertex.position[0],vertex.position[1],vertex.position[2]});});
    if(preview.ready)preview.items.push_back({key,preview.transform});
    return preview;
}
bool EditorLayer::PositionPlacement(Application& app,float u,float v,const PlacementBounds& bounds,const ecs::TransformComponent& base,const PlacementPoints& geometry,const std::vector<ecs::Entity>& ignore,PlacementPreview* output){
    auto& preview=output?*output:m_PlacementPreview;
    const auto eye=app.GetCameraPosition();const auto camera=GizmoCamera(eye,app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),float(std::max(1,m_ViewportPixelWidth))/std::max(1,m_ViewportPixelHeight),app.GetCameraNearPlane(),app.GetCameraFarPlane());
    float x=(std::clamp(u,0.f,1.f)*2-1)/camera.projection[0],y=(1-std::clamp(v,0.f,1.f)*2)/camera.projection[5];
    V ray{camera.view[0]*x+camera.view[1]*y+camera.view[2],camera.view[4]*x+camera.view[5]*y+camera.view[6],camera.view[8]*x+camera.view[9]*y+camera.view[10]};ray=Scale(ray,1/std::sqrt(Dot(ray,ray)));
    auto hit=m_SurfaceSnap?RaycastPlacement(app,eye,ray,app.GetCameraFarPlane(),ignore):std::nullopt;
    V normal{0,1,0},contact;bool gridApplied=false;
    if(hit){
        preview.surface=true;normal=hit->normal;contact=hit->position;preview.status="Surface: exact Mesh triangles";
        if(m_Snapping){
            V snapped{std::round(contact.x/m_SnapTranslation)*m_SnapTranslation,std::round(contact.y/m_SnapTranslation)*m_SnapTranslation,std::round(contact.z/m_SnapTranslation)*m_SnapTranslation};
            snapped=Add(snapped,Scale(normal,Dot(normal,Add(contact,Scale(snapped,-1)))));
            // Keep the snapped contact on real triangles, including small/edge faces.
            auto snappedHit=RaycastPlacement(app,Add(snapped,Scale(normal,.01f)),Scale(normal,-1),.02f,ignore);
            if(snappedHit){contact=snappedHit->position;normal=snappedHit->normal;gridApplied=true;}
            else preview.status+=" (grid point outside surface; contact retained)";
        }
    }else{
        if(output)return false; // A Move gesture without a surface keeps ordinary Gizmo movement.
        float plane=std::abs(ray.y)>1e-6f?-eye.y/ray.y:-1;
        const bool ground=plane>app.GetCameraNearPlane()&&plane<app.GetCameraFarPlane();
        const float distance=ground?plane:std::min(10.f,app.GetCameraFarPlane()*.5f);contact=Add(eye,Scale(ray,distance));
        preview.status=ground?"Ground plane Y=0 (no Mesh surface)":"Camera distance (no Mesh surface)";
        if(m_Snapping){contact={std::round(contact.x/m_SnapTranslation)*m_SnapTranslation,std::round(contact.y/m_SnapTranslation)*m_SnapTranslation,std::round(contact.z/m_SnapTranslation)*m_SnapTranslation};gridApplied=true;}
    }
    if(gridApplied)preview.status+=" / Grid Snap";
    preview.transform=ContactPlacement(bounds,base,contact,normal,m_AlignToNormal&&preview.surface,preview.surface?m_SurfaceOffset:0,preview.surface?m_SurfaceRotationOffset:0,geometry);
    if(preview.surface)preview.status+=geometry?" / vertex contact":" / bounds contact";
    preview.ready=ValidGroupSource(preview.transform);if(!preview.ready)preview.status="Placement Transform exceeds editor limits";
    return preview.ready;
}
const EditorLayer::PlacementPreview& EditorLayer::UpdatePrefabPlacementPreview(Application& app,const std::string& key,float u,float v){
    m_PlacementPreview={};auto& preview=m_PlacementPreview;preview.active=true;preview.key=key;preview.kind=ecs::AssetKind::Prefab;
    if(app.IsEditorPlayMode()||app.IsPrefabEditing()){preview.status="Return to the scene and stop Play to place prefabs";m_PrefabPlacement.reset();return preview;}
    if(!std::isfinite(u)||!std::isfinite(v)){preview.status="Invalid placement coordinates";return preview;}
    auto* resources=app.GetResourceManager();if(!resources){preview.status="Resource manager unavailable";return preview;}
    try{
        const auto path=scripting::SafeAsset(key,".prefab.json");const auto time=std::filesystem::last_write_time(path);const auto size=std::filesystem::file_size(path);if(!m_PrefabPlacement||m_PrefabPlacement->key!=key)scripting::InvalidatePrefabCache();const auto prefabRevision=scripting::PrefabRevision(key);
        if(!m_PrefabPlacement||m_PrefabPlacement->key!=key||m_PrefabPlacement->path!=path||m_PrefabPlacement->time!=time||m_PrefabPlacement->size!=size||m_PrefabPlacement->prefabRevision!=prefabRevision){
            // Parse and resolve the local hierarchy only on source changes / a new drag.
            m_PrefabPlacement.emplace();auto& cache=*m_PrefabPlacement;cache.key=key;cache.path=path;cache.time=time;cache.size=size;cache.prefabRevision=prefabRevision;
            try{
                cache.source=scripting::ReadPrefab(key);
                ecs::World source;SceneSettings settings;std::string error;
                if(!SceneSerializer::RestoreWorld({{"version",4},{"entities",cache.source.at("entities")}},source,settings,&error))throw std::runtime_error(error);
                ecs::Entity root;for(auto e:source.Entities())if(SceneSerializer::EntitySceneId(source,e)==cache.source.at("root").get<std::string>())root=e;
                cache.root=source.LocalTransform(root);cache.root.position={};
                if(!source.SetLocalTransform(root,{},&error))throw std::runtime_error(error);
                for(auto e:source.Subtree(root)){
                    const auto relative=source.WorldTransform(e);cache.nodes.push_back(relative);
                    if(auto* mesh=source.GetComponent<ecs::MeshRendererComponent>(e);mesh&&mesh->visible){
                        PrefabPlacementPart part;part.mesh=mesh->meshPath;part.relative=relative;cache.parts.push_back(std::move(part));
                    }
                }
                if(cache.parts.empty()){cache.bounds={{0,0,0},{0,0,0}};cache.hasBounds=true;}
            }catch(const std::exception& e){cache.error=e.what();cache.parts.clear();cache.nodes.clear();}
        }
        auto& cache=*m_PrefabPlacement;if(!cache.error.empty()){preview.status="Prefab: "+cache.error;return preview;}
        std::size_t loading=0,failed=0;std::string diagnostic;bool changed=!cache.hasBounds;
        for(auto& part:cache.parts){
            if(part.mesh.empty()){changed|=part.usable;part.usable=false;++failed;if(diagnostic.empty())diagnostic="Visible Mesh has an empty asset key";continue;}
            auto mesh=resources->Get<MeshResource>(part.mesh);
            if(!mesh){(void)resources->LoadAsync<MeshResource>(part.mesh);mesh=resources->Get<MeshResource>(part.mesh);}
            const bool usable=mesh&&mesh->IsLoaded()&&!mesh->GetData().placeholder&&!mesh->GetData().meshData.vertices.empty()&&!mesh->GetData().meshData.indices.empty();
            if(part.owner.lock().get()!=mesh.get()||part.version!=(mesh?mesh->GetVersion():0)||part.usable!=usable){
                part.owner=mesh;part.version=mesh?mesh->GetVersion():0;part.usable=usable;changed=true;
                if(usable)part.bounds=Bounds(mesh);
            }
            if(!usable){
                if(!mesh||mesh->IsLoading()||mesh->GetLoadState()==ResourceLoadState::Unloaded)++loading;else ++failed;
                if(diagnostic.empty())diagnostic=part.mesh+": "+(mesh&&mesh->HasError()?mesh->GetErrorMessage():"geometry unavailable");
            }
        }
        if(changed){
            cache.hasBounds=false;PlacementBounds bounds{{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};
            for(const auto& part:cache.parts)if(part.usable)for(auto p:PlacementCorners(part.bounds,part.relative)){
                bounds.min={std::min(bounds.min.x,p.x),std::min(bounds.min.y,p.y),std::min(bounds.min.z,p.z)};
                bounds.max={std::max(bounds.max.x,p.x),std::max(bounds.max.y,p.y),std::max(bounds.max.z,p.z)};cache.hasBounds=true;
            }
            cache.bounds=bounds;
        }
        PlacementPoints geometry=[&](const auto& visit){for(const auto& part:cache.parts)if(part.usable){auto mesh=resources->Get<MeshResource>(part.mesh);if(!mesh||!mesh->IsLoaded()||mesh->GetVersion()!=part.version)continue;const auto matrix=TransformMatrix(part.relative);for(const auto& vertex:mesh->GetData().meshData.vertices)visit(PlacementTransformPoint(matrix,{vertex.position[0],vertex.position[1],vertex.position[2]}));}};
        if(cache.hasBounds&&PositionPlacement(app,u,v,cache.bounds,cache.root,geometry)){
            const auto rootMatrix=TransformMatrix(preview.transform);
            // Same checked TRS composition used by World hierarchy; never draw a
            // distorted approximation of a graph that could not be instantiated.
            for(const auto& node:cache.nodes)if(!DecomposeTransform(MultiplyMatrix(rootMatrix,TransformMatrix(node)),node.scale))throw std::runtime_error("Prefab placement requires unsupported shear or out-of-range transforms");
            preview.items.reserve(cache.parts.size());
            for(const auto& part:cache.parts)if(part.usable){
                auto pose=DecomposeTransform(MultiplyMatrix(rootMatrix,TransformMatrix(part.relative)),part.relative.scale);
                if(!pose)throw std::runtime_error("Prefab Mesh transform cannot be represented");preview.items.push_back({part.mesh,*pose});
            }
        }
        if(loading||failed){preview.ready=false;preview.status+=" / Partial Prefab: "+std::to_string(preview.items.size())+"/"+std::to_string(cache.parts.size())+" Mesh, "+std::to_string(loading)+" loading, "+std::to_string(failed)+" failed; drop disabled. "+diagnostic;}
        else if(preview.ready)preview.status+=cache.parts.empty()?" / Prefab has no visible Mesh (root placement)":" / Prefab: "+std::to_string(preview.items.size())+" Mesh";
        return preview;
    }catch(const std::exception& e){preview.ready=false;preview.items.clear();preview.status="Prefab: "+std::string(e.what());return preview;}
}
ecs::Entity EditorLayer::ConfirmPlacement(Application& app){
    auto preview=m_PlacementPreview;if(app.IsEditorPlayMode()||!preview.active||!preview.ready)return {};
    if(preview.kind==ecs::AssetKind::Prefab){
        try{
            if(!m_PrefabPlacement||m_PrefabPlacement->key!=preview.key)throw std::runtime_error("Prefab preview expired; drag it again");
            const auto& cache=*m_PrefabPlacement;
            scripting::PollPrefabReload(true);
            if(scripting::SafeAsset(preview.key,".prefab.json")!=cache.path||std::filesystem::last_write_time(cache.path)!=cache.time||std::filesystem::file_size(cache.path)!=cache.size||scripting::PrefabRevision(preview.key)!=cache.prefabRevision)throw std::runtime_error("Prefab or its Parent changed during placement; wait for its updated preview");
            auto* resources=app.GetResourceManager();
            for(const auto& part:cache.parts){auto mesh=resources?resources->Get<MeshResource>(part.mesh):nullptr;
                if(!mesh||!mesh->IsLoaded()||mesh->GetData().placeholder||part.owner.lock()!=mesh||part.version!=mesh->GetVersion())throw std::runtime_error("Prefab Mesh changed or is unavailable; wait for its updated preview");}
            auto entity=InstantiatePrefab(app,preview.key,preview.transform.position,&preview.transform,&cache.source);CancelPlacement();return entity;
        }catch(const std::exception& e){m_EditorError=e.what();CancelPlacement();return {};}
    }
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
    std::vector<ecs::Entity> ignored;for(auto root:app.GetWorld().HierarchyRoots(m_Selection.items)){auto subtree=app.GetWorld().Subtree(root);ignored.insert(ignored.end(),subtree.begin(),subtree.end());}
    for(auto e:app.GetWorld().HierarchyRoots(m_Selection.items))if(app.GetWorld().HasComponent<ecs::TransformComponent>(e)){
        auto absolute=app.GetWorld().WorldTransform(e);auto* t=&absolute;
        std::vector<V> corners;bool unavailable=false;
        for(auto descendant:app.GetWorld().Subtree(e))if(auto* renderer=app.GetWorld().GetComponent<ecs::MeshRendererComponent>(descendant);renderer&&renderer->visible){
            auto mesh=resources->Get<MeshResource>(renderer->meshPath);
            if(!mesh){(void)resources->LoadAsync<MeshResource>(renderer->meshPath);unavailable=true;continue;}
            if(!mesh->IsLoaded()||mesh->GetData().placeholder){unavailable=true;continue;}
            const auto pose=app.GetWorld().WorldTransform(descendant);const auto matrix=TransformMatrix(pose);
            if(!mesh->GetData().meshData.vertices.empty())for(const auto& vertex:mesh->GetData().meshData.vertices)corners.push_back(PlacementTransformPoint(matrix,{vertex.position[0],vertex.position[1],vertex.position[2]}));
            else {auto values=PlacementCorners(Bounds(mesh),pose);corners.insert(corners.end(),values.begin(),values.end());}
        }
        if(unavailable){++loading;continue;}
        if(corners.empty()){auto values=PlacementCorners(PlacementBounds{},*t);corners.insert(corners.end(),values.begin(),values.end());}
        V lo{INFINITY,INFINITY,INFINITY},hi{-INFINITY,-INFINITY,-INFINITY};
        for(auto p:corners){lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);}
        auto hit=RaycastPlacement(app,{(lo.x+hi.x)*.5f,lo.y+.001f,(lo.z+hi.z)*.5f},{0,-1,0},2000001.f,ignored);
        if(!hit||hit->normal.y<.05f)continue;
        float support=INFINITY;for(auto p:corners)support=std::min(support,Dot(hit->normal,Add(p,Scale(hit->position,-1))));
        const float drop=(m_SurfaceOffset-support)/hit->normal.y;if(std::abs(drop)<.0001f)continue;
        auto target=*t;target.position.y+=drop;
        if(!ValidGroupSource(target))continue;
        EntitySnapshot item;item.entity=e;item.world=e.world;item.alive=true;item.transform=app.GetWorld().LocalTransform(e);item.parent=app.GetWorld().Parent(e);before.related.push_back(std::move(item));changes.push_back({e,target});
    }
    m_EditorError=loading?std::to_string(loading)+" model bounds unavailable; skipped. Wait for loading and retry.":"";
    if(changes.empty())return false;
    std::vector<std::pair<ecs::Entity,ecs::TransformComponent>> edits;for(auto& change:changes)edits.emplace_back(change.entity,change.target);
    if(!app.GetWorld().SetWorldTransforms(edits,&m_EditorError))return false;for(auto& change:changes)app.NotifyEditorMutation(change.entity);
    CommitUndo(app,"Drop to Floor",std::move(before));return true;
}
bool EditorLayer::DrawPlacementMenu(Application& app){
    if(ImGui::Button("Placement"))ImGui::OpenPopup("PlacementTools");bool owns=ImGui::IsItemHovered()||ImGui::IsPopupOpen("PlacementTools");
    if(ImGui::BeginPopup("PlacementTools")){
        ImGui::BeginDisabled(app.IsEditorPlayMode());ImGui::Checkbox("Surface Snap",&m_SurfaceSnap);
        bool surfaceChanged=false;ImGui::BeginDisabled(!m_SurfaceSnap);
        int orientation=m_AlignToNormal?1:0;if(ImGui::Combo("Orientation",&orientation,"Preserve Rotation\0Align to Normal\0")){m_AlignToNormal=orientation==1;surfaceChanged=true;}
        ImGui::SetNextItemWidth(140);surfaceChanged|=ImGui::DragFloat("Surface Offset",&m_SurfaceOffset,.01f,-1000,1000,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetNextItemWidth(140);surfaceChanged|=ImGui::DragFloat("Rotation Offset (deg)",&m_SurfaceRotationOffset,.5f,-180,180,"%.1f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextDisabled("Move Gizmo: hold S before dragging for Surface Snap.");
        ImGui::TextDisabled("Vertex Snap takes priority. No surface: ordinary movement.");ImGui::EndDisabled();if(surfaceChanged)PersistPreferences(app);
        ImGui::Checkbox("Grid Snap",&m_Snapping);ImGui::SetNextItemWidth(130);ImGui::DragFloat("Grid step",&m_SnapTranslation,.05f,.001f,1000,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::BeginDisabled(HasPendingTransaction()||(!m_EditPivot&&m_Selection.items.size()!=1));ImGui::Checkbox("Edit Pivot",&m_EditPivot);ImGui::EndDisabled();
        ImGui::BeginDisabled(m_Selection.items.size()!=1||HasPendingTransaction());
        if(ImGui::MenuItem("Reset Pivot"))ResetEntityPivot(app,m_Selection.active);
        if(ImGui::MenuItem("Pivot to Bounds Center"))CenterEntityPivot(app,m_Selection.active);ImGui::EndDisabled();
        if(m_Selection.items.size()!=1)ImGui::TextDisabled("Edit Pivot requires one entity.");
        if(m_EditPivot)ImGui::TextWrapped("Move the pivot; geometry stays in place. Exit Edit Pivot to Rotate/Scale. Escape cancels the drag.");
        bool vertex=m_VertexSnapEnabled;if(ImGui::Checkbox("Vertex Snap",&vertex))SetVertexSnap(vertex);
        if(m_VertexSnapEnabled){ImGui::TextWrapped("V + click: choose source on selected geometry. Drag Move Gizmo to a target vertex; release confirms. Escape cancels.");ImGui::TextWrapped("Vertex Snap overrides Grid during Move. Alt+Drag duplicates without vertex snapping.");ImGui::TextWrapped("%s",m_VertexSnapStatus.c_str());}
        ImGui::BeginDisabled(m_Selection.items.empty());if(ImGui::MenuItem("Drop to Floor"))DropSelectionToFloor(app);ImGui::EndDisabled();ImGui::EndDisabled();
        ImGui::TextDisabled("Surface queries use exact Mesh triangles.");ImGui::TextDisabled("No hit: ground Y=0, then 10 units from camera.");ImGui::TextDisabled("Drop without Mesh: transformed unit-cube bounds.");
        ImGui::EndPopup();
    }return owns;
}
void EditorLayer::DrawPlacementTarget(Application& app,const ImVec2& origin,const ImVec2& size){
    const auto* dragged=ImGui::GetDragDropPayload();
    if(dragged&&dragged->IsDataType("WHISP_ASSET")&&ImGui::IsKeyPressed(ImGuiKey_Escape,false)){m_PlacementCancelled=true;m_EscapeConsumed=true;}
    if(m_PlacementCancelled){ImGui::ClearDragDrop();CancelPlacement();if(!ImGui::IsMouseDown(0))m_PlacementCancelled=false;return;}
    if(!dragged||!dragged->IsDataType("WHISP_ASSET"))CancelPlacement();
    else {m_PlacementPreview.active=false;m_PlacementPreview.items.clear();}
    if(app.IsEditorPlayMode()||m_ModalActive||m_ToolActive||m_LightPointerCaptured||m_PlaneScaleCaptured||app.IsCameraControlActive()||ImGui::IsMouseDown(1)){CancelPlacement();return;}
    if(ImGui::BeginDragDropTarget()){
        if(const auto* payload=ImGui::AcceptDragDropPayload("WHISP_ASSET",ImGuiDragDropFlags_AcceptBeforeDelivery))if(payload->DataSize==sizeof(AssetPayload)){
            const auto& asset=*static_cast<const AssetPayload*>(payload->Data);
            if(std::memchr(asset.key,0,sizeof(asset.key))){
                const auto mouse=ImGui::GetMousePos();float u=(mouse.x-origin.x)/size.x,v=(mouse.y-origin.y)/size.y;
                if(asset.kind==ecs::AssetKind::Mesh||asset.kind==ecs::AssetKind::Prefab){
                    UpdatePlacementPreview(app,asset.key,u,v);
                    auto status=m_PlacementPreview.status;
                    if(payload->IsDelivery()){
                        if(!m_PlacementPreview.ready)m_EditorError=status;
                        else ConfirmPlacement(app);
                    }else{
                        auto* draw=ImGui::GetWindowDrawList();const ImVec2 p{origin.x+10,origin.y+size.y-30};
                        draw->AddRectFilled({p.x-4,p.y-3},{std::min(origin.x+size.x-5,p.x+ImGui::CalcTextSize(status.c_str()).x+5),p.y+ImGui::GetTextLineHeight()+4},IM_COL32(12,18,24,230));
                        if(!m_PlacementPreview.ready){ImGui::BeginTooltip();ImGui::PushTextWrapPos(480*ImGui::GetStyle().FontScaleDpi);ImGui::TextUnformatted(status.c_str());ImGui::PopTextWrapPos();ImGui::EndTooltip();}
                        draw->AddText(p,m_PlacementPreview.ready?IM_COL32(80,235,255,255):IM_COL32(255,190,80,255),status.c_str());
                    }
                }else if(asset.kind==ecs::AssetKind::None&&std::string(asset.key).ends_with(".wscene")&&payload->IsDelivery())PlaceImportedScene(app,asset.key,PlacementPoint(app,u,v));
            }
        }ImGui::EndDragDropTarget();
    }
}
void EditorLayer::DrawSurfaceMove(Application& app,const ImVec2& origin,const ImVec2& size,bool afterGizmo){
    const auto& io=ImGui::GetIO();
    if(app.IsEditorPlayMode()||m_EditPivot||m_VertexSnapEnabled||m_GameView||!m_SurfaceSnap||m_GizmoOperation!=0||m_ModalActive||app.IsCameraControlActive()||ImGui::GetDragDropPayload()||io.KeyAlt||io.KeyCtrl||io.WantTextInput){m_SurfaceMove.reset();return;}
    auto& world=app.GetWorld();auto* resources=app.GetResourceManager();if(!resources)return;
    if(m_SurfaceMove&&(m_SurfaceMove->world!=world.GetIdentity()||m_SurfaceMove->roots!=world.HierarchyRoots(m_Selection.items)))m_SurfaceMove.reset();
    if(!afterGizmo){
        if(ImGui::IsKeyDown(ImGuiKey_S)&&ImGui::IsMouseClicked(0)&&ImGuizmo::IsOver()&&!m_Selection.items.empty()){
            SurfaceMoveSession session;session.world=world.GetIdentity();session.roots=world.HierarchyRoots(m_Selection.items);
            for(auto e:session.roots){const auto t=world.WorldTransform(e);session.originals.push_back(t);const auto p=ecs::PivotPosition(t);session.base.position=Add(session.base.position,p);auto subtree=world.Subtree(e);session.ignored.insert(session.ignored.end(),subtree.begin(),subtree.end());}
            session.base.position=Scale(session.base.position,1.f/session.roots.size());session.base.rotation=world.WorldTransform(m_Selection.active).rotation;const auto inverseFrame=InverseGroupPivot(session.base);session.bounds={{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};
            auto include=[&](V p){p=PlacementTransformPoint(inverseFrame,p);auto& b=session.bounds;b.min={std::min(b.min.x,p.x),std::min(b.min.y,p.y),std::min(b.min.z,p.z)};b.max={std::max(b.max.x,p.x),std::max(b.max.y,p.y),std::max(b.max.z,p.z)};};
            for(auto e:session.ignored)if(auto* renderer=world.GetComponent<ecs::MeshRendererComponent>(e);renderer&&renderer->visible){
                PrefabPlacementPart part;part.mesh=renderer->meshPath;part.relative=world.WorldTransform(e);auto mesh=resources->Get<MeshResource>(part.mesh);part.owner=mesh;part.version=mesh?mesh->GetVersion():0;part.usable=mesh&&mesh->IsLoaded()&&!mesh->GetData().placeholder&&!mesh->GetData().meshData.vertices.empty();part.bounds=part.usable?Bounds(mesh):PlacementBounds{};
                for(auto point:PlacementCorners(part.bounds,part.relative))include(point);session.parts.push_back(std::move(part));
            }
            if(session.parts.empty())for(auto point:PlacementCorners(PlacementBounds{},session.base))include(point);
            m_SurfaceMove=std::move(session);
        }return;
    }
    if(!m_SurfaceMove)return;
    if(!ImGuizmo::IsUsing()){m_SurfaceMove.reset();return;}
    if(!m_GizmoUndoCaptured||m_DuplicateDrag)return;
    auto& session=*m_SurfaceMove;const auto inverseFrame=InverseGroupPivot(session.base);
    PlacementPoints geometry=[&](const auto& visit){for(const auto& part:session.parts){auto mesh=resources->Get<MeshResource>(part.mesh);const auto matrix=TransformMatrix(part.relative);
        if(part.usable&&mesh&&mesh->GetVersion()==part.version){for(const auto& vertex:mesh->GetData().meshData.vertices)visit(PlacementTransformPoint(inverseFrame,PlacementTransformPoint(matrix,{vertex.position[0],vertex.position[1],vertex.position[2]})));}
        else for(auto point:PlacementCorners(part.bounds,part.relative))visit(PlacementTransformPoint(inverseFrame,point));
    }};
    const auto mouse=ImGui::GetMousePos();PlacementPreview preview;
    if(!PositionPlacement(app,(mouse.x-origin.x)/size.x,(mouse.y-origin.y)/size.y,session.bounds,session.base,geometry,session.ignored,&preview))return;
    auto transforms=TransformGroup(session.originals,session.base,TransformMatrix(preview.transform));if(!transforms){m_EditorError="Surface alignment requires unsupported shear";return;}
    std::vector<std::pair<ecs::Entity,ecs::TransformComponent>> edits;for(std::size_t i=0;i<session.roots.size();++i)edits.emplace_back(session.roots[i],(*transforms)[i]);
    if(!world.SetWorldTransforms(edits,&m_EditorError))return;for(auto e:session.roots)app.NotifyEditorMutation(e);
    if(m_GroupTransform){const auto delta=MultiplyMatrix(TransformMatrix(preview.transform),InverseGroupPivot(session.base));m_GroupTransform->current=MultiplyMatrix(delta,TransformMatrix(m_GroupTransform->pivot));}
    ImGui::GetWindowDrawList()->AddText({origin.x+10,origin.y+size.y-24},IM_COL32(80,235,255,255),"SURFACE MOVE: release confirms; Escape cancels.");
}

}
