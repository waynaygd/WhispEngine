#define NOMINMAX
#include "EditorLayer.h"
#include "LightGizmoMath.h"
#include "../core/Application.h"
#include "../ecs/LightGather.h"
#include "../render/ColorPipeline.h"
#include "../resources/ResourceManager.h"
#include <imgui.h>
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
    if(!ecs::GetLightParameters(app.GetWorld(),m_SelectedEntity))return;
    auto before=CaptureSelectedEntity(app);app.DestroySceneEntity(m_SelectedEntity);
    CommitUndo(app,"Delete Light",before);m_SelectedEntity={};
}
namespace
{
using V=ecs::Vec3;
V Add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V Sub(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V Scale(V a,float b){return {a.x*b,a.y*b,a.z*b};}
float Dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V Cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
struct Occluder {ResourceHandle<MeshResource> mesh;std::array<float,12> inverse;V translation,min,max;};
V Inverse(V p,const std::array<float,12>& n){return {n[0]*p.x+n[4]*p.y+n[8]*p.z,n[1]*p.x+n[5]*p.y+n[9]*p.z,n[2]*p.x+n[6]*p.y+n[10]*p.z};}
float HitDistance(V eye,V direction,const Occluder& o,float maximum)
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
        V q=Cross(delta,e1);float v=inv*Dot(d,q);if(v<0||u+v>1)continue;float t=inv*Dot(e2,q);if(t>=0&&t<nearest)nearest=t;
    }return nearest;
}
bool Occluded(V eye,V target,const std::vector<Occluder>& objects){
    V delta=Sub(target,eye);float distance=std::sqrt(Dot(delta,delta));if(distance<1e-5f)return false;
    V direction=Scale(delta,1/distance);for(const auto& o:objects)if(HitDistance(eye,direction,o,distance-.03f)<distance-.03f)return true;return false;
}
Occluder Bounds(ResourceHandle<MeshResource> mesh,const ecs::RenderPose& pose){
    std::array<float,16> model;ecs::BuildRenderModelMatrix(model.data(),pose);auto inv=BuildNormalMatrix(model);
    struct CachedBounds {std::weak_ptr<Resource<MeshResource>> owner;std::uint64_t version=0;V lo,hi;};
    static thread_local std::unordered_map<const void*,CachedBounds> cache;
    if(cache.size()>4096)cache.clear();auto& c=cache[mesh.get()];
    if(c.owner.lock()!=mesh||c.version!=mesh->GetVersion()){
        c.owner=mesh;c.version=mesh->GetVersion();c.lo={INFINITY,INFINITY,INFINITY};c.hi={-INFINITY,-INFINITY,-INFINITY};
        for(const auto& v:mesh->GetData().meshData.vertices){c.lo.x=std::min(c.lo.x,v.position[0]);c.lo.y=std::min(c.lo.y,v.position[1]);c.lo.z=std::min(c.lo.z,v.position[2]);c.hi.x=std::max(c.hi.x,v.position[0]);c.hi.y=std::max(c.hi.y,v.position[1]);c.hi.z=std::max(c.hi.z,v.position[2]);}
    }V lo=c.lo,hi=c.hi;
    return {mesh,inv,pose.position,lo,hi};
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
    if(app.GetResourceManager())world.ForEach<ecs::TransformComponent,ecs::MeshRendererComponent>([&](ecs::Entity e,auto& t,auto& m){
        if(!m.visible)return;auto mesh=app.GetResourceManager()->Get<MeshResource>(m.meshPath);if(!mesh||!mesh->IsLoaded()||mesh->GetData().meshData.vertices.empty())return;
        auto pose=app.GetRenderInterpolation().Resolve(e,t);auto bounds=Bounds(mesh,pose);
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
        LightType type;auto* light=ecs::GetLightParameters(world,entity,&type);if(!light)return;
        auto pose=app.GetRenderInterpolation().Resolve(entity,transform);auto q=ProjectGizmo(pose.position,camera);const bool selected=entity==m_SelectedEntity;if(!q&&!selected)return;
        xraySegments=m_LightXrayMode==2||(m_LightXrayMode==1&&selected);
        bool iconHidden=Occluded(app.GetCameraPosition(),pose.position,occluders);if(iconHidden&&!xraySegments&&!selected)return;
        auto p=SanitizeLight(*light);ImVec2 icon=q?*project(pose.position):ImVec2{};
        ImU32 color=selected?IM_COL32(255,240,80,255):ImGui::ColorConvertFloat4ToU32(ImVec4(LinearToSrgb(p.color[0]),LinearToSrgb(p.color[1]),LinearToSrgb(p.color[2]),p.enabled?1.f:.4f));
        if(q&&(!iconHidden||xraySegments)){if(iconHidden)color=(color&0x00ffffffu)|(160u<<24);draw->AddCircle(icon,7,color,16,2);if(selected)draw->AddCircle(icon,11,color,20,1);
        if(type==LightType::Directional){for(int j=0;j<8;++j){float a=j*.785398f;draw->AddLine(ImVec2(icon.x+9*std::cos(a),icon.y+9*std::sin(a)),ImVec2(icon.x+13*std::cos(a),icon.y+13*std::sin(a)),color,1.5f);}}
        else if(type==LightType::Point){draw->AddLine(ImVec2(icon.x-4,icon.y+8),ImVec2(icon.x+4,icon.y+8),color,2);}
        else {draw->AddTriangle(ImVec2(icon.x-6,icon.y-5),ImVec2(icon.x+6,icon.y-5),ImVec2(icon.x,icon.y+6),color,1.5f);}
        auto mouse=ImGui::GetMousePos();if((!iconHidden||selected&&xraySegments)&&HitLightIcon(mouse.x-origin.x,mouse.y-origin.y,(*q)[0],(*q)[1],size.x,size.y)&&(*q)[2]<hitZ){hit=entity;hitZ=(*q)[2];}
        }
        if(!selected)return;
        auto d=ecs::LightDirection(pose);V right=std::abs(d.y)<.95f?Cross({0,1,0},d):Cross({1,0,0},d);right=Scale(right,1/std::sqrt(Dot(right,right)));V up=Cross(d,right);
        if(type==LightType::Directional){line(pose.position,Add(pose.position,Scale(d,2)),color);return;}
        if(type==LightType::Point){for(int plane=0;plane<3;++plane)for(int j=0;j<48;++j){auto point=[&](int k){float a=k*6.2831853f/48,x=std::cos(a)*p.range,y=std::sin(a)*p.range;return Add(pose.position,plane==0?V{x,y,0}:plane==1?V{x,0,y}:V{0,x,y});};line(point(j),point(j+1),color);}}
        else {for(float angle:{p.innerAngle,p.outerAngle}){V tip=Add(pose.position,Scale(d,p.range*std::cos(angle*.0174532925f)));float radius=p.range*std::sin(angle*.0174532925f);
            for(int j=0;j<48;++j){auto ring=[&](int k){float a=k*6.2831853f/48;return Add(tip,Add(Scale(right,radius*std::cos(a)),Scale(up,radius*std::sin(a))));};line(ring(j),ring(j+1),color);if(j%12==0)line(pose.position,ring(j),color);}}}
        // Range and cone handles edit the component, never Transform or physics.
        auto handle=[&](V base,V axis,float value,int which){
            if(ImGuizmo::IsUsing()||(!m_LightPointerCaptured&&(app.IsCameraControlActive()||ImGui::IsMouseDown(1))))return;
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
    if(hit.IsValid()&&!app.IsCameraControlActive()&&!ImGui::IsMouseDown(1)&&m_ViewportHovered&&!m_InputTextActive&&!m_ModalActive&&ImGui::IsMouseClicked(0)&&!ImGuizmo::IsOver()&&!ImGuizmo::IsUsing()&&!ImGui::IsAnyItemActive()){FinishTransaction(app);SelectEntityFromUser(app,hit);m_LightIconClicked=true;}
    draw->PopClipRect();
}
}

namespace editor {
void EditorLayer::DrawViewportOverlays(Application& app,const ImVec2& origin,const ImVec2& size){
    if(m_LightPointerCaptured&&!ImGui::IsMouseDown(0)){FinishTransaction(app);m_LightPointerCaptured=false;}
    if(app.IsEditorPlayMode())return;
    if(size.x<=1||size.y<=1)return;const auto camera=GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),size.x/size.y,app.GetCameraNearPlane(),app.GetCameraFarPlane());
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
    auto project=[&](V p)->std::optional<ImVec2>{auto v=ProjectGizmo(p,camera);if(!v)return {};return ImVec2(origin.x+(*v)[0]*size.x,origin.y+(*v)[1]*size.y);};
    auto line=[&](V a,V b,ImU32 color){auto q=ProjectEditorLine(a,b,camera);if(q)draw->AddLine(ImVec2(origin.x+(*q)[0]*size.x,origin.y+(*q)[1]*size.y),ImVec2(origin.x+(*q)[2]*size.x,origin.y+(*q)[3]*size.y),color);};
    auto& world=app.GetWorld();auto* resources=app.GetResourceManager();
    if(resources&&!m_PlaneScaleCaptured&&!app.IsCameraControlActive()&&!ImGui::IsMouseDown(1)&&!m_LightHandleHovered&&!m_LightPointerCaptured&&!m_LightIconClicked&&m_ViewportHovered&&!m_InputTextActive&&!m_ModalActive&&ImGui::IsMouseClicked(0)&&!ImGuizmo::IsOver()&&!ImGuizmo::IsUsing()&&!ImGui::IsAnyItemActive()){
        auto mouse=ImGui::GetMousePos();float x=((mouse.x-origin.x)/size.x*2-1)/camera.projection[0],y=(1-(mouse.y-origin.y)/size.y*2)/camera.projection[5];
        V ray{camera.view[0]*x+camera.view[1]*y+camera.view[2],camera.view[4]*x+camera.view[5]*y+camera.view[6],camera.view[8]*x+camera.view[9]*y+camera.view[10]};ray=Scale(ray,1/std::sqrt(Dot(ray,ray)));
        float nearest=app.GetCameraFarPlane();ecs::Entity hit;
        world.ForEach<ecs::TransformComponent,ecs::MeshRendererComponent>([&](ecs::Entity e,auto& t,auto& m){
            if(!m.visible)return;auto mesh=resources->Get<MeshResource>(m.meshPath);if(!mesh||!mesh->IsLoaded()||mesh->GetData().meshData.vertices.empty())return;
            auto bounds=Bounds(mesh,app.GetRenderInterpolation().Resolve(e,t));if(bounds.inverse==std::array<float,12>{})return;
            float distance=HitDistance(app.GetCameraPosition(),ray,bounds,nearest);if(distance<nearest){nearest=distance;hit=e;}
        });
        FinishTransaction(app);SelectEntityFromUser(app,hit);
    }
    if(m_GameView){draw->PopClipRect();return;}
    if(resources&&world.IsAlive(m_SelectedEntity))if(auto* m=world.GetComponent<ecs::MeshRendererComponent>(m_SelectedEntity))if(auto* t=world.GetComponent<ecs::TransformComponent>(m_SelectedEntity)){
        auto mesh=resources->Get<MeshResource>(m->meshPath);if(mesh&&mesh->IsLoaded()&&!mesh->GetData().meshData.vertices.empty()){
            auto pose=app.GetRenderInterpolation().Resolve(m_SelectedEntity,*t);auto bounds=Bounds(mesh,pose);std::array<float,16> model;ecs::BuildRenderModelMatrix(model.data(),pose);V corners[8];
            for(int i=0;i<8;++i){V p{i&1?bounds.max.x:bounds.min.x,i&2?bounds.max.y:bounds.min.y,i&4?bounds.max.z:bounds.min.z};corners[i]={model[0]*p.x+model[4]*p.y+model[8]*p.z+model[12],model[1]*p.x+model[5]*p.y+model[9]*p.z+model[13],model[2]*p.x+model[6]*p.y+model[10]*p.z+model[14]};}
            for(int i=0;i<8;++i)for(int bit:{1,2,4})if(!(i&bit))line(corners[i],corners[i|bit],IM_COL32(255,225,80,255));
        }
    }
    draw->PopClipRect();
}
}

