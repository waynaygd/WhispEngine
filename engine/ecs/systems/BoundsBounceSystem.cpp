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
            if (transform.position.x < bounds.minX)
            {
                transform.position.x = bounds.minX;
                velocity.linear.x = -velocity.linear.x;
            }
            else if (transform.position.x > bounds.maxX)
            {
                transform.position.x = bounds.maxX;
                velocity.linear.x = -velocity.linear.x;
            }

            if (transform.position.y < bounds.minY)
            {
                transform.position.y = bounds.minY;
                velocity.linear.y = -velocity.linear.y;
            }
            else if (transform.position.y > bounds.maxY)
            {
                transform.position.y = bounds.maxY;
                velocity.linear.y = -velocity.linear.y;
            }
        });
}
}
