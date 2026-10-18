#pragma once
#include "../ecs/World.h"
#include "../ecs/events/EventBus.h"
#include "GameplayServices.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
namespace scripting {
class LuaRuntime {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    LuaRuntime();
    ~LuaRuntime();
    LuaRuntime(const LuaRuntime&)=delete;
    LuaRuntime& operator=(const LuaRuntime&)=delete;
    void Start(ecs::World&,std::function<bool(const std::string&)> input={},GameplayServices services={});
    // Disposable existing loader/VM; no active ownership or lifecycle callbacks.
    static bool ValidateStartupScripts(const ecs::World&,std::string* error=nullptr);
    void Stop();
    void Update(float dt);
    void FixedUpdate(float dt);
    void SuspendInput();
    void QueueCollision(const ecs::CollisionEvent&);
    bool ReloadScript(const std::string&);
    nlohmann::json Fields(const std::string&);
    std::string Error(const std::string&)const;
    std::size_t InstanceCount()const;
    bool HasCameraOwner()const;
    // Read-only, owner-thread diagnostics. Views expire at the next runtime
    // mutation; consumers must copy/format them before advancing a frame.
    struct InstanceDiagnostics {
        bool alive=false,loaded=false,present=false,matches=false,failed=false;
        bool inputAllowed=false,cameraOwner=false,cursorOwner=false;
        std::string_view path,error;
        std::uint64_t revision=0,starts=0,updates=0,fixedUpdates=0;
    };
    InstanceDiagnostics InspectInstance(ecs::Entity)const;
};
}
