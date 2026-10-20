#pragma once
#include "../ecs/World.h"
#include "../ecs/events/EventBus.h"
#include "GameplayServices.h"
#include "../resources/PostFxResource.h"
#include "GameplayEventHistory.h"
#include "LuaInspection.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
#include <optional>
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
    void Start(ecs::World&,std::function<bool(const std::string&)> input={},GameplayServices services={});
    void Stop();
    bool SetPostFx(const std::string&,const std::string&,const nlohmann::json&);
    bool SetPostFxWeight(const std::string&,float);
    bool ClearPostFx(const std::string&);
    const PostFxRuntimeOverrides& PostFxOverrides()const;
    GameplayEventHistory& Events();
    void Update(float dt);
    void FixedUpdate(float dt);
    void SuspendInput();
    void QueueCollision(const ecs::CollisionEvent&);
    bool ReloadScript(const std::string&);
    nlohmann::json Fields(const std::string&);
    // Inspection only: never loads or executes a script. Source matching rejects stale schemas.
    std::optional<nlohmann::json> CachedFields(const std::string& path,const std::string& source)const;
    static bool ValidFieldValue(const nlohmann::json& schema,const nlohmann::json& value);
    std::string Error(const std::string&)const;
    // Owner-thread inspection; Info never walks Lua tables, Inspect is strictly bounded.
    LuaInstanceInfo InspectionInfo(ecs::Entity)const;
    LuaInstanceSnapshot Inspect(ecs::Entity)const;
    std::size_t InstanceCount()const;
    bool HasCameraOwner()const;
};
}
