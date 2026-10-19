#include "BoundsBounceSystem.h"

#include "../World.h"
#include "../../core/Logger.h"
#include "../components/BoundsBounceComponent.h"
#include "../components/TransformComponent.h"
#include "../components/VelocityComponent.h"

namespace ecs
{
void BoundsBounceSystem::Update(World& world, float)
{
    world.ForEach<TransformComponent, VelocityComponent, BoundsBounceComponent>(
        [&](Entity e, TransformComponent&, VelocityComponent& velocity, BoundsBounceComponent& bounds)
        {
            auto transform=world.WorldTransform(e);
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
            std::string error;if(!world.SetWorldTransform(e,transform,&error))Logger::Get().Warn(std::string("BoundsBounceSystem: ")+error);
        });
}
}
