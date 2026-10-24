#define NOMINMAX
#include "EditorLayer.h"
#include "LightGizmoMath.h"
#include "MeshOcclusion.h"
#include "../core/Application.h"
#include "../ecs/LightGather.h"
#include "../ecs/PlayerSpawn.h"
#include "../render/ColorPipeline.h"
#include "../resources/ResourceManager.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <cmath>
#include <chrono>
namespace editor
{
ecs::Entity EditorLayer::CreateLight(Application& app,LightType type)
{
    if(app.IsEditorPlayMode())return {};
    auto e=app.CreateLightEntity(type);SelectEntityFromUser(app,e);
    EntitySnapshot before;before.entity=e;before.alive=false;CommitUndo(app,"Create Light",before);return e;
}
void EditorLayer::DeleteSelectedLight(Application& app)
{
    if(!ecs::GetLightParameters(app.GetWorld(),m_Selection.active))return;
    auto before=CaptureSubtree(app,m_Selection.active);app.DestroySceneEntity(m_Selection.active);
    CommitUndo(app,"Delete Light",before);PruneSelection(app);
}
namespace
{
using V=ecs::Vec3;
V Add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V Sub(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V Scale(V a,float b){return {a.x*b,a.y*b,a.z*b};}
float Dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V Cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
struct Occluder {ResourceHandle<MeshResource> mesh;std::array<float,12> inverse;V translation,min,max;std::shared_ptr<const MeshOcclusion> occlusion;};
V Inverse(V p,const std::array<float,12>& n){return {n[0]*p.x+n[4]*p.y+n[8]*p.z,n[1]*p.x+n[5]*p.y+n[9]*p.z,n[2]*p.x+n[6]*p.y+n[10]*p.z};}
float HitDistance(V eye,V direction,const Occluder& o,float maximum,V* normal=nullptr)
{
    V p=Inverse(Sub(eye,o.translation),o.inverse),d=Inverse(direction,o.inverse);float low=0,high=maximum;
    float pv[3]={p.x,p.y,p.z},dv[3]={d.x,d.y,d.z},mn[3]={o.min.x,o.min.y,o.min.z},mx[3]={o.max.x,o.max.y,o.max.z};
    for(int k=0;k<3;++k){if(std::abs(dv[k])<1e-10f){if(pv[k]<mn[k]||pv[k]>mx[k])return maximum;}
        else{float a=(mn[k]-pv[k])/dv[k],b=(mx[k]-pv[k])/dv[k];if(a>b)std::swap(a,b);low=std::max(low,a);high=std::min(high,b);}}
    if(high<low)return maximum;const auto& mesh=o.mesh->GetData().meshData;float nearest=maximum;
    auto vertex=[&](unsigned i){auto& v=mesh.vertices[i];return V{v.position[0],v.position[1],v.position[2]};};
    for(std::size_t j=0;j+2<mesh.indices.size();j+=3){
        V a=vertex(mesh.indices[j]),e1=Sub(vertex(mesh.indices[j+1]),a),e2=Sub(vertex(mesh.indices[j+2]),a),h=Cross(d,e2);float determinant=Dot(e1,h);
        if(std::abs(determinant)<1e-10f)continue;float inv=1/determinant;V delta=Sub(p,a);float u=inv*Dot(delta,h);if(u<0||u>1)continue;
        V q=Cross(delta,e1);float v=inv*Dot(d,q);if(v<0||u+v>1)continue;float t=inv*Dot(e2,q);if(t>=0&&t<nearest){
            nearest=t;if(normal){auto n=Cross(e1,e2);const auto& m=o.inverse;
                n={m[0]*n.x+m[1]*n.y+m[2]*n.z,m[4]*n.x+m[5]*n.y+m[6]*n.z,m[8]*n.x+m[9]*n.y+m[10]*n.z};
                float length=std::sqrt(Dot(n,n));*normal=length>1e-12f?Scale(n,(Dot(n,direction)>0?-1.f:1.f)/length):V{0,1,0};
            }
        }
    }return nearest;
}
bool Occluded(V eye,V target,const std::vector<Occluder>& objects){
    V delta=Sub(target,eye);float distance=std::sqrt(Dot(delta,delta));if(distance<1e-5f)return false;
    V direction=Scale(delta,1/distance);for(const auto& o:objects){
        const float maximum=distance-.03f;
        if(o.occlusion){if(o.occlusion->AnyHit(o.mesh->GetData().meshData,Inverse(Sub(eye,o.translation),o.inverse),Inverse(direction,o.inverse),maximum))return true;}
        else if(HitDistance(eye,direction,o,maximum)<maximum)return true;
    }return false;
}
Occluder Bounds(ResourceHandle<MeshResource> mesh,const ecs::RenderPose& pose,bool accelerate=false){
    std::array<float,16> model;ecs::BuildRenderModelMatrix(model.data(),pose);auto inv=BuildNormalMatrix(model);
    struct CachedBounds {std::weak_ptr<Resource<MeshResource>> owner;std::uint64_t version=0;V lo,hi;std::shared_ptr<const MeshOcclusion> occlusion;};
    static thread_local std::unordered_map<const void*,CachedBounds> cache;
    if(cache.size()>64)std::erase_if(cache,[](auto& entry){return entry.second.owner.expired();});
    if(cache.size()>4096)cache.clear();auto& c=cache[mesh.get()];
    if(c.owner.lock()!=mesh||c.version!=mesh->GetVersion()){
        c.owner=mesh;c.version=mesh->GetVersion();c.occlusion.reset();c.lo={INFINITY,INFINITY,INFINITY};c.hi={-INFINITY,-INFINITY,-INFINITY};
        for(const auto& v:mesh->GetData().meshData.vertices){c.lo.x=std::min(c.lo.x,v.position[0]);c.lo.y=std::min(c.lo.y,v.position[1]);c.lo.z=std::min(c.lo.z,v.position[2]);c.hi.x=std::max(c.hi.x,v.position[0]);c.hi.y=std::max(c.hi.y,v.position[1]);c.hi.z=std::max(c.hi.z,v.position[2]);}
    }if(accelerate&&!c.occlusion)c.occlusion=std::make_shared<MeshOcclusion>(mesh->GetData().meshData);V lo=c.lo,hi=c.hi;
    return {mesh,inv,pose.position,lo,hi,c.occlusion};
}

}
void EditorLayer::DrawLightGizmos(Application& app,IRenderAdapter*,const ImVec2& origin,const ImVec2& size)
{
    const auto begin=std::chrono::steady_clock::now();
    struct Timing {double& result;std::chrono::steady_clock::time_point start;~Timing(){result=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}} timing{m_Statistics.lightGizmoMs,begin};
    if(m_LightPointerCaptured&&!ImGui::IsMouseDown(0)){FinishTransaction(app);m_LightPointerCaptured=false;}
    if(app.IsEditorPlayMode()||m_GameView)return;
    if(m_ModalActive){if(m_LightPointerCaptured){CancelTransaction(app);m_LightPointerCaptured=false;}return;}
    if(!m_ShowLightGizmos||size.x<=1||size.y<=1)return;
    auto camera=GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),size.x/size.y,app.GetCameraNearPlane(),app.GetCameraFarPlane());
    auto& world=app.GetWorld();std::vector<Occluder> occluders;
    // One resource lookup per distinct mesh in this draw, not per instance.
    // Frame-local ownership observes unload, project switches and hot reload.
    std::unordered_map<std::string,ResourceHandle<MeshResource>> frameMeshes;
    if(app.GetResourceManager())world.ForEach<ecs::TransformComponent,ecs::MeshRendererComponent>([&](ecs::Entity e,auto& t,auto& m){
        if(!m.visible||!EditorEntityVisible(app,e))return;
        auto [it,inserted]=frameMeshes.try_emplace(m.meshPath);
        if(inserted)it->second=app.GetResourceManager()->Get<MeshResource>(m.meshPath);
        auto mesh=it->second;if(!mesh||!mesh->IsLoaded()||mesh->GetData().meshData.vertices.empty())return;
        auto pose=app.GetRenderInterpolation().ResolveWorld(world,e);auto bounds=Bounds(mesh,pose,true);
        if(bounds.inverse!=std::array<float,12>{})occluders.push_back(std::move(bounds));
    });
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
    auto project=[&](V p)->std::optional<ImVec2>{auto q=ProjectGizmo(p,camera);if(!q)return {};return ImVec2(origin.x+(*q)[0]*size.x,origin.y+(*q)[1]*size.y);};
    bool xraySegments=false;
    auto line=[&](V a,V b,ImU32 color){
        for(int part=0;part<4;++part){V from=Add(a,Scale(Sub(b,a),part*.25f)),to=Add(a,Scale(Sub(b,a),(part+1)*.25f));
            bool hidden=Occluded(app.GetCameraPosition(),Add(from,Scale(Sub(to,from),.5f)),occluders);if(hidden&&!xraySegments)continue;
            if(hidden)to=Add(from,Scale(Sub(to,from),.55f));auto q=ProjectEditorLine(from,to,camera);if(!q)continue;
            auto tint=hidden?(color&0x00ffffffu)|(110u<<24):color;
            draw->AddLine(ImVec2(origin.x+(*q)[0]*size.x,origin.y+(*q)[1]*size.y),ImVec2(origin.x+(*q)[2]*size.x,origin.y+(*q)[3]*size.y),tint,hidden?1.f:1.5f);
        }
    };
    ecs::Entity hit;float hitZ=INFINITY;
    world.ForEach<ecs::TransformComponent>([&](ecs::Entity entity,auto& transform){
        if(!EditorEntityVisible(app,entity))return;
        LightType type;auto* light=ecs::GetLightParameters(world,entity,&type);if(!light)return;
        auto pose=app.GetRenderInterpolation().ResolveWorld(world,entity);auto q=ProjectGizmo(pose.position,camera);const bool selected=IsSelected(entity);if(!q&&!selected)return;
        xraySegments=m_LightXrayMode==2||(m_LightXrayMode==1&&selected);
        bool iconHidden=Occluded(app.GetCameraPosition(),pose.position,occluders);if(iconHidden&&!xraySegments&&!selected)return;
        auto p=SanitizeLight(*light);ImVec2 icon=q?*project(pose.position):ImVec2{};
        ImU32 color=selected?IM_COL32(255,240,80,255):ImGui::ColorConvertFloat4ToU32(ImVec4(LinearToSrgb(p.color[0]),LinearToSrgb(p.color[1]),LinearToSrgb(p.color[2]),p.enabled?1.f:.4f));
        if(q&&(!iconHidden||xraySegments)){if(iconHidden)color=(color&0x00ffffffu)|(160u<<24);draw->AddCircle(icon,7,color,16,2);if(selected)draw->AddCircle(icon,11,color,20,1);
        if(type==LightType::Directional){for(int j=0;j<8;++j){float a=j*.785398f;draw->AddLine(ImVec2(icon.x+9*std::cos(a),icon.y+9*std::sin(a)),ImVec2(icon.x+13*std::cos(a),icon.y+13*std::sin(a)),color,1.5f);}}
        else if(type==LightType::Point){draw->AddLine(ImVec2(icon.x-4,icon.y+8),ImVec2(icon.x+4,icon.y+8),color,2);}
        else {draw->AddTriangle(ImVec2(icon.x-6,icon.y-5),ImVec2(icon.x+6,icon.y-5),ImVec2(icon.x,icon.y+6),color,1.5f);}
        auto mouse=ImGui::GetMousePos();if(CanViewportSelect(app,entity)&&(!iconHidden||selected&&xraySegments)&&HitLightIcon(mouse.x-origin.x,mouse.y-origin.y,(*q)[0],(*q)[1],size.x,size.y)&&(*q)[2]<hitZ){hit=entity;hitZ=(*q)[2];}
        }
        if(!selected)return;
        auto d=ecs::LightDirection(pose);V right=std::abs(d.y)<.95f?Cross({0,1,0},d):Cross({1,0,0},d);right=Scale(right,1/std::sqrt(Dot(right,right)));V up=Cross(d,right);
        if(type==LightType::Directional){line(pose.position,Add(pose.position,Scale(d,2)),color);return;}
        if(type==LightType::Point){for(int plane=0;plane<3;++plane)for(int j=0;j<48;++j){auto point=[&](int k){float a=k*6.2831853f/48,x=std::cos(a)*p.range,y=std::sin(a)*p.range;return Add(pose.position,plane==0?V{x,y,0}:plane==1?V{x,0,y}:V{0,x,y});};line(point(j),point(j+1),color);}}
        else {for(float angle:{p.innerAngle,p.outerAngle}){V tip=Add(pose.position,Scale(d,p.range*std::cos(angle*.0174532925f)));float radius=p.range*std::sin(angle*.0174532925f);
            for(int j=0;j<48;++j){auto ring=[&](int k){float a=k*6.2831853f/48;return Add(tip,Add(Scale(right,radius*std::cos(a)),Scale(up,radius*std::sin(a))));};line(ring(j),ring(j+1),color);if(j%12==0)line(pose.position,ring(j),color);}}}
        // Range and cone handles edit the component, never Transform or physics.
        auto handle=[&](V base,V axis,float value,int which){
            if(m_Selection.items.size()>1||((ImGui::GetIO().KeyCtrl||ImGui::GetIO().KeyShift)&&ImGui::IsMouseClicked(0))||ImGuizmo::IsUsing()||(!m_LightPointerCaptured&&(app.IsCameraControlActive()||ImGui::IsMouseDown(1))))return;
            auto start=project(base),end=project(Add(base,Scale(axis,value)));if(!start||!end)return;
            if(end->x<origin.x||end->y<origin.y||end->x>=origin.x+size.x||end->y>=origin.y+size.y)return;
            bool hidden=Occluded(app.GetCameraPosition(),Add(base,Scale(axis,value)),occluders);if(hidden&&!xraySegments)return;
            float dx=end->x-start->x,dy=end->y-start->y,length=std::sqrt(dx*dx+dy*dy);if(length<8||value<.001f)return;
            const float dpi=ImGui::GetStyle().FontScaleDpi;draw->AddRectFilled(ImVec2(end->x-4*dpi,end->y-4*dpi),ImVec2(end->x+4*dpi,end->y+4*dpi),color);
            ImGui::PushID(int(entity.index));ImGui::PushID(which);ImGui::SetCursorScreenPos(ImVec2(end->x-6*dpi,end->y-6*dpi));ImGui::InvisibleButton("LightHandle",ImVec2(12*dpi,12*dpi));
            m_LightHandleHovered|=ImGui::IsItemHovered()&&m_ViewportHovered;
            if(ImGui::IsItemHovered()&&m_ViewportHovered)draw->AddRect(ImVec2(end->x-6*dpi,end->y-6*dpi),ImVec2(end->x+6*dpi,end->y+6*dpi),IM_COL32(255,255,220,255));
            if(ImGui::IsItemActivated()&&m_ViewportHovered){PushUndoBeforeChange(app,"Light range/cone handle");m_LightPointerCaptured=true;}
            if(m_LightPointerCaptured&&ImGui::IsItemActive()&&ImGui::IsMouseDragging(0)){
                auto delta=ImGui::GetIO().MouseDelta;float next=std::max(.001f,value+(delta.x*dx+delta.y*dy)/length*(value/length));
                if(which==0)light->range=next;else {float angle=std::asin(std::min(1.f,next/p.range))*57.2957795f;if(which==1)light->innerAngle=angle;else light->outerAngle=angle;}
                *light=SanitizeLight(*light);
            }
            if(ImGui::IsItemDeactivated()&&!ImGui::IsMouseDown(0)){FinishTransaction(app);m_LightPointerCaptured=false;}
            ImGui::PopID();ImGui::PopID();
        };
        V cameraRight{camera.view[0],camera.view[4],camera.view[8]};
        handle(pose.position,type==LightType::Point?cameraRight:d,p.range,0);
        if(type==LightType::Spot){for(int which=1;which<=2;++which){float a=(which==1?p.innerAngle:p.outerAngle)*.0174532925f;V tip=Add(pose.position,Scale(d,p.range*std::cos(a)));handle(tip,right,p.range*std::sin(a),which);}}
    });
    m_SpecialIconHovered |= hit.IsValid();
    if(hit.IsValid()&&!app.IsCameraControlActive()&&!ImGui::IsMouseDown(1)&&m_ViewportHovered&&!m_InputTextActive&&!m_ModalActive&&ImGui::IsMouseClicked(0)&&(!m_Selection.active.IsValid()||!ImGuizmo::IsOver()||ImGui::GetIO().KeyCtrl||ImGui::GetIO().KeyShift)&&!ImGuizmo::IsUsing()&&!ImGui::IsAnyItemActive()){FinishTransaction(app);SelectViewportEntity(app,hit,ImGui::GetIO().KeyCtrl,ImGui::GetIO().KeyShift);m_LightIconClicked=true;}
    draw->PopClipRect();
}
}

