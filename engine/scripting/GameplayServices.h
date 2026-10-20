#pragma once
#include "../ecs/Entity.h"
#include "../ecs/MathTypes.h"
#include <functional>
#include <string>
#include <vector>
class ResourceManager;
namespace scripting {
// Host capabilities; no Lua or renderer ownership. Follow is resolved by the
// host after physics using the same interpolated pose as scene rendering.
struct GameplayCamera {
    ecs::Vec3 position{},offset{};
    float yaw=0,pitch=0;
    ecs::Entity target{};
    bool following=false;
};
struct GameplayServices {
    std::function<bool()> inputAllowed;
    std::function<bool(const std::string&)> pressed,released;
    std::function<ecs::Vec3()> mouseDelta;
    std::function<bool()> cursorCaptured;
    std::function<bool(bool)> captureCursor;
    std::function<bool(const std::string&,const std::string&)> bindAction;
    std::function<std::vector<std::string>()> actions;
    ResourceManager* postfxResources=nullptr;
    GameplayCamera* camera=nullptr;
};
}
