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
            transform.x += velocity.vx * dt;
            transform.y += velocity.vy * dt;
            transform.angle += velocity.angularVelocity * dt;
        });
}
}