namespace editor {
void EditorLayer::DrawPlayerStartGizmos(Application& app,const ImVec2& origin,const ImVec2& size){
    if(app.IsEditorPlayMode()||m_GameView||size.x<=1||size.y<=1)return;
    const auto camera=GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),size.x/size.y,app.GetCameraNearPlane(),app.GetCameraFarPlane());
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
    auto project=[&](V p)->std::optional<ImVec2>{auto q=ProjectGizmo(p,camera);if(!q)return {};return ImVec2(origin.x+(*q)[0]*size.x,origin.y+(*q)[1]*size.y);};
    auto line=[&](V a,V b,ImU32 color){auto q=ProjectEditorLine(a,b,camera);if(!q)return;ImVec2 x(origin.x+(*q)[0]*size.x,origin.y+(*q)[1]*size.y),y(origin.x+(*q)[2]*size.x,origin.y+(*q)[3]*size.y);draw->AddLine(x,y,IM_COL32(10,15,20,230),4);draw->AddLine(x,y,color,2);};
    ecs::Entity hit;float nearest=INFINITY;const auto mouse=ImGui::GetMousePos();
    app.GetWorld().ForEach<ecs::PlayerStartComponent,ecs::TransformComponent>([&](ecs::Entity e,auto& start,auto& local){
        const auto t=app.GetWorld().WorldTransform(e);
        if(!EditorEntityVisible(app,e))return;
        const bool selected=IsSelected(e);const ImU32 color=start.enabled?IM_COL32(80,235,255,255):IM_COL32(155,160,165,255);
        const auto half=ecs::PlayerBodyHalfExtents(start);
        const bool prefab=start.spawnMode==2||start.spawnMode==3||(start.spawnMode==0&&!start.playerPrefab.empty());
        const auto center=ecs::PlayerBodyCenter(t.position,half.y),eye=prefab?t.position:ecs::PlayerEyePosition(center,ecs::PlayerEyeHeight(start),half.y);
        const auto q=ecs::RenderRotation(t.rotation);
        V forward=prefab?V{2*(q.x*q.z+q.w*q.y),2*(q.y*q.z-q.w*q.x),1-2*(q.x*q.x+q.y*q.y)}:PlayerStartGizmoDirection(t);
        V tip=Add(eye,Scale(forward,1.1f));line(eye,tip,color);
        V right{1-2*(q.y*q.y+q.z*q.z),2*(q.x*q.y+q.w*q.z),2*(q.x*q.z-q.w*q.y)};
        line(tip,Add(Sub(tip,Scale(forward,.2f)),Scale(right,.12f)),color);line(tip,Sub(Sub(tip,Scale(forward,.2f)),Scale(right,.12f)),color);
        auto p=project(t.position);
        if(p){
            // Constant screen-size person silhouette with a contrasting outline.
            draw->AddCircleFilled(ImVec2(p->x,p->y-24),6,IM_COL32(10,15,20,240));draw->AddCircleFilled(ImVec2(p->x,p->y-24),4,color);
            auto stroke=[&](ImVec2 a,ImVec2 b){draw->AddLine(a,b,IM_COL32(10,15,20,240),6);draw->AddLine(a,b,color,3);};
            stroke(ImVec2(p->x,p->y-18),ImVec2(p->x,p->y-9));stroke(ImVec2(p->x-8,p->y-15),ImVec2(p->x+8,p->y-15));stroke(ImVec2(p->x,p->y-9),ImVec2(p->x-6,p->y));stroke(ImVec2(p->x,p->y-9),ImVec2(p->x+6,p->y));
            if(selected)draw->AddText(ImVec2(p->x+12,p->y-28),color,prefab?"PlayerStart (prefab origin)":"PlayerStart (feet)");
            auto depth=ProjectGizmo(t.position,camera);
            if(CanViewportSelect(app,e)&&mouse.x>=p->x-12&&mouse.x<=p->x+12&&mouse.y>=p->y-32&&mouse.y<=p->y+5&&(*depth)[2]<nearest){hit=e;nearest=(*depth)[2];}
        }
        if(!selected)return;
        if(prefab){line(Add(t.position,V{-.2f,0,0}),Add(t.position,V{.2f,0,0}),color);line(Add(t.position,V{0,0,-.2f}),Add(t.position,V{0,0,.2f}),color);return;}
        std::array<V,8> corners;auto h=half;
        for(int i=0;i<8;++i)corners[i]=ecs::PlayerBoxPoint(t,{i&1?h.x:-h.x,i&2?h.y:-h.y,i&4?h.z:-h.z},h.y);
        for(int i=0;i<8;++i)for(int bit:{1,2,4})if(!(i&bit))line(corners[i],corners[i|bit],color);
        line(Add(t.position,V{-.2f,0,0}),Add(t.position,V{.2f,0,0}),color);line(Add(t.position,V{0,0,-.2f}),Add(t.position,V{0,0,.2f}),color);
        if(auto eyes=project(eye)){draw->AddCircleFilled(*eyes,5,IM_COL32(10,15,20,240));draw->AddCircleFilled(*eyes,3,IM_COL32(255,220,90,255));draw->AddText(ImVec2(eyes->x+7,eyes->y),IM_COL32(255,220,90,255),"Eyes");}
    });
    m_SpecialIconHovered |= hit.IsValid();
    if(hit.IsValid()&&!m_LightIconClicked&&!app.IsCameraControlActive()&&!ImGui::IsMouseDown(1)&&m_ViewportHovered&&!m_InputTextActive&&!m_ModalActive&&!m_LightPointerCaptured&&ImGui::IsMouseClicked(0)&&(!m_Selection.active.IsValid()||!ImGuizmo::IsOver()||ImGui::GetIO().KeyCtrl||ImGui::GetIO().KeyShift)&&!ImGuizmo::IsUsing()&&!ImGui::IsAnyItemActive()){
        FinishTransaction(app);SelectViewportEntity(app,hit,ImGui::GetIO().KeyCtrl,ImGui::GetIO().KeyShift);m_LightIconClicked=true;
    }
    draw->PopClipRect();
}
void EditorLayer::DrawViewportOverlays(Application& app,const ImVec2& origin,const ImVec2& size){
    if(m_LightPointerCaptured&&!ImGui::IsMouseDown(0)){FinishTransaction(app);m_LightPointerCaptured=false;}
    if(app.IsEditorPlayMode()){m_BoxSelection.reset();return;}
    if(size.x<=1||size.y<=1){m_BoxSelection.reset();return;}const auto camera=GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),size.x/size.y,app.GetCameraNearPlane(),app.GetCameraFarPlane());
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
    auto project=[&](V p)->std::optional<ImVec2>{auto v=ProjectGizmo(p,camera);if(!v)return {};return ImVec2(origin.x+(*v)[0]*size.x,origin.y+(*v)[1]*size.y);};
    auto line=[&](V a,V b,ImU32 color){auto q=ProjectEditorLine(a,b,camera);if(q)draw->AddLine(ImVec2(origin.x+(*q)[0]*size.x,origin.y+(*q)[1]*size.y),ImVec2(origin.x+(*q)[2]*size.x,origin.y+(*q)[3]*size.y),color);};
    auto& world=app.GetWorld();auto* resources=app.GetResourceManager();
    const auto gestureId=ImGui::GetCurrentWindow()->GetID("ViewportBoxSelection");
    const bool otherInput=m_InputTextActive&&ImGui::GetActiveID()!=gestureId;
    if(m_BoxSelection&&(m_SelectionLocked||m_BoxSelection->viewport!=GetViewportBounds()||app.IsCameraControlActive()||otherInput||m_ModalActive||ImGui::GetDragDropPayload()||ImGuizmo::IsUsing()||ImGui::IsMouseDown(1)))m_BoxSelection.reset();
    const bool canvasHovered=m_ViewportHovered||(!BlocksGameplayInput()&&ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)&&ImGui::IsMouseHoveringRect(origin,ImVec2(origin.x+size.x,origin.y+size.y)));
    if(!m_VertexSourcePicking&&!m_BoxSelection&&resources&&!m_SelectionLocked&&!ImGui::GetDragDropPayload()&&!m_PlaneScaleCaptured&&!app.IsCameraControlActive()&&!ImGui::IsMouseDown(1)&&!m_LightHandleHovered&&!m_LightPointerCaptured&&!m_LightIconClicked&&canvasHovered&&!m_InputTextActive&&!m_ModalActive&&ImGui::IsMouseClicked(0)&&(!m_Selection.active.IsValid()||!ImGuizmo::IsOver()||ImGui::GetIO().KeyCtrl||ImGui::GetIO().KeyShift)&&!ImGuizmo::IsUsing()&&!BlocksGameplayInput()){
        auto mouse=ImGui::GetMousePos();float x=((mouse.x-origin.x)/size.x*2-1)/camera.projection[0],y=(1-(mouse.y-origin.y)/size.y*2)/camera.projection[5];
        V ray{camera.view[0]*x+camera.view[1]*y+camera.view[2],camera.view[4]*x+camera.view[5]*y+camera.view[6],camera.view[8]*x+camera.view[9]*y+camera.view[10]};ray=Scale(ray,1/std::sqrt(Dot(ray,ray)));
        const auto hit=PickViewportMesh(app,app.GetCameraPosition(),ray,app.GetCameraFarPlane());
        if(hit.IsValid())SelectViewportEntity(app,hit,ImGui::GetIO().KeyCtrl,ImGui::GetIO().KeyShift);
        else {
            const auto& io=ImGui::GetIO();
            // Own this gesture instead of ImGui's background window MoveId.
            // This also keeps a floating Viewport stationary while boxing.
            auto* window=ImGui::GetCurrentWindow();ImGui::SetActiveID(window->GetID("ViewportBoxSelection"),window);ImGui::GetCurrentContext()->MovingWindow=nullptr;
            m_BoxSelection=BoxSelection{{mouse.x,mouse.y},GetViewportBounds(),world.GetIdentity(),io.KeyAlt?SelectionOperation::Subtract:((io.KeyCtrl||io.KeyShift)?SelectionOperation::Add:SelectionOperation::Replace)};
        }
    }
    if(m_BoxSelection){
        ImGui::KeepAliveID(gestureId);
        auto& box=*m_BoxSelection;const auto mouse=ImGui::GetMousePos();
        const float dx=mouse.x-box.start[0],dy=mouse.y-box.start[1],threshold=ImGui::GetIO().MouseDragThreshold;
        box.dragging|=dx*dx+dy*dy>=threshold*threshold;
        const ImVec2 start{box.start[0],box.start[1]},end{std::clamp(mouse.x,origin.x,origin.x+size.x),std::clamp(mouse.y,origin.y,origin.y+size.y)};
        const ImVec2 lo{std::min(start.x,end.x),std::min(start.y,end.y)},hi{std::max(start.x,end.x),std::max(start.y,end.y)};
        if(box.dragging){draw->AddRectFilled(lo,hi,IM_COL32(65,165,240,35));draw->AddRect(lo,hi,IM_COL32(90,190,255,235),0,0,ImGui::GetStyle().FontScaleDpi);}
        if(ImGui::IsMouseReleased(0)){
            const auto operation=box.operation;const bool dragging=box.dragging;m_BoxSelection.reset();if(ImGui::GetActiveID()==gestureId)ImGui::ClearActiveID();
            if(dragging)SelectViewportRectangle(app,{(lo.x-origin.x)/size.x,(lo.y-origin.y)/size.y,(hi.x-origin.x)/size.x,(hi.y-origin.y)/size.y},operation);
            else if(operation==SelectionOperation::Replace)SelectViewportEntity(app,{},false,false);
        }else if(!ImGui::IsMouseDown(0))m_BoxSelection.reset();
    }
    if(m_GameView){draw->PopClipRect();return;}
    for(auto selected:m_Selection.items)if(resources&&world.IsAlive(selected)&&EditorEntityVisible(app,selected))if(auto* m=world.GetComponent<ecs::MeshRendererComponent>(selected))if(auto* t=world.GetComponent<ecs::TransformComponent>(selected)){
        auto mesh=resources->Get<MeshResource>(m->meshPath);if(mesh&&mesh->IsLoaded()&&!mesh->GetData().meshData.vertices.empty()){
            auto pose=app.GetRenderInterpolation().ResolveWorld(world,selected);auto bounds=Bounds(mesh,pose);std::array<float,16> model;ecs::BuildRenderModelMatrix(model.data(),pose);V corners[8];
            for(int i=0;i<8;++i){V p{i&1?bounds.max.x:bounds.min.x,i&2?bounds.max.y:bounds.min.y,i&4?bounds.max.z:bounds.min.z};corners[i]={model[0]*p.x+model[4]*p.y+model[8]*p.z+model[12],model[1]*p.x+model[5]*p.y+model[9]*p.z+model[13],model[2]*p.x+model[6]*p.y+model[10]*p.z+model[14]};}
            for(int i=0;i<8;++i)for(int bit:{1,2,4})if(!(i&bit))line(corners[i],corners[i|bit],IM_COL32(255,225,80,255));
        }
    }
    draw->PopClipRect();
}
}

