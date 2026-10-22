#pragma once
#include "components/CameraComponent.h"
#include "RenderInterpolation.h"
namespace ecs {
// Final immutable-by-value render input, shared by every graphics backend.
// No selection, input, simulation ownership or mutable camera world lives here.
struct CameraState {
    Entity entity{};
    RenderPose pose{};
    Vec3 right{1,0,0},up{0,1,0},forward{0,0,1};
    CameraComponent settings{};
    float aspect=16.f/9.f;
    std::array<float,16> view{},projection{},viewProjection{};
};
inline RenderQuaternion CameraCompose(RenderQuaternion a,RenderQuaternion b) {
    return NormalizeQuaternion({a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
        a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z});
}
inline CameraState BuildCameraState(RenderPose pose,CameraComponent settings,float aspect,Entity entity={}) {
    ValidateCamera(settings);
    if(!std::isfinite(aspect)||aspect<=0)throw std::runtime_error("Camera: invalid aspect ratio");
    for(float value:{pose.position.x,pose.position.y,pose.position.z,pose.rotation.x,pose.rotation.y,pose.rotation.z,pose.rotation.w})
        if(!std::isfinite(value))throw std::runtime_error("Camera: nonfinite Transform");
    CameraState s;s.entity=entity;s.pose=pose;s.pose.scale={1,1,1};s.pose.rotation=NormalizeQuaternion(pose.rotation);s.settings=settings;s.aspect=aspect;
    float matrix[16];BuildRenderModelMatrix(matrix,s.pose);
    s.right={matrix[0],matrix[1],matrix[2]};s.up={matrix[4],matrix[5],matrix[6]};s.forward={matrix[8],matrix[9],matrix[10]};
    auto& v=s.view;v[15]=1;
    const Vec3 axes[]={s.right,s.up,s.forward};
    for(int row=0;row<3;++row){auto a=axes[row];v[row]=a.x;v[4+row]=a.y;v[8+row]=a.z;v[12+row]=-(a.x*pose.position.x+a.y*pose.position.y+a.z*pose.position.z);}
    auto& p=s.projection;const float range=settings.farClip-settings.nearClip;
    if(settings.projection==CameraProjection::Perspective){
        p[5]=1/std::tan(settings.fieldOfView*.5f);p[0]=p[5]/aspect;p[10]=settings.farClip/range;p[11]=1;p[14]=-settings.nearClip*settings.farClip/range;
    }else{p[5]=2/settings.orthographicSize;p[0]=p[5]/aspect;p[10]=1/range;p[14]=-settings.nearClip/range;p[15]=1;}
    for(int col=0;col<4;++col)for(int row=0;row<4;++row)for(int k=0;k<4;++k)s.viewProjection[col*4+row]+=p[k*4+row]*v[col*4+k];
    return s;
}
inline CameraState ResolveCameraState(const World& world,Entity entity,float aspect,const RenderInterpolation* interpolation=nullptr) {
    auto* c=world.GetComponent<CameraComponent>(entity);
    if(!c||!c->enabled)throw std::runtime_error("Camera: dead, foreign, disabled or absent CameraComponent");
    // Translation obeys existing hierarchy/pivot/scale semantics. Orientation is
    // a composition of local rotations only: scale never flips or stretches view.
    auto pose=interpolation?interpolation->ResolveWorld(world,entity):RenderPose::From(world.WorldTransform(entity));
    auto local=[&](Entity e){auto t=world.LocalTransform(e);return interpolation?interpolation->Resolve(e,t).rotation:RenderRotation(t.rotation);};
    auto rotation=local(entity);std::size_t depth=0;
    for(auto p=world.Parent(entity);p.IsValid();p=world.Parent(p)){
        if(++depth>256)throw std::runtime_error("Camera hierarchy depth exceeds 256");
        rotation=CameraCompose(local(p),rotation);
    }
    pose.rotation=rotation;return BuildCameraState(pose,*c,aspect,entity);
}
}
