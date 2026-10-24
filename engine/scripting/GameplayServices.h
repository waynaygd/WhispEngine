#pragma once
#include "../ecs/Entity.h"
#include "../ecs/MathTypes.h"
#include "../ecs/CameraState.h"
#include <functional>
#include <string>
#include <vector>
class ResourceManager;
namespace ui {class RuntimeUI;}
namespace scripting {
// Host capabilities; no Lua or renderer ownership. Follow is resolved by the
// host after physics using the same interpolated pose as scene rendering.
struct GameplayCamera {
    ecs::Vec3 position{},offset{};
    float yaw=0,pitch=0;
    ecs::Entity target{};
    bool following=false;
    ecs::Entity selected{};
    bool Eligible(const ecs::World& world,ecs::Entity e)const {
        auto* c=world.GetComponent<ecs::CameraComponent>(e);
        return c&&c->enabled&&ecs::ValidCamera(*c)&&world.HasComponent<ecs::TransformComponent>(e);
    }
    ecs::Entity Active(const ecs::World& world) {
        if(Eligible(world,selected))return selected;
        selected={};ecs::Entity best{};int priority=0;
        for(auto e:world.Entities())if(Eligible(world,e)){
            int p=world.GetComponent<ecs::CameraComponent>(e)->priority;
            if(!best.IsValid()||p>priority||(p==priority&&e.index<best.index)){best=e;priority=p;}
        }
        return best;
    }
    bool Select(const ecs::World& world,ecs::Entity e) {
        if(!Eligible(world,e))return false;selected=e;return true;
    }
};
struct GameplayServices {
    // Request only. The host owns the queue and publishes at a frame boundary.
    std::function<bool(const std::string&)> loadScene;
    std::function<bool()> inputAllowed;
    std::function<bool(const std::string&)> pressed,released;
    std::function<ecs::Vec3()> mouseDelta;
    std::function<bool()> cursorCaptured;
    std::function<bool(bool)> captureCursor;
    std::function<bool(const std::string&,const std::string&)> bindAction;
    std::function<std::vector<std::string>()> actions;
    ResourceManager* postfxResources=nullptr;
    GameplayCamera* camera=nullptr;
    ui::RuntimeUI* runtimeUI=nullptr;
    std::function<void(const std::string&)> setUIInputMode;
    std::function<std::string()> getUIInputMode;
    std::function<void()> resetUIInput;
};
}