namespace editor {
ecs::Entity EditorLayer::PickViewportMesh(Application& app,V origin,V direction,float maximum)const{
    auto* resources=app.GetResourceManager();if(!resources)return {};
    const float length=std::sqrt(Dot(direction,direction));if(!std::isfinite(length)||length<1e-10f||!std::isfinite(maximum)||maximum<=0)return {};
    direction=Scale(direction,1/length);ecs::Entity hit;
    app.GetWorld().ForEach<ecs::TransformComponent,ecs::MeshRendererComponent>([&](ecs::Entity e,auto& t,auto& m){
        if(!m.visible||!CanViewportSelect(app,e))return;
        auto mesh=resources->Get<MeshResource>(m.meshPath);if(!mesh||!mesh->IsLoaded()||mesh->GetData().meshData.vertices.empty())return;
        auto bounds=Bounds(mesh,app.GetRenderInterpolation().ResolveWorld(app.GetWorld(),e));if(bounds.inverse==std::array<float,12>{})return;
        const float distance=HitDistance(origin,direction,bounds,maximum);if(distance<maximum){maximum=distance;hit=e;}
    });return hit;
}
std::optional<EditorLayer::SurfaceHit> EditorLayer::RaycastPlacement(Application& app,V origin,V direction,float maximum,const std::vector<ecs::Entity>& ignore)const{
    auto* resources=app.GetResourceManager();if(!resources||!std::isfinite(maximum)||maximum<=0)return {};
    const float length=std::sqrt(Dot(direction,direction));if(!std::isfinite(length)||length<1e-10f)return {};direction=Scale(direction,1/length);
    std::optional<SurfaceHit> hit;
    app.GetWorld().ForEach<ecs::TransformComponent,ecs::MeshRendererComponent>([&](ecs::Entity e,auto& t,auto& m){
        if(!m.visible||!EditorEntityVisible(app,e)||std::find(ignore.begin(),ignore.end(),e)!=ignore.end())return;
        auto mesh=resources->Get<MeshResource>(m.meshPath);if(!mesh||!mesh->IsLoaded()||mesh->GetData().placeholder||mesh->GetData().meshData.vertices.empty())return;
        const auto bounds=Bounds(mesh,ecs::RenderPose::From(app.GetWorld().WorldTransform(e)));if(bounds.inverse==std::array<float,12>{})return;
        V normal;const float distance=HitDistance(origin,direction,bounds,maximum,&normal);
        if(distance<maximum&&distance>=0){maximum=distance;hit=SurfaceHit{e,Add(origin,Scale(direction,distance)),normal,distance};}
    });return hit;
}
ecs::Vec3 EditorLayer::PlacementPoint(Application& app,float u,float v)const{
    auto eye=app.GetCameraPosition();if(!std::isfinite(u)||!std::isfinite(v))return eye;
    u=std::clamp(u,0.f,1.f);v=std::clamp(v,0.f,1.f);
    const auto camera=GizmoCamera(eye,app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),float(std::max(1,m_ViewportPixelWidth))/std::max(1,m_ViewportPixelHeight),app.GetCameraNearPlane(),app.GetCameraFarPlane());
    float x=(u*2-1)/camera.projection[0],y=(1-v*2)/camera.projection[5];
    V ray{camera.view[0]*x+camera.view[1]*y+camera.view[2],camera.view[4]*x+camera.view[5]*y+camera.view[6],camera.view[8]*x+camera.view[9]*y+camera.view[10]};ray=Scale(ray,1/std::sqrt(Dot(ray,ray)));
    float nearest=app.GetCameraFarPlane();bool hit=false;
    if(auto* resources=app.GetResourceManager())app.GetWorld().ForEach<ecs::TransformComponent,ecs::MeshRendererComponent>([&](ecs::Entity e,auto& t,auto& m){
        if(!m.visible||!EditorEntityVisible(app,e))return;auto mesh=resources->Get<MeshResource>(m.meshPath);if(!mesh||!mesh->IsLoaded()||mesh->GetData().meshData.vertices.empty())return;
        auto bounds=Bounds(mesh,app.GetRenderInterpolation().ResolveWorld(app.GetWorld(),e));if(bounds.inverse==std::array<float,12>{})return;
        float distance=HitDistance(eye,ray,bounds,nearest);if(distance>app.GetCameraNearPlane()&&distance<nearest){nearest=distance;hit=true;}
    });
    if(!hit){float plane=std::abs(ray.y)>1e-6f?-eye.y/ray.y:-1;nearest=plane>app.GetCameraNearPlane()&&plane<nearest?plane:std::min(10.f,nearest*.5f);}
    return Add(eye,Scale(ray,nearest));
}
ecs::Entity EditorLayer::PlaceModel(Application& app,const std::string& key,ecs::Vec3 point){
    if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z)){m_EditorError="Invalid placement position";return {};}
    auto entity=CreateModelEntity(app,key,std::filesystem::u8path(key).stem().string());if(!entity.IsValid())return {};
    app.GetWorld().GetComponent<ecs::TransformComponent>(entity)->position=point;app.NotifyEditorMutation(entity);
    if(auto* resources=app.GetResourceManager())resources->LoadAsync<MeshResource>(key);return entity;
}
}

