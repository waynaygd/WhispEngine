#pragma once

#include "../core/ConfigLoader.h"

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
        std::string* outError = nullptr);
};
