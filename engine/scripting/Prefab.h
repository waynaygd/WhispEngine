#pragma once
#include "../ecs/World.h"
#include "../ecs/MathTypes.h"
#include <filesystem>
namespace scripting {
// Strict asset key resolution, including canonical containment through symlinks.
std::filesystem::path SafeAsset(const std::string& key,const std::string& suffix);
ecs::Entity SpawnPrefab(ecs::World&,const std::string& key,ecs::Vec3 position);
}
