#pragma once

#include "../core/ConfigLoader.h"
#include "SceneSettings.h"
#include "../ecs/World.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ecs
{
class World;
}

class SceneSerializer
{
public:
    static bool LoadEntityConfigs(
        const std::filesystem::path& path,
        std::vector<EcsDemoEntityConfig>& outEntities,
        std::string* outError = nullptr);

    static bool SaveEntityConfigs(
        const std::filesystem::path& path,
        const std::vector<EcsDemoEntityConfig>& entities,
        std::string* outError = nullptr);

    static bool SaveWorld(
        const std::filesystem::path& path,
        ecs::World& world,
        std::string* outError = nullptr,
        const SceneSettings* settings = nullptr);
    static nlohmann::json CaptureWorld(ecs::World&,const SceneSettings& = {});
    static bool RestoreWorld(const nlohmann::json&,ecs::World&,SceneSettings&,std::string* error=nullptr);
    static bool LoadWorld(const std::filesystem::path&,ecs::World&,SceneSettings&,std::string* error=nullptr);
    // Inspect before constructing a candidate World. Missing marker means RootOnly;
    // no mode conversion or Relationship-based inference is performed.
    static bool DetermineWorldMode(const nlohmann::json&,ecs::World::DataMode&,std::string* error=nullptr);
    static bool ReadWorldMode(const std::filesystem::path&,ecs::World::DataMode&,std::string* error=nullptr);
    static bool WriteAtomic(const std::filesystem::path&,const nlohmann::json&,std::string* error=nullptr,bool replace=true);
    static std::string EntitySceneId(const ecs::World&,ecs::Entity);
    static std::string PrimaryDirectionalId(ecs::World&);
};