namespace editor {
ecs::Vec3 EditorLayer::PlacementPoint(Application& app,float u,float v)const{
    auto eye=app.GetCameraPosition();if(!std::isfinite(u)||!std::isfinite(v))return eye;
    u=std::clamp(u,0.f,1.f);v=std::clamp(v,0.f,1.f);
    const auto camera=GizmoCamera(eye,app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),float(std::max(1,m_ViewportPixelWidth))/std::max(1,m_ViewportPixelHeight),app.GetCameraNearPlane(),app.GetCameraFarPlane());
    float x=(u*2-1)/camera.projection[0],y=(1-v*2)/camera.projection[5];
    V ray{camera.view[0]*x+camera.view[1]*y+camera.view[2],camera.view[4]*x+camera.view[5]*y+camera.view[6],camera.view[8]*x+camera.view[9]*y+camera.view[10]};ray=Scale(ray,1/std::sqrt(Dot(ray,ray)));
    float nearest=app.GetCameraFarPlane();bool hit=false;
    if(auto* resources=app.GetResourceManager())app.GetWorld().ForEach<ecs::TransformComponent,ecs::MeshRendererComponent>([&](ecs::Entity e,auto& t,auto& m){
        if(!m.visible)return;auto mesh=resources->Get<MeshResource>(m.meshPath);if(!mesh||!mesh->IsLoaded()||mesh->GetData().meshData.vertices.empty())return;
        auto bounds=Bounds(mesh,app.GetRenderInterpolation().Resolve(e,t));if(bounds.inverse==std::array<float,12>{})return;
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
