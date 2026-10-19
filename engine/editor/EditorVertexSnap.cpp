#include "EditorLayer.h"
#include "LightGizmoMath.h"
#include "../core/Application.h"
#include "../resources/ResourceManager.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <cmath>

namespace editor {
namespace {
using V=ecs::Vec3;
V TransformPoint(const ImportMatrix& m,V p){return {float(m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12]),float(m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13]),float(m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14])};}
bool Finite(V p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
// Only local bounds are cached, with resource ownership/version checks. Vertex
// buffers remain owned by ResourceManager and are never copied or modified.
PlacementBounds CachedBounds(const ResourceHandle<MeshResource>& mesh){
    struct Entry{std::weak_ptr<Resource<MeshResource>> owner;std::uint64_t version=0;PlacementBounds bounds;};
    static thread_local std::unordered_map<const void*,Entry> cache;
    if(cache.size()>4096)cache.clear();auto& entry=cache[mesh.get()];
    if(entry.owner.lock()!=mesh||entry.version!=mesh->GetVersion()){entry.owner=mesh;entry.version=mesh->GetVersion();entry.bounds=MeshPlacementBounds(mesh->GetData().meshData);}
    return entry.bounds;
}
}
bool EditorLayer::VertexMovesWithSelection(const Application& app,ecs::Entity entity)const{
    for(auto e=entity;e.IsValid();e=app.GetWorld().Parent(e))if(m_Selection.Contains(e))return true;return false;
}
std::optional<V> EditorLayer::VertexWorldPosition(Application& app,const VertexReference& vertex)const{
    auto& world=app.GetWorld();if(!world.IsAlive(vertex.entity)||!world.HasComponent<ecs::TransformComponent>(vertex.entity))return {};
    const auto* renderer=world.GetComponent<ecs::MeshRendererComponent>(vertex.entity);auto* resources=app.GetResourceManager();
    if(!renderer||!renderer->visible||renderer->meshPath!=vertex.mesh||!resources)return {};
    auto mesh=resources->Get<MeshResource>(vertex.mesh);
    if(!mesh||!mesh->IsLoaded()||mesh->GetData().placeholder||mesh->GetVersion()!=vertex.version||vertex.index>=mesh->GetData().meshData.vertices.size())return {};
    const auto& p=mesh->GetData().meshData.vertices[vertex.index].position;
    auto result=TransformPoint(TransformMatrix(world.WorldTransform(vertex.entity)),{p[0],p[1],p[2]});return Finite(result)?std::optional<V>(result):std::nullopt;
}
bool EditorLayer::SetVertexSnapSource(Application& app,ecs::Entity entity,std::size_t index){
    if(app.IsEditorPlayMode()||!app.GetWorld().IsAlive(entity)||!VertexMovesWithSelection(app,entity)||!EditorEntityVisible(app,entity))return false;
    auto* renderer=app.GetWorld().GetComponent<ecs::MeshRendererComponent>(entity);auto* resources=app.GetResourceManager();
    auto mesh=renderer&&resources?resources->Get<MeshResource>(renderer->meshPath):ResourceHandle<MeshResource>{};
    if(!mesh||!mesh->IsLoaded()||mesh->GetData().placeholder||index>=mesh->GetData().meshData.vertices.size()){m_VertexSnapStatus="Source geometry is not loaded or vertex is unavailable.";return false;}
    VertexReference source{entity,renderer->meshPath,mesh->GetVersion(),index};if(!VertexWorldPosition(app,source))return false;
    m_VertexSource=source;m_VertexTarget.reset();m_VertexSnapStatus="Source chosen. Drag Move Gizmo near another mesh vertex.";return true;
}
std::optional<EditorLayer::VertexReference> EditorLayer::FindViewportVertex(Application& app,float u,float v,bool source){
    if(app.IsEditorPlayMode()||!std::isfinite(u)||!std::isfinite(v)||u<0||u>1||v<0||v>1)return {};
    auto& world=app.GetWorld();auto* resources=app.GetResourceManager();const auto b=GetViewportBounds();const float w=b[2]-b[0],h=b[3]-b[1];if(!resources||w<=1||h<=1)return {};
    const auto camera=GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),w/h,app.GetCameraNearPlane(),app.GetCameraFarPlane());
    const float radius=12*ImGui::GetStyle().FontScaleDpi,retention=radius*1.5f;
    auto distance=[&](V p)->std::optional<std::array<float,2>>{auto q=ProjectGizmo(p,camera);if(!q)return {};float x=((*q)[0]-u)*w,y=((*q)[1]-v)*h;return std::array<float,2>{x*x+y*y,(*q)[2]};};
    auto eligible=[&](ecs::Entity e){return EditorEntityVisible(app,e)&&(source?VertexMovesWithSelection(app,e):(!VertexMovesWithSelection(app,e)&&CanViewportSelect(app,e)));};
    if(!source&&m_VertexTarget&&eligible(m_VertexTarget->entity))if(auto p=VertexWorldPosition(app,*m_VertexTarget))if(auto d=distance(*p);d&&(*d)[0]<=retention*retention)return m_VertexTarget;
    std::optional<VertexReference> best;float bestDistance=radius*radius,bestDepth=INFINITY;
    world.ForEach<ecs::TransformComponent,ecs::MeshRendererComponent>([&](ecs::Entity e,const auto&,const auto& renderer){
        if(!renderer.visible||!eligible(e))return;auto mesh=resources->Get<MeshResource>(renderer.meshPath);
        if(!mesh||!mesh->IsLoaded()||mesh->GetData().placeholder||mesh->GetData().meshData.vertices.empty())return;
        const auto transform=world.WorldTransform(e);const auto matrix=TransformMatrix(transform);const auto corners=PlacementCorners(CachedBounds(mesh),transform);
        float left=INFINITY,top=INFINITY,right=-INFINITY,bottom=-INFINITY;bool crossesDepth=false;unsigned front=0;
        for(auto p:corners){const auto& m=camera.view;const float z=m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14];if(z>=camera.nearPlane)++front;
            if(z<camera.nearPlane){crossesDepth=true;continue;}
            float x=.5f+.5f*camera.projection[0]*(m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12])/z,y=.5f-.5f*camera.projection[5]*(m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13])/z;
            left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);}
        if(!front||(!crossesDepth&&(u<left-radius/w||u>right+radius/w||v<top-radius/h||v>bottom+radius/h)))return;
        // Only meshes whose projected bounds meet the cursor are scanned.
        const auto& vertices=mesh->GetData().meshData.vertices;
        for(std::size_t i=0;i<vertices.size();++i){const auto& p=vertices[i].position;auto d=distance(TransformPoint(matrix,{p[0],p[1],p[2]}));if(!d)continue;
            if((*d)[0]<bestDistance-1e-4f||(std::abs((*d)[0]-bestDistance)<=1e-4f&&(*d)[1]<bestDepth)){bestDistance=(*d)[0];bestDepth=(*d)[1];best=VertexReference{e,renderer.meshPath,mesh->GetVersion(),i};}}
    });return best;
}
bool EditorLayer::SnapSelectionToVertex(Application& app,const VertexReference& target,bool finish){
    if(app.IsEditorPlayMode()||!m_VertexSnapEnabled||!m_VertexSource||!VertexMovesWithSelection(app,m_VertexSource->entity)||VertexMovesWithSelection(app,target.entity)||!CanViewportSelect(app,target.entity))return false;
    auto sourcePoint=VertexWorldPosition(app,*m_VertexSource),targetPoint=VertexWorldPosition(app,target);
    if(!sourcePoint||!targetPoint){m_VertexSnapStatus="Geometry changed or is not loaded. Choose the source again.";return false;}
    V delta{targetPoint->x-sourcePoint->x,targetPoint->y-sourcePoint->y,targetPoint->z-sourcePoint->z};if(!Finite(delta))return false;
    if(std::abs(delta.x)+std::abs(delta.y)+std::abs(delta.z)<1e-6f)return true;
    auto& world=app.GetWorld();auto before=m_Transaction?EntitySnapshot{}:CaptureSelection(app,true);std::vector<std::pair<ecs::Entity,ecs::TransformComponent>> edits;
    for(auto e:world.HierarchyRoots(m_Selection.items)){auto t=world.WorldTransform(e);t.position={t.position.x+delta.x,t.position.y+delta.y,t.position.z+delta.z};edits.emplace_back(e,t);}
    if(!world.SetWorldTransforms(edits,&m_EditorError)){m_VertexSnapStatus="Vertex Snap rejected: "+m_EditorError;return false;}
    for(const auto& [e,t]:edits)app.NotifyEditorMutation(e);
    if(m_GroupTransform){m_GroupTransform->current[12]+=delta.x;m_GroupTransform->current[13]+=delta.y;m_GroupTransform->current[14]+=delta.z;}
    if(!m_Transaction)BeginTransaction("Vertex Snap",std::move(before));if(finish)FinishTransaction(app);
    m_VertexTarget=target;m_VertexSnapStatus="Vertices aligned. Release to confirm; Escape cancels.";return true;
}
void EditorLayer::DrawVertexSnap(Application& app,const ImVec2& origin,const ImVec2& size,bool afterGizmo){
    if(!afterGizmo)m_VertexSourcePicking=false;
    if(m_EditPivot||!m_VertexSnapEnabled||app.IsEditorPlayMode()||m_GameView||app.IsCameraControlActive()||m_ModalActive||size.x<=1||size.y<=1||ImGui::GetDragDropPayload()){
        m_VertexTarget.reset();return;
    }
    if(m_VertexSource&&(!VertexMovesWithSelection(app,m_VertexSource->entity)||!VertexWorldPosition(app,*m_VertexSource))){m_VertexSource.reset();m_VertexTarget.reset();m_VertexSnapStatus="Source unavailable. Hold V + click a loaded selected mesh vertex.";}
    const auto mouse=ImGui::GetMousePos();const float u=(mouse.x-origin.x)/size.x,v=(mouse.y-origin.y)/size.y;
    if(!afterGizmo){
        const auto& io=ImGui::GetIO();m_VertexSourcePicking=ImGui::IsKeyDown(ImGuiKey_V)&&!io.KeyCtrl&&!io.KeyAlt&&!io.WantTextInput&&!BlocksGameplayInput()&&!ImGuizmo::IsUsing()&&m_ViewportHovered;
        if(m_VertexSourcePicking){auto vertex=FindViewportVertex(app,u,v,true);m_VertexSnapStatus=vertex?"Click to choose this source vertex.":"No loaded selected mesh vertex within 12 pixels.";
            if(vertex&&ImGui::IsMouseClicked(0))SetVertexSnapSource(app,vertex->entity,vertex->index);
            if(vertex)m_VertexTarget=vertex;else m_VertexTarget.reset();}
        return;
    }
    const bool moving=m_GizmoOperation==0&&ImGuizmo::IsUsing()&&m_GizmoUndoCaptured&&!m_DuplicateDrag;
    if(moving){
        m_VertexTarget=FindViewportVertex(app,u,v,false);
        if(m_VertexSource&&m_VertexTarget)SnapSelectionToVertex(app,*m_VertexTarget,false);
        else m_VertexSnapStatus=m_VertexSource?"No target vertex nearby. Move normally or approach another corner.":"Choose a source first: hold V + click selected geometry.";
    }else if(!m_VertexSourcePicking)m_VertexTarget.reset();
    const auto camera=GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),size.x/size.y,app.GetCameraNearPlane(),app.GetCameraFarPlane());
    auto screen=[&](const std::optional<VertexReference>& ref)->std::optional<ImVec2>{if(!ref)return {};auto p=VertexWorldPosition(app,*ref);auto q=p?ProjectGizmo(*p,camera):std::nullopt;if(!q)return {};return ImVec2{origin.x+(*q)[0]*size.x,origin.y+(*q)[1]*size.y};};
    const auto a=screen(m_VertexSource),b=screen(m_VertexTarget);auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
    if(a)draw->AddCircle(*a,6,IM_COL32(255,210,70,255),16,2);
    if(b){draw->AddCircleFilled(*b,4,IM_COL32(80,225,255,255));draw->AddCircle(*b,8,IM_COL32(80,225,255,255),16,1.5f);if(a)draw->AddLine(*a,*b,IM_COL32(80,225,255,220),1.5f);}
    draw->AddText({origin.x+10,origin.y+size.y-24},IM_COL32(130,220,255,255),m_VertexSnapStatus.c_str());draw->PopClipRect();
}
}
