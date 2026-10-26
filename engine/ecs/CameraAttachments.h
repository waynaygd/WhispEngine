#pragma once
#include "CameraState.h"
#include "components/CameraAttachmentComponent.h"
namespace ecs {
// Resolve from immutable current-frame camera data, without storing overrides or
// editing ECS. The nearest enabled attachment replaces its ancestry's pose.
inline std::optional<RenderPose> CameraAttachmentPose(const World& world,Entity entity,
    const CameraState& camera,const RenderInterpolation* interpolation=nullptr){
    if(!world.HasComponent<TransformComponent>(entity))return {};
    std::array<Entity,256> chain;std::size_t count=0;
    for(auto root=entity;root.IsValid();root=world.Parent(root)){
        if(count==chain.size())return {};
        auto* attachment=world.GetComponent<CameraAttachmentComponent>(root);
        if(attachment&&attachment->enabled&&ValidCameraAttachment(*attachment)){
            auto t=world.LocalTransform(root);
            // Preserve world scale/pivot, but never interpolate the discarded physical
            // position/rotation a second time relative to the camera.
            t.scale=(interpolation?interpolation->ResolveWorld(world,root):RenderPose::From(world.WorldTransform(root))).scale;
            t.position=attachment->localPosition;t.rotation=attachment->localRotation;
            auto local=RenderPose::From(t);auto p=local.position;
            RenderPose pose;
            pose.position={camera.pose.position.x+camera.right.x*p.x+camera.up.x*p.y+camera.forward.x*p.z,
                camera.pose.position.y+camera.right.y*p.x+camera.up.y*p.y+camera.forward.y*p.z,
                camera.pose.position.z+camera.right.z*p.x+camera.up.z*p.y+camera.forward.z*p.z};
            pose.rotation=CameraCompose(camera.pose.rotation,local.rotation);pose.scale=local.scale;
            if(count==0)return pose;
            float matrix[16];BuildRenderModelMatrix(matrix,pose);ImportMatrix combined;std::copy(matrix,matrix+16,combined.begin());
            // Existing Transform composition/decomposition rules, including pivot.
            for(auto i=count;i>0;--i){
                auto child=chain[i-1];auto ct=world.LocalTransform(child);auto cp=interpolation?interpolation->Resolve(child,ct):RenderPose::From(ct);
                BuildRenderModelMatrix(matrix,cp);ImportMatrix m;std::copy(matrix,matrix+16,m.begin());combined=MultiplyMatrix(combined,m);
            }
            auto result=DecomposeTransform(combined,world.LocalTransform(entity).scale);
            if(!result)return {}; // Unsupported shear/singular scale: ordinary pose.
            return RenderPose::From(*result);
        }
        chain[count++]=root;
    }
    return {};
}
}
