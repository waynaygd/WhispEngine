#include "BoundsBounceSystem.h"

#include "../World.h"
#include "../components/BoundsBounceComponent.h"
#include "../components/TransformComponent.h"
#include "../components/VelocityComponent.h"

namespace ecs
{
void BoundsBounceSystem::Update(World& world, float)
{
    world.ForEach<TransformComponent, VelocityComponent, BoundsBounceComponent>(
        [](Entity, TransformComponent& transform, VelocityComponent& velocity, BoundsBounceComponent& bounds)
        {
            if (transform.x < bounds.minX)
            {
                transform.x = bounds.minX;
                velocity.vx = -velocity.vx;
            }
            else if (transform.x > bounds.maxX)
            {
                transform.x = bounds.maxX;
                velocity.vx = -velocity.vx;
            }

            if (transform.y < bounds.minY)
            {
                transform.y = bounds.minY;
                velocity.vy = -velocity.vy;
            }
            else if (transform.y > bounds.maxY)
            {
                transform.y = bounds.maxY;
                velocity.vy = -velocity.vy;
            }
        });
}
}
