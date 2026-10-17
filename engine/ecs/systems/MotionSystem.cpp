#include "MotionSystem.h"

#include "../World.h"
#include "../components/TransformComponent.h"
#include "../components/VelocityComponent.h"
#include "../TransformAccess.h"

namespace ecs
{
void MotionSystem::Update(World& world, float dt)
{
    if(world.IsHierarchyData()){
        auto graph=HierarchyGraph::Build(world);if(!graph)throw std::runtime_error("Motion: invalid hierarchy");
        // No writes during ForEach: hierarchy setters validate the whole graph.
        for(auto index:graph->preorder){auto e=graph->nodes[index].entity;auto* v=world.GetComponent<VelocityComponent>(e);if(!v)continue;
            auto t=ReadWorldTransform(world,e);if(!t)throw std::runtime_error("Motion requires a TRS world transform (shear unsupported)");
            t->position.x+=v->linear.x*dt;t->position.y+=v->linear.y*dt;t->position.z+=v->linear.z*dt;
            t->rotation.x+=v->angular.x*dt;t->rotation.y+=v->angular.y*dt;t->rotation.z+=v->angular.z*dt;
            if(!TrySetWorldTransform(world,e,*t))throw std::runtime_error("Motion: unsupported hierarchy world write");
        }return;
    }
    world.ForEach<TransformComponent, VelocityComponent>(
        [&world,dt](Entity e, TransformComponent&, VelocityComponent& velocity)
        {
            auto transform=*ReadWorldTransform(world,e);
            transform.position.x += velocity.linear.x * dt;
            transform.position.y += velocity.linear.y * dt;
            transform.position.z += velocity.linear.z * dt;

            transform.rotation.x += velocity.angular.x * dt;
            transform.rotation.y += velocity.angular.y * dt;
            transform.rotation.z += velocity.angular.z * dt;
            if(!TrySetWorldTransform(world,e,transform))throw std::runtime_error("Motion produced a nonfinite world Transform");
        });
}
}
