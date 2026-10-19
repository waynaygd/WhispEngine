#pragma once
#include "TransformMath.h"
#include "../resources/MeshData.h"
#include <functional>
namespace editor {
struct PlacementBounds {ecs::Vec3 min{-.5f,-.5f,-.5f},max{.5f,.5f,.5f};};
inline PlacementBounds MeshPlacementBounds(const MeshData& mesh){
    if(mesh.vertices.empty())return {};
    PlacementBounds b{{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};
    for(const auto& v:mesh.vertices){b.min.x=std::min(b.min.x,v.position[0]);b.min.y=std::min(b.min.y,v.position[1]);b.min.z=std::min(b.min.z,v.position[2]);b.max.x=std::max(b.max.x,v.position[0]);b.max.y=std::max(b.max.y,v.position[1]);b.max.z=std::max(b.max.z,v.position[2]);}return b;
}
inline std::array<ecs::Vec3,8> PlacementCorners(const PlacementBounds& b,const ecs::TransformComponent& t){
    const auto m=TransformMatrix(t);std::array<ecs::Vec3,8> corners;
    for(int i=0;i<8;++i){ecs::Vec3 p{i&1?b.max.x:b.min.x,i&2?b.max.y:b.min.y,i&4?b.max.z:b.min.z};corners[i]={float(m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12]),float(m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13]),float(m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14])};}return corners;
}
using PlacementPoints=std::function<void(const std::function<void(ecs::Vec3)>&)>;
inline ecs::Vec3 PlacementTransformPoint(const ImportMatrix& m,ecs::Vec3 p){return {float(m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12]),float(m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13]),float(m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14])};}
inline ecs::TransformComponent ContactPlacement(const PlacementBounds& bounds,ecs::TransformComponent t,ecs::Vec3 contact,ecs::Vec3 normal,bool align,float surfaceOffset=0,float rotationOffset=0,const PlacementPoints& geometry={}){
    if(align){
        // Shortest rotation from local up to the hit normal. Compose with the
        // initial orientation; never replace the model's Transform with Euler sums.
        auto q=ecs::RenderRotation(t.rotation);auto pose=ecs::RenderPose::From(t);pose.position={};pose.scale={1,1,1};float m[16];ecs::BuildRenderModelMatrix(m,pose);
        ecs::Vec3 up{m[4],m[5],m[6]};float dot=up.x*normal.x+up.y*normal.y+up.z*normal.z;
        ecs::RenderQuaternion d{up.y*normal.z-up.z*normal.y,up.z*normal.x-up.x*normal.z,up.x*normal.y-up.y*normal.x,1+dot};
        if(dot<-.99999f){d={m[0],m[1],m[2],0};}d=ecs::NormalizeQuaternion(d);
        pose.rotation=ecs::NormalizeQuaternion({d.w*q.x+d.x*q.w+d.y*q.z-d.z*q.y,d.w*q.y-d.x*q.z+d.y*q.w+d.z*q.x,d.w*q.z+d.x*q.y-d.y*q.x+d.z*q.w,d.w*q.w-d.x*q.x-d.y*q.y-d.z*q.z});
        ecs::BuildRenderModelMatrix(m,pose);ImportMatrix rotation;std::copy(m,m+16,rotation.begin());if(auto r=DecomposeTransform(rotation))t.rotation=r->rotation;
    }
    if(rotationOffset!=0){
        const float angle=rotationOffset*.00872664625997f;const auto q=ecs::RenderRotation(t.rotation);const float s=std::sin(angle);
        const ecs::RenderQuaternion d{normal.x*s,normal.y*s,normal.z*s,std::cos(angle)};ecs::RenderPose pose;pose.rotation=ecs::NormalizeQuaternion({d.w*q.x+d.x*q.w+d.y*q.z-d.z*q.y,d.w*q.y-d.x*q.z+d.y*q.w+d.z*q.x,d.w*q.z+d.x*q.y-d.y*q.x+d.z*q.w,d.w*q.w-d.x*q.x-d.y*q.y-d.z*q.z});
        float m[16];ecs::BuildRenderModelMatrix(m,pose);ImportMatrix rotation;std::copy(m,m+16,rotation.begin());if(auto r=DecomposeTransform(rotation))t.rotation=r->rotation;
    }
    t.position={};float support=INFINITY;ecs::Vec3 center{};
    for(auto p:PlacementCorners(bounds,t)){support=std::min(support,p.x*normal.x+p.y*normal.y+p.z*normal.z);center.x+=p.x/8;center.y+=p.y/8;center.z+=p.z/8;}
    if(geometry){float exact=INFINITY;const auto matrix=TransformMatrix(t);geometry([&](ecs::Vec3 local){const auto p=PlacementTransformPoint(matrix,local);const float dot=p.x*normal.x+p.y*normal.y+p.z*normal.z;if(std::isfinite(dot))exact=std::min(exact,dot);});if(std::isfinite(exact))support=exact;}
    const float offset=surfaceOffset+center.x*normal.x+center.y*normal.y+center.z*normal.z-support;
    t.position={contact.x-center.x+normal.x*offset,contact.y-center.y+normal.y*offset,contact.z-center.z+normal.z*offset};return t;
}
}
