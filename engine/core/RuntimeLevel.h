#pragma once
#include "ProjectContext.h"
#include "../scene/SceneSerializer.h"
#include "../ecs/PlayerSpawn.h"

// Data-only preparation shared by ordinary Play, startup-map Play and travel.
// No published World, Lua, physics, renderer or editor state is changed here.
struct PreparedRuntimeLevel {
    std::unique_ptr<ecs::World> world;
    ecs::World::CloneMap mapping;
    SceneSettings settings;
    ecs::Entity player;
    ecs::TransformComponent spawn;
    bool prefabSpawn=false;
};
inline PreparedRuntimeLevel PrepareRuntimeLevel(const ecs::World& authored,
    const ProjectContext* project,const std::string& scene={}) {
    PreparedRuntimeLevel level;
    if(scene.empty())level.world=authored.CloneData(&level.mapping);
    else {
        if(!project)throw std::runtime_error("Level loading requires an open project");
        level.world=std::make_unique<ecs::World>();std::string error;
        if(!SceneSerializer::LoadWorld(project->ResolveScene(scene),*level.world,level.settings,&error))
            throw std::runtime_error("Scene '"+scene+"': "+error);
    }
    level.player=ecs::SpawnPlayer(*level.world,&level.spawn,&level.prefabSpawn,project?&project->maps:nullptr);
    level.world->ValidateHierarchy();
    return level;
}
