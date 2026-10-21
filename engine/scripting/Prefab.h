#pragma once
#include "../ecs/World.h"
#include "../ecs/MathTypes.h"
#include <filesystem>
#include <nlohmann/json.hpp>
namespace scripting {
// Strict asset key resolution, including canonical containment through symlinks.
std::filesystem::path SafeAsset(const std::string& key,const std::string& suffix);
ecs::Entity SpawnPrefab(ecs::World&,const std::string& key,ecs::Vec3 position);
nlohmann::json ReadPrefab(const std::string& key);
void ValidatePrefab(const nlohmann::json&);
nlohmann::json CapturePrefab(ecs::World&,ecs::Entity root);
ecs::Entity InstantiatePrefab(ecs::World&,const std::string& key,const nlohmann::json&,ecs::Vec3 position);
nlohmann::json InstanceSceneNodes(ecs::World&,ecs::Entity root,const nlohmann::json& source);
ecs::Entity PrefabRoot(const ecs::World&,ecs::Entity member);
void LinkPrefab(ecs::World&,ecs::Entity,const std::string&,const nlohmann::json& source);
}