namespace editor {
void EditorLayer::SelectViewportRectangle(Application& app,std::array<float,4> rectangle,SelectionOperation operation){
    PruneSelection(app);if(m_SelectionLocked||app.IsEditorPlayMode())return;
    for(float v:rectangle)if(!std::isfinite(v))return;
    if(rectangle[0]>rectangle[2])std::swap(rectangle[0],rectangle[2]);
    if(rectangle[1]>rectangle[3])std::swap(rectangle[1],rectangle[3]);
    const auto viewport=GetViewportBounds();const float width=viewport[2]-viewport[0],height=viewport[3]-viewport[1];
    if(width<=1||height<=1)return;
    const auto camera=GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),width/height,app.GetCameraNearPlane(),app.GetCameraFarPlane());
    auto& world=app.GetWorld();auto* resources=app.GetResourceManager();std::vector<ecs::Entity> hits;
    // Clip the twelve bounds edges against depth before perspective division.
    // Unlike point projection this also handles offscreen pivots and bounds
    // crossing the near plane, without selecting objects behind the camera.
    auto projectedBounds=[&](const std::array<V,8>& corners)->std::optional<std::array<float,4>>{
        std::array<V,8> points;const auto& m=camera.view;
        for(int i=0;i<8;++i){auto p=corners[i];points[i]={m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};}
        std::array<float,4> result{INFINITY,INFINITY,-INFINITY,-INFINITY};
        for(int i=0;i<8;++i)for(int bit:{1,2,4})if(!(i&bit)){
            const auto a=points[i],b=points[i|bit];double lo=0,hi=1,dz=double(b.z)-a.z;
            if(std::abs(dz)<1e-10){if(a.z<camera.nearPlane||a.z>camera.farPlane)continue;}
            else {double l=(camera.nearPlane-a.z)/dz,h=(camera.farPlane-a.z)/dz;if(l>h)std::swap(l,h);lo=std::max(lo,l);hi=std::min(hi,h);if(lo>hi)continue;}
            for(double t:{lo,hi}){const double z=a.z+dz*t;const float u=float(.5+.5*camera.projection[0]*(a.x+(b.x-a.x)*t)/z),v=float(.5-.5*camera.projection[5]*(a.y+(b.y-a.y)*t)/z);
                if(!std::isfinite(u)||!std::isfinite(v))continue;result[0]=std::min(result[0],u);result[1]=std::min(result[1],v);result[2]=std::max(result[2],u);result[3]=std::max(result[3],v);}
        }
        if(result[0]>result[2])return {};return result;
    };
    world.ForEach<ecs::TransformComponent>([&](ecs::Entity e,const auto&){
        if(!CanViewportSelect(app,e))return;
        const auto pose=app.GetRenderInterpolation().ResolveWorld(world,e);std::array<V,8> corners;std::optional<std::array<float,4>> projected;
        if(auto* renderer=world.GetComponent<ecs::MeshRendererComponent>(e)){
            if(!renderer->visible||!resources)return;auto mesh=resources->Get<MeshResource>(renderer->meshPath);
            if(!mesh||!mesh->IsLoaded()||mesh->GetData().meshData.vertices.empty())return;
            const auto bounds=Bounds(mesh,pose);std::array<float,16> model;ecs::BuildRenderModelMatrix(model.data(),pose);
            for(int i=0;i<8;++i){V p{i&1?bounds.max.x:bounds.min.x,i&2?bounds.max.y:bounds.min.y,i&4?bounds.max.z:bounds.min.z};corners[i]={model[0]*p.x+model[4]*p.y+model[8]*p.z+model[12],model[1]*p.x+model[5]*p.y+model[9]*p.z+model[13],model[2]*p.x+model[6]*p.y+model[10]*p.z+model[14]};}
            projected=projectedBounds(corners);
        }else if(auto* collider=world.GetComponent<ecs::ColliderComponent>(e)){
            const auto center=Add(pose.position,collider->offset),half=collider->halfExtents;
            for(int i=0;i<8;++i)corners[i]=Add(center,V{(i&1?1:-1)*half.x,(i&2?1:-1)*half.y,(i&4?1:-1)*half.z});projected=projectedBounds(corners);
        }else if(auto* start=world.GetComponent<ecs::PlayerStartComponent>(e);start&&(start->spawnMode==1||(start->spawnMode==0&&start->playerPrefab.empty()))){
            std::array<float,16> model;ecs::BuildRenderModelMatrix(model.data(),pose);
            for(int i=0;i<8;++i){V p{(i&1?1.f:-1.f)*.3f,i&2?start->characterHeight:0.f,(i&4?1.f:-1.f)*.3f};corners[i]={model[0]*p.x+model[4]*p.y+model[8]*p.z+model[12],model[1]*p.x+model[5]*p.y+model[9]*p.z+model[13],model[2]*p.x+model[6]*p.y+model[10]*p.z+model[14]};}projected=projectedBounds(corners);
        }else if(auto point=ProjectGizmo(pose.position,camera)){
            // Global lights/fog and empty entities have no geometry bounds;
            // use their existing editor marker's screen footprint.
            const float radius=12*ImGui::GetStyle().FontScaleDpi;projected=std::array<float,4>{(*point)[0]-radius/width,(*point)[1]-radius/height,(*point)[0]+radius/width,(*point)[1]+radius/height};
        }
        if(projected&&(*projected)[0]<=rectangle[2]&&(*projected)[2]>=rectangle[0]&&(*projected)[1]<=rectangle[3]&&(*projected)[3]>=rectangle[1])hits.push_back(e);
    });
    // World iteration order gives deterministic Active; subtraction keeps it
    // when possible and otherwise falls back to the last remaining entity.
    EndGroupTransform(app);FinishTransaction(app);
    if(operation==SelectionOperation::Replace)m_Selection.Clear();
    for(auto e:hits)if(operation==SelectionOperation::Subtract){if(m_Selection.Contains(e))m_Selection.Toggle(e);}else m_Selection.Add(e);
    m_Selection.Prune(world);m_PropertyDrafts.clear();m_GameView=false;
}
}
