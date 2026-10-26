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
nlohmann::json DecodePrefabAsset(nlohmann::json);
nlohmann::json ReadPrefabAsset(const std::string&);
nlohmann::json NewChildPrefab(const std::string& parent);
nlohmann::json ResolvePrefabDelta(const nlohmann::json& delta,const nlohmann::json& parent);
nlohmann::json EditPrefabDelta(const nlohmann::json& delta,const nlohmann::json& before,const nlohmann::json& after);
void PollPrefabReload(bool force=false);
void ClearPrefabCache();
void InvalidatePrefabCache();
std::uint64_t PrefabRevision(const std::string&);
nlohmann::json CapturePrefab(ecs::World&,ecs::Entity root);
ecs::Entity InstantiatePrefab(ecs::World&,const std::string& key,const nlohmann::json&,ecs::Vec3 position);
nlohmann::json InstanceSceneNodes(ecs::World&,ecs::Entity root,const nlohmann::json& source);
ecs::Entity PrefabRoot(const ecs::World&,ecs::Entity member);
void LinkPrefab(ecs::World&,ecs::Entity,const std::string&,const nlohmann::json& source);
}
