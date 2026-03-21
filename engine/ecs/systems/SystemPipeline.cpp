#include "SystemPipeline.h"

#include "../World.h"

namespace ecs
{
void SystemPipeline::Update(World& world, float dt)
{
    for (const auto& system : m_Systems)
        system->Update(world, dt);
}

void SystemPipeline::Clear()
{
    m_Systems.clear();
}
}
