#pragma once
#include "World.h"
#include <functional>
#include <mutex>
namespace ecs {
// Producers only capture IDs/values. Apply is a main-thread phase boundary after jobs join.
class MutationQueue {
    struct Command {std::uint64_t world;std::function<void(World&)> apply;};
    std::mutex mutex;
    std::vector<Command> pending;
public:
    void Enqueue(std::uint64_t world,std::function<void(World&)> command){
        std::lock_guard lock(mutex);pending.push_back({world,std::move(command)});
    }
    void Destroy(Entity entity){Enqueue(entity.world,[entity](World& w){if(w.IsAlive(entity))w.DestroyEntity(entity);});}
    template<class T>void Add(Entity entity,T value={}){Enqueue(entity.world,[entity,value=std::move(value)](World& w){if(w.IsAlive(entity)&&!w.HasComponent<T>(entity))w.AddComponent<T>(entity,value);});}
    template<class T>void Remove(Entity entity){Enqueue(entity.world,[entity](World& w){if(w.IsAlive(entity))w.RemoveComponent<T>(entity);});}
    std::size_t Apply(World& world){
        world.AssertOwnerThread();
        if(world.IsIterating())throw std::logic_error("Apply mutations only at a phase boundary");
        std::vector<Command> work;{std::lock_guard lock(mutex);work.swap(pending);}
        std::size_t applied=0;for(auto& c:work)if(c.world==world.GetIdentity()){c.apply(world);++applied;}return applied;
    }
    void Clear(){std::lock_guard lock(mutex);pending.clear();}
};
}
