#pragma once
#include "../ecs/World.h"
#include "../ecs/events/EventBus.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
#include <string>
namespace scripting {
class LuaRuntime {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    LuaRuntime();
    ~LuaRuntime();
    LuaRuntime(const LuaRuntime&)=delete;
    LuaRuntime& operator=(const LuaRuntime&)=delete;
    void Start(ecs::World&,std::function<bool(const std::string&)> input={});
    void Stop();
    void Update(float dt);
    void QueueCollision(const ecs::CollisionEvent&);
    bool ReloadScript(const std::string&);
    nlohmann::json Fields(const std::string&);
    std::string Error(const std::string&)const;
    std::size_t InstanceCount()const;
};
}
