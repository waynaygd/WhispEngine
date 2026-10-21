#pragma once
#include "TransformMath.h"
#include <vector>
namespace editor {
inline bool ValidGroupSource(const ecs::TransformComponent& t){
    for(float v:{t.position.x,t.position.y,t.position.z,t.rotation.x,t.rotation.y,t.rotation.z})if(!std::isfinite(v)||std::abs(v)>1e6f)return false;
    for(float v:{t.scale.x,t.scale.y,t.scale.z})if(!std::isfinite(v)||std::abs(v)<.001f||std::abs(v)>10000.f)return false;return true;
}
inline ImportMatrix TransformMatrix(const ecs::TransformComponent& t){float f[16];ecs::BuildRenderModelMatrix(f,ecs::RenderPose::From(t));ImportMatrix m;std::copy(f,f+16,m.begin());return m;}
inline ImportMatrix InverseGroupPivot(const ecs::TransformComponent& pivot){
    // A group reference has unit scale; transpose its orthonormal rotation.
    auto m=TransformMatrix(pivot),inverse=IdentityMatrix();
    for(int c=0;c<3;++c)for(int r=0;r<3;++r)inverse[c*4+r]=m[r*4+c];
    for(int r=0;r<3;++r)inverse[12+r]=-(inverse[r]*m[12]+inverse[4+r]*m[13]+inverse[8+r]*m[14]);return inverse;
}
inline std::optional<std::vector<ecs::TransformComponent>> TransformGroup(const std::vector<ecs::TransformComponent>& originals,const ecs::TransformComponent& pivot,const ImportMatrix& target){
    // Validate the gizmo itself as well as every resulting object before applying.
    auto reference=DecomposeTransform(target);if(!reference||reference->scale.x<.001f||reference->scale.y<.001f||reference->scale.z<.001f)return {};
    const auto delta=MultiplyMatrix(target,InverseGroupPivot(pivot));std::vector<ecs::TransformComponent> result;result.reserve(originals.size());
    for(const auto& original:originals){
        auto transformed=DecomposeTransform(MultiplyMatrix(delta,TransformMatrix(original)),original.scale);
        if(!transformed)return {};
        for(float value:{transformed->position.x,transformed->position.y,transformed->position.z})if(std::abs(value)>1e6f)return {};
        for(float value:{transformed->scale.x,transformed->scale.y,transformed->scale.z})if(std::abs(value)<.001f||std::abs(value)>10000.f)return {};
        result.push_back(ecs::RebasePivot(*transformed,original.pivot));
    }
    return result;
}
}
