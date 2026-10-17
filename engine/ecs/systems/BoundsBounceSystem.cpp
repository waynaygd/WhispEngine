#include "BoundsBounceSystem.h"

#include "../World.h"
#include "../components/BoundsBounceComponent.h"
#include "../components/TransformComponent.h"
#include "../components/VelocityComponent.h"
#include "../TransformAccess.h"

namespace ecs {
void BoundsBounceSystem::Update(World& world, float)
{
    if(world.IsHierarchyData()){
        auto graph=HierarchyGraph::Build(world);if(!graph)throw std::runtime_error("Bounds: invalid hierarchy");
        for(auto index:graph->preorder){auto e=graph->nodes[index].entity;auto* v=world.GetComponent<VelocityComponent>(e);auto* b=world.GetComponent<BoundsBounceComponent>(e);if(!v||!b)continue;
            auto p=ReadWorldPosition(world,e);if(!p)throw std::runtime_error("Bounds: missing world position");auto velocity=*v;
            if(p->x<b->minX){p->x=b->minX;velocity.linear.x=-velocity.linear.x;}else if(p->x>b->maxX){p->x=b->maxX;velocity.linear.x=-velocity.linear.x;}
            if(p->y<b->minY){p->y=b->minY;velocity.linear.y=-velocity.linear.y;}else if(p->y>b->maxY){p->y=b->maxY;velocity.linear.y=-velocity.linear.y;}
            if(!TrySetWorldPosition(world,e,*p))throw std::runtime_error("Bounds: unsupported hierarchy world write");*v=velocity;
        }return;
    }
    world.ForEach<TransformComponent, VelocityComponent, BoundsBounceComponent>(
        [&world](Entity e, TransformComponent&, VelocityComponent& storedVelocity, BoundsBounceComponent& bounds) {
            auto transform=*ReadWorldTransform(world,e);
            auto velocity=storedVelocity;
            if (transform.position.x < bounds.minX) {
                transform.position.x = bounds.minX;
                velocity.linear.x = -velocity.linear.x;
            } else if (transform.position.x > bounds.maxX) {
                transform.position.x = bounds.maxX;
                velocity.linear.x = -velocity.linear.x;
            }

            if (transform.position.y < bounds.minY) {
                transform.position.y = bounds.minY;
                velocity.linear.y = -velocity.linear.y;
            } else if (transform.position.y > bounds.maxY) {
                transform.position.y = bounds.maxY;
                velocity.linear.y = -velocity.linear.y;
            }
            if(!TrySetWorldTransform(world,e,transform))throw std::runtime_error("Bounds produced a nonfinite world Transform");
            storedVelocity=velocity;
        });
}
}
