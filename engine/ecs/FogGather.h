#pragma once
#include "World.h"
#include "components/FogComponent.h"
namespace ecs {
inline FogFrame GatherFog(World& world,const LightCamera& camera={}){
    FogFrame frame;frame.camera=camera;Entity chosen{};
    world.ForEach<FogComponent>([&](Entity e,const auto& fog){
        if(!ValidFog(fog)){++frame.invalidCount;return;}if(!fog.enabled)return;++frame.activeCount;
        if(!chosen.IsValid()||e.index<chosen.index){chosen=e;frame.settings=fog;frame.active=true;}
    });return frame;
}
}
