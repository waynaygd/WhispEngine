#include "MotionSystem.h"

#include "../World.h"
#include "../../core/Logger.h"
#include "../components/TransformComponent.h"
#include "../components/VelocityComponent.h"

namespace ecs
{
void MotionSystem::Update(World& world, float dt)
{
    world.ForEach<TransformComponent, VelocityComponent>(
        [&](Entity e, TransformComponent&, VelocityComponent& velocity)
        {
            auto transform=world.WorldTransform(e);
            transform.position.x += velocity.linear.x * dt;
            transform.position.y += velocity.linear.y * dt;
            transform.position.z += velocity.linear.z * dt;

            transform.rotation.x += velocity.angular.x * dt;
            transform.rotation.y += velocity.angular.y * dt;
            transform.rotation.z += velocity.angular.z * dt;
            std::string error;if(!world.SetWorldTransform(e,transform,&error))Logger::Get().Warn(std::string("MotionSystem: ")+error);
        });
}
}
