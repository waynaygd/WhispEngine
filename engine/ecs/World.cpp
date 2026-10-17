#include "World.h"
#include "Hierarchy.h"
#include "HierarchyPhysics.h"

#include <sstream>

namespace ecs
{
void World::RequireRuntimeWorld()const{AssertOwnerThread();if(IsHierarchyData())(void)RequireHierarchyRuntime(*this);}
void World::AttachHierarchyLifecycle(void* owner,std::function<void(const std::vector<Entity>&)> callback){
    CheckMutation();if(!IsHierarchyData()||!owner||!callback||m_HierarchyLifecycleOwner)throw std::logic_error("Hierarchy lifecycle already owned or invalid attachment");
    m_HierarchyLifecycle=std::move(callback);m_HierarchyLifecycleOwner=owner;
}
void World::DetachHierarchyLifecycle(void* owner){AssertOwnerThread();if(m_HierarchyLifecycleOwner==owner){m_HierarchyLifecycle={};m_HierarchyLifecycleOwner=nullptr;}}
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
    if(IsHierarchyData()){
        std::string error;auto graph=HierarchyGraph::Build(*this,&error);
        if(!graph)throw std::runtime_error(error);
        auto order=graph->Closure({entity},true,&error);if(!order)throw std::runtime_error(error);
        m_FreeIndices.reserve(m_FreeIndices.size()+order->size());
        if(m_InHierarchyLifecycle)throw std::logic_error("Recursive native hierarchy destruction: enqueue instead");
        if(m_HierarchyLifecycle){m_InHierarchyLifecycle=true;try{m_HierarchyLifecycle(*order);}catch(...){m_InHierarchyLifecycle=false;throw;}m_InHierarchyLifecycle=false;}
        // The runtime owner has retired the whole closure while components
        // still exist. RootOnly retains its original single-entity path.
        for(auto e:*order)DestroyEntityStorage(e);return true;
    }
    return DestroyEntityStorage(entity);
}
bool World::DestroyEntityStorage(Entity entity){

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
    if(IsHierarchyData()&&m_HierarchyLifecycle){
        if(m_InHierarchyLifecycle)throw std::logic_error("Recursive hierarchy clear");
        auto graph=HierarchyGraph::Build(*this);if(!graph)throw std::runtime_error("Clear: invalid hierarchy");
        auto order=graph->Closure(Entities(),true);m_InHierarchyLifecycle=true;
        try{m_HierarchyLifecycle(*order);}catch(...){m_InHierarchyLifecycle=false;throw;}m_InHierarchyLifecycle=false;
    }
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
    if(IsHierarchyData()){std::string error;if(!ValidateHierarchy(*this,&error))throw std::runtime_error(error);}
    auto copy=std::make_unique<World>(m_DataMode);copy->m_Slots=m_Slots;copy->m_FreeIndices=m_FreeIndices;copy->m_AliveCount=m_AliveCount;
    for(const auto& [type,storage]:m_ComponentStorages)copy->m_ComponentStorages.emplace(type,storage->Clone());
    if(mapping){mapping->clear();mapping->reserve(m_AliveCount);for(auto e:Entities())mapping->emplace(e,copy->LocalEntity(e.index));}
    return copy;
}
void World::ReplaceData(World& source){
    if(&source==this)throw std::logic_error("Cannot replace World data from itself");
    CheckMutation();source.CheckMutation();
    if(m_HierarchyLifecycleOwner||source.m_HierarchyLifecycleOwner)throw std::logic_error("Stop hierarchy runtime before ReplaceData");
    if(m_DataMode!=source.m_DataMode)throw std::logic_error("Cannot publish a data-only hierarchy World into a gameplay World");
    if(source.IsHierarchyData()){std::string error;if(!ValidateHierarchy(source,&error))throw std::runtime_error(error);}
    m_Slots=std::move(source.m_Slots);m_FreeIndices=std::move(source.m_FreeIndices);m_ComponentStorages=std::move(source.m_ComponentStorages);
    m_AliveCount=source.m_AliveCount;source.m_AliveCount=0;m_Identity=s_NextIdentity.fetch_add(1);
}
}
