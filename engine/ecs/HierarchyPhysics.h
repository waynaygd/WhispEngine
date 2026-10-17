#pragma once
#include "Hierarchy.h"
#include "components/ColliderComponent.h"
#include "components/RigidbodyComponent.h"
#include "components/CharacterMovementComponent.h"
#include "components/PlayerComponent.h"
#include "components/PlayerStartComponent.h"
namespace ecs {
// Per-query/per-step value geometry; never stored as another authoritative TRS.
struct HierarchyColliderShape {TransformComponent transform;ColliderComponent collider;};
inline HierarchyColliderShape ResolveHierarchyCollider(const World& world,const HierarchyGraph& graph,Entity e,bool validatedAncestry=false){
    const auto& node=graph.nodes.at(graph.entities.at(e));auto* c=world.GetComponent<ColliderComponent>(e);
    if(!c||!node.local||!node.matrix)throw std::runtime_error("Hierarchy physics: collider requires Transform");
    auto t=DecomposeTRS(*node.matrix,node.local->scale);
    if(!t)throw std::runtime_error("Hierarchy physics: sheared/singular collider is unsupported");
    auto* rb=world.GetComponent<RigidbodyComponent>(e);
    for(auto p=validatedAncestry?std::optional<std::size_t>{}:node.parent;p;p=graph.nodes[*p].parent)if(auto* ancestor=world.GetComponent<RigidbodyComponent>(graph.nodes[*p].entity);ancestor&&!ancestor->isStatic)
        throw std::runtime_error("Hierarchy physics: collider cannot descend from a dynamic body; visual children only");
    if(rb&&!rb->isStatic){
        if(node.parent)throw std::runtime_error("Hierarchy physics: dynamic Rigidbody must be a root");
        // The existing solver has world-sized shapes, not rotating local offsets.
        if(std::abs(node.local->scale.x-1)>1e-5f||std::abs(node.local->scale.y-1)>1e-5f||std::abs(node.local->scale.z-1)>1e-5f||c->offset.x!=0||c->offset.y!=0||c->offset.z!=0)
            throw std::runtime_error("Hierarchy physics: dynamic roots require unit scale and zero collider offset");
    }
    HierarchyColliderShape shape{*t,*c};auto& dimensions=shape.collider.halfExtents;
    const Vec3 scale{std::abs(t->scale.x),std::abs(t->scale.y),std::abs(t->scale.z)};
    if(c->type!=ColliderType::Box&&c->type!=ColliderType::Sphere)throw std::runtime_error("Hierarchy physics: unknown collider type");
    if(c->type==ColliderType::Sphere&&(std::abs(scale.x-scale.y)>1e-5f*scale.x||std::abs(scale.x-scale.z)>1e-5f*scale.x))
        throw std::runtime_error("Hierarchy physics: sphere requires uniform absolute world scale");
    if(!Finite(dimensions)||!Finite(c->offset)||std::min({dimensions.x,dimensions.y,dimensions.z})<0)throw std::runtime_error("Hierarchy physics: invalid collider dimensions/offset");
    dimensions={dimensions.x*scale.x,dimensions.y*scale.y,dimensions.z*scale.z};
    const auto& m=*node.matrix;auto offset=c->offset;
    shape.collider.offset={float(m[0]*offset.x+m[4]*offset.y+m[8]*offset.z),float(m[1]*offset.x+m[5]*offset.y+m[9]*offset.z),float(m[2]*offset.x+m[6]*offset.y+m[10]*offset.z)};
    if(!Finite(dimensions)||!Finite(shape.collider.offset)||std::max({dimensions.x,dimensions.y,dimensions.z})>1e6f)throw std::runtime_error("Hierarchy physics: world geometry exceeds supported range");
    auto position=shape.transform.position,shift=shape.collider.offset;
    if(!Finite(position)||std::max({std::abs(position.x),std::abs(position.y),std::abs(position.z),std::abs(shift.x),std::abs(shift.y),std::abs(shift.z),std::abs(position.x+shift.x),std::abs(position.y+shift.y),std::abs(position.z+shift.z)})>1e6f)throw std::runtime_error("Hierarchy physics: collider position/offset/center exceeds supported range [+-1e6]");
    return shape;
}
inline HierarchyGraph RequireHierarchyRuntime(const World& world){
    std::string error;auto graph=HierarchyGraph::Build(world,&error);if(!graph)throw std::runtime_error(error);
    std::vector<bool> dynamicAncestor(graph->nodes.size());
    for(auto index:graph->preorder){const auto& node=graph->nodes[index];auto e=node.entity;
        if(node.parent){auto* rb=world.GetComponent<RigidbodyComponent>(graph->nodes[*node.parent].entity);dynamicAncestor[index]=dynamicAncestor[*node.parent]||(rb&&!rb->isStatic);}
        if(node.parent&&(world.HasComponent<PlayerComponent>(e)||world.HasComponent<CharacterMovementComponent>(e)||(world.HasComponent<RigidbodyComponent>(e)&&!world.GetComponent<RigidbodyComponent>(e)->isStatic)))
            throw std::runtime_error("Hierarchy runtime: dynamic Rigidbody/Character/Player must be a root");
        if(world.HasComponent<PlayerStartComponent>(e)&&(node.parent||!node.children.empty()))
            throw std::runtime_error("Hierarchy runtime: PlayerStart must be a root without children (cleanup refused)");
        if(world.HasComponent<ColliderComponent>(e)){if(dynamicAncestor[index])throw std::runtime_error("Hierarchy physics: collider cannot descend from a dynamic body; visual children only");(void)ResolveHierarchyCollider(world,*graph,e,true);}
    }return std::move(*graph);
}
}
