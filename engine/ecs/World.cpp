#include "World.h"

#include <sstream>

namespace ecs
{
Entity World::CreateEntity()
{
    CheckMutation();
    std::uint32_t index = Entity::InvalidIndex;

    if (!m_FreeIndices.empty())
    {
        index = m_FreeIndices.back();
        m_FreeIndices.pop_back();
        m_Slots[index].alive = true;
    }
    else
    {
        index = static_cast<std::uint32_t>(m_Slots.size());
        m_Slots.push_back(EntitySlot{});
        m_Slots.back().alive = true;
    }

    ++m_AliveCount;
    return Entity{ index, m_Slots[index].generation, m_Identity };
}

bool World::DestroyEntity(Entity entity)
{
    CheckMutation();
    if (!IsAlive(entity))
        return false;

    for (auto& [type, storage] : m_ComponentStorages)
    {
        (void)type;
        storage->Remove(entity.index);
    }

    EntitySlot& slot = m_Slots[entity.index];
    slot.alive = false;
    ++slot.generation;
    m_FreeIndices.push_back(entity.index);
    --m_AliveCount;
    return true;
}

bool World::IsAlive(Entity entity) const
{
    if (!entity.IsValid() || entity.world != m_Identity || entity.index >= m_Slots.size())
        return false;

    const EntitySlot& slot = m_Slots[entity.index];
    return slot.alive && slot.generation == entity.generation;
}

void World::Clear()
{
    CheckMutation();
    m_FreeIndices.clear();
    // Keep generations across a reset: old editor/physics handles must never
    // alias newly created entities after Clear().
    for (std::size_t i = 0; i < m_Slots.size(); ++i)
    {
        auto& slot = m_Slots[i];
        if (slot.alive) ++slot.generation;
        slot.alive = false;
        m_FreeIndices.push_back(static_cast<std::uint32_t>(i));
    }
    for (auto& [type, storage] : m_ComponentStorages)
    {
        (void)type;
        storage->Clear();
    }
    m_ComponentStorages.clear();
    m_AliveCount = 0;
}

std::string World::DebugDescribeEntity(Entity entity) const
{
    std::ostringstream ss;
    ss << "Entity(index=" << entity.index
       << ", generation=" << entity.generation
       << ", world=" << entity.world
       << ", alive=" << (IsAlive(entity) ? "true" : "false")
       << ")";
    return ss.str();
}
}

namespace ecs {
std::unique_ptr<World> World::CloneData(CloneMap* mapping)const{
    AssertOwnerThread();if(IsIterating())throw std::logic_error("Clone World at a phase boundary");
    auto copy=std::make_unique<World>();copy->m_Slots=m_Slots;copy->m_FreeIndices=m_FreeIndices;copy->m_AliveCount=m_AliveCount;
    for(const auto& [type,storage]:m_ComponentStorages)copy->m_ComponentStorages.emplace(type,storage->Clone());
    if(mapping){mapping->clear();mapping->reserve(m_AliveCount);for(auto e:Entities())mapping->emplace(e,copy->LocalEntity(e.index));}
    return copy;
}
void World::ReplaceData(World& source){
    if(&source==this)throw std::logic_error("Cannot replace World data from itself");
    CheckMutation();source.CheckMutation();
    m_Slots=std::move(source.m_Slots);m_FreeIndices=std::move(source.m_FreeIndices);m_ComponentStorages=std::move(source.m_ComponentStorages);
    m_AliveCount=source.m_AliveCount;source.m_AliveCount=0;m_Identity=s_NextIdentity.fetch_add(1);
}
}
