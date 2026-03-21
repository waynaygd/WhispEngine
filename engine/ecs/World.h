#pragma once

#include "Entity.h"

#include <cstddef>
#include <memory>
#include <string>
#include <stdexcept>
#include <typeindex>
#include <typeinfo>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ecs
{
class World
{
public:
    Entity CreateEntity();
    bool DestroyEntity(Entity entity);

    template <typename T, typename... Args>
    T& AddComponent(Entity entity, Args&&... args);

    template <typename T>
    bool HasComponent(Entity entity) const;

    template <typename T>
    T* GetComponent(Entity entity);

    template <typename T>
    const T* GetComponent(Entity entity) const;

    template <typename T>
    bool RemoveComponent(Entity entity);

    template <typename TPrimary, typename... TOther, typename Func>
    void ForEach(Func&& func);

    [[nodiscard]] bool IsAlive(Entity entity) const;
    [[nodiscard]] std::size_t GetAliveCount() const { return m_AliveCount; }
    [[nodiscard]] std::size_t GetCapacity() const { return m_Slots.size(); }

    void Clear();
    [[nodiscard]] std::string DebugDescribeEntity(Entity entity) const;

private:
    struct IComponentStorage
    {
        virtual ~IComponentStorage() = default;
        virtual void Remove(std::uint32_t entityIndex) = 0;
        virtual void Clear() = 0;
    };

    template <typename T>
    class ComponentStorage final : public IComponentStorage
    {
    public:
        template <typename... Args>
        T& Emplace(std::uint32_t entityIndex, Args&&... args)
        {
            auto [it, inserted] = m_Components.emplace(entityIndex, T{ std::forward<Args>(args)... });
            if (!inserted)
                throw std::logic_error("Component already exists on entity");
            return it->second;
        }

        [[nodiscard]] bool Has(std::uint32_t entityIndex) const
        {
            return m_Components.find(entityIndex) != m_Components.end();
        }

        T* Get(std::uint32_t entityIndex)
        {
            const auto it = m_Components.find(entityIndex);
            return it == m_Components.end() ? nullptr : &it->second;
        }

        const T* Get(std::uint32_t entityIndex) const
        {
            const auto it = m_Components.find(entityIndex);
            return it == m_Components.end() ? nullptr : &it->second;
        }

        bool RemoveComponent(std::uint32_t entityIndex)
        {
            return m_Components.erase(entityIndex) > 0;
        }

        void Remove(std::uint32_t entityIndex) override
        {
            m_Components.erase(entityIndex);
        }

        void Clear() override
        {
            m_Components.clear();
        }

        auto& Items()
        {
            return m_Components;
        }

        const auto& Items() const
        {
            return m_Components;
        }

    private:
        std::unordered_map<std::uint32_t, T> m_Components;
    };

    template <typename T>
    ComponentStorage<T>* FindStorage()
    {
        const auto it = m_ComponentStorages.find(std::type_index(typeid(T)));
        return it == m_ComponentStorages.end() ? nullptr : static_cast<ComponentStorage<T>*>(it->second.get());
    }

    template <typename T>
    const ComponentStorage<T>* FindStorage() const
    {
        const auto it = m_ComponentStorages.find(std::type_index(typeid(T)));
        return it == m_ComponentStorages.end() ? nullptr : static_cast<const ComponentStorage<T>*>(it->second.get());
    }

    template <typename T>
    ComponentStorage<T>& GetOrCreateStorage()
    {
        const auto key = std::type_index(typeid(T));
        auto it = m_ComponentStorages.find(key);
        if (it == m_ComponentStorages.end())
        {
            auto storage = std::make_unique<ComponentStorage<T>>();
            const auto [createdIt, inserted] = m_ComponentStorages.emplace(key, std::move(storage));
            (void)inserted;
            it = createdIt;
        }

        return *static_cast<ComponentStorage<T>*>(it->second.get());
    }

    struct EntitySlot
    {
        std::uint32_t generation = 0;
        bool alive = false;
    };

    std::vector<EntitySlot> m_Slots;
    std::vector<std::uint32_t> m_FreeIndices;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentStorage>> m_ComponentStorages;
    std::size_t m_AliveCount = 0;
};

template <typename T, typename... Args>
T& World::AddComponent(Entity entity, Args&&... args)
{
    if (!IsAlive(entity))
        throw std::logic_error("Cannot add component to dead entity");

    return GetOrCreateStorage<T>().Emplace(entity.index, std::forward<Args>(args)...);
}

template <typename T>
bool World::HasComponent(Entity entity) const
{
    if (!IsAlive(entity))
        return false;

    const ComponentStorage<T>* storage = FindStorage<T>();
    return storage != nullptr && storage->Has(entity.index);
}

template <typename T>
T* World::GetComponent(Entity entity)
{
    if (!IsAlive(entity))
        return nullptr;

    ComponentStorage<T>* storage = FindStorage<T>();
    return storage == nullptr ? nullptr : storage->Get(entity.index);
}

template <typename T>
const T* World::GetComponent(Entity entity) const
{
    if (!IsAlive(entity))
        return nullptr;

    const ComponentStorage<T>* storage = FindStorage<T>();
    return storage == nullptr ? nullptr : storage->Get(entity.index);
}

template <typename T>
bool World::RemoveComponent(Entity entity)
{
    if (!IsAlive(entity))
        return false;

    ComponentStorage<T>* storage = FindStorage<T>();
    return storage != nullptr && storage->RemoveComponent(entity.index);
}

template <typename TPrimary, typename... TOther, typename Func>
void World::ForEach(Func&& func)
{
    ComponentStorage<TPrimary>* primaryStorage = FindStorage<TPrimary>();
    if (primaryStorage == nullptr)
        return;

    auto otherStorages = std::tuple<ComponentStorage<TOther>*...>{ FindStorage<TOther>()... };
    const bool hasAllStorages = std::apply(
        [](auto*... storages)
        {
            return ((storages != nullptr) && ...);
        },
        otherStorages);

    if constexpr (sizeof...(TOther) > 0)
    {
        if (!hasAllStorages)
            return;
    }

    for (auto& [entityIndex, primaryComponent] : primaryStorage->Items())
    {
        Entity entity{ entityIndex, m_Slots[entityIndex].generation };
        if (!IsAlive(entity))
            continue;

        if constexpr (sizeof...(TOther) == 0)
        {
            func(entity, primaryComponent);
        }
        else
        {
            const bool hasAllComponents = std::apply(
                [entityIndex](auto*... storages)
                {
                    return (storages->Has(entityIndex) && ...);
                },
                otherStorages);

            if (!hasAllComponents)
                continue;

            std::apply(
                [&](auto*... storages)
                {
                    func(entity, primaryComponent, *storages->Get(entityIndex)...);
                },
                otherStorages);
        }
    }
}
}
