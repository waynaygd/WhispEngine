#pragma once

#include "Entity.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ecs
{
class World
{
public:
    Entity CreateEntity();
    bool DestroyEntity(Entity entity);

    [[nodiscard]] bool IsAlive(Entity entity) const;
    [[nodiscard]] std::size_t GetAliveCount() const { return m_AliveCount; }
    [[nodiscard]] std::size_t GetCapacity() const { return m_Slots.size(); }

    void Clear();
    [[nodiscard]] std::string DebugDescribeEntity(Entity entity) const;

private:
    struct EntitySlot
    {
        std::uint32_t generation = 0;
        bool alive = false;
    };

    std::vector<EntitySlot> m_Slots;
    std::vector<std::uint32_t> m_FreeIndices;
    std::size_t m_AliveCount = 0;
};
}
