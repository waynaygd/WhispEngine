#pragma once
#include "../ecs/Entity.h"
#include "../ecs/MathTypes.h"
#include "../ecs/RenderInterpolation.h"
#include <functional>
#include <string>
#include <vector>
namespace scripting {
// Host capabilities; no Lua or renderer ownership. Follow is resolved by the
// host after physics using the same interpolated pose as scene rendering.
struct GameplayCamera {
    ecs::Vec3 position{},offset{};
    float yaw=0,pitch=0;
    ecs::Entity target{};
    bool following=false;
};
inline void ResolveGameplayCamera(GameplayCamera& camera,const ecs::World& world,const ecs::RenderInterpolation* interpolation=nullptr){
    if(!camera.following)return;
    auto position=ecs::ReadWorldPosition(world,camera.target);
    if(!position){camera.following=false;return;}
    if(!world.IsHierarchyData()&&interpolation){auto t=ecs::ReadWorldTransform(world,camera.target);*position=interpolation->Resolve(camera.target,*t).position;}
    camera.position={position->x+camera.offset.x,position->y+camera.offset.y,position->z+camera.offset.z};
}
struct GameplayServices {
    std::function<bool()> inputAllowed;
    std::function<bool(const std::string&)> pressed,released;
    std::function<ecs::Vec3()> mouseDelta;
    std::function<bool()> cursorCaptured;
    std::function<bool(bool)> captureCursor;
    std::function<bool(const std::string&,const std::string&)> bindAction;
    std::function<std::vector<std::string>()> actions;
    GameplayCamera* camera=nullptr;
};
}
