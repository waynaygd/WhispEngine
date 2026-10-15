#pragma once
#include "../../render/SceneLights.h"
#include "../World.h"
#include "TagComponent.h"
#include "TransformComponent.h"
namespace ecs
{
struct DirectionalLightComponent : LightParameters { bool primary=false; };
struct PointLightComponent : LightParameters {};
struct SpotLightComponent : LightParameters {};
inline LightParameters* GetLightParameters(World& world,Entity e,LightType* type=nullptr)
{
    if(!world.IsAlive(e))return nullptr;
    if(auto* p=world.GetComponent<DirectionalLightComponent>(e)){if(type)*type=LightType::Directional;return p;}
    if(auto* p=world.GetComponent<PointLightComponent>(e)){if(type)*type=LightType::Point;return p;}
    if(auto* p=world.GetComponent<SpotLightComponent>(e)){if(type)*type=LightType::Spot;return p;}
    return nullptr;
}
inline void AddSceneLight(World& world,Entity e,const SceneLightDescription& d)
{
    auto p=SanitizeLight(d.parameters);
    if(d.type==LightType::Directional){auto& light=world.AddComponent<DirectionalLightComponent>(e);static_cast<LightParameters&>(light)=p;light.primary=d.primary;}
    else if(d.type==LightType::Point)static_cast<LightParameters&>(world.AddComponent<PointLightComponent>(e))=p;
    else static_cast<LightParameters&>(world.AddComponent<SpotLightComponent>(e))=p;
}
inline void ClearSceneLights(World& world)
{
    std::vector<Entity> entities;
    world.ForEach<TransformComponent>([&](Entity e,auto&){if(GetLightParameters(world,e))entities.push_back(e);});
    for(auto e:entities)world.DestroyEntity(e);
}
}
