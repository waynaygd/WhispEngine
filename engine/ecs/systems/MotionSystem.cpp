#include "MotionSystem.h"

#include "../World.h"
#include "../components/TransformComponent.h"
#include "../components/VelocityComponent.h"

namespace ecs
{
void MotionSystem::Update(World& world, float dt)
{
    world.ForEach<TransformComponent, VelocityComponent>(
        [dt](Entity, TransformComponent& transform, VelocityComponent& velocity)
        {
            transform.position.x += velocity.linear.x * dt;
            transform.position.y += velocity.linear.y * dt;
            transform.position.z += velocity.linear.z * dt;

            transform.rotation.x += velocity.angular.x * dt;
            transform.rotation.y += velocity.angular.y * dt;
            transform.rotation.z += velocity.angular.z * dt;
        });
}
}
