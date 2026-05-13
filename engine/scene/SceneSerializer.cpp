#include "SceneSerializer.h"

#include "../core/Logger.h"
#include "../ecs/World.h"
#include "../ecs/components/BoundsBounceComponent.h"
#include "../ecs/components/MaterialComponent.h"
#include "../ecs/components/MeshRendererComponent.h"
#include "../ecs/components/TagComponent.h"
#include "../ecs/components/TransformComponent.h"
#include "../ecs/components/VelocityComponent.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace
{
void SetError(std::string* outError, const std::string& message)
{
    if (outError != nullptr)
        *outError = message;
}

nlohmann::json Vec3ToJson(const ecs::Vec3& v)
{
    return nlohmann::json::array({ v.x, v.y, v.z });
}

nlohmann::json ColorToJson(const std::array<float, 4>& color)
{
    return nlohmann::json::array({ color[0], color[1], color[2], color[3] });
}

void ReadVec3(const nlohmann::json& json, const char* key, ecs::Vec3& out)
{
    if (!json.contains(key) || !json[key].is_array())
        return;

    const auto& value = json[key];
    if (value.size() > 0) out.x = value[0].get<float>();
    if (value.size() > 1) out.y = value[1].get<float>();
    if (value.size() > 2) out.z = value[2].get<float>();
}

nlohmann::json EntityToJson(const EcsDemoEntityConfig& entity)
{
    nlohmann::json json;
    json["tag"] = entity.tag;
    json["meshPath"] = entity.meshPath;
    json["texturePath"] = entity.texturePath;
    json["shaderPath"] = entity.shaderPath;
    json["materialPath"] = entity.materialPath;
    json["color"] = ColorToJson(entity.materialTint);
    json["visible"] = entity.visible;
    json["bounce"] = entity.bounce;
    json["position"] = Vec3ToJson(entity.position);
    json["rotation"] = Vec3ToJson(entity.rotation);
    json["scale"] = Vec3ToJson(entity.scale);
    json["linearVelocity"] = Vec3ToJson(entity.linearVelocity);
    json["angularVelocity"] = Vec3ToJson(entity.angularVelocity);
    json["colliderManual"] = entity.colliderManual;
    json["colliderType"] = entity.colliderType;
    json["simulatePhysics"] = entity.simulatePhysics;
    json["isStatic"] = entity.isStatic;
    json["useGravity"] = entity.useGravity;
    if (entity.colliderManual)
    {
        json["colliderHalfExtents"] = Vec3ToJson(entity.colliderHalfExtents);
        json["colliderOffset"] = Vec3ToJson(entity.colliderOffset);
    }
    return json;
}

EcsDemoEntityConfig EntityFromJson(const nlohmann::json& json)
{
    EcsDemoEntityConfig entity;
    entity.tag = json.value("tag", std::string());
    entity.meshPath = json.value("meshPath", std::string());
    entity.texturePath = json.value("texturePath", std::string());
    entity.shaderPath = json.value("shaderPath", std::string());
    entity.materialPath = json.value("materialPath", std::string());
    if (json.contains("color") && json["color"].is_array())
    {
        const auto& color = json["color"];
        for (std::size_t i = 0; i < color.size() && i < entity.materialTint.size(); ++i)
            entity.materialTint[i] = color[i].get<float>();
    }
    entity.visible = json.value("visible", true);
    entity.bounce = json.value("bounce", true);
    ReadVec3(json, "position", entity.position);
    ReadVec3(json, "rotation", entity.rotation);
    ReadVec3(json, "scale", entity.scale);
    ReadVec3(json, "linearVelocity", entity.linearVelocity);
    ReadVec3(json, "angularVelocity", entity.angularVelocity);
    ReadVec3(json, "colliderHalfExtents", entity.colliderHalfExtents);
    ReadVec3(json, "colliderOffset", entity.colliderOffset);
    entity.colliderType = json.value("colliderType", std::string("box"));
    entity.colliderManual = json.value("colliderManual", false) ||
        json.contains("colliderHalfExtents") || json.contains("colliderOffset");
    entity.simulatePhysics = json.value("simulatePhysics", true);
    entity.isStatic = json.value("isStatic", false);
    entity.useGravity = json.value("useGravity", true);
    return entity;
}
}

bool SceneSerializer::LoadEntityConfigs(
    const std::filesystem::path& path,
    std::vector<EcsDemoEntityConfig>& outEntities,
    std::string* outError)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        SetError(outError, "SceneSerializer: cannot open scene file: " + path.string());
        return false;
    }

    nlohmann::json scene;
    try
    {
        file >> scene;
    }
    catch (const std::exception& e)
    {
        SetError(outError, std::string("SceneSerializer: JSON parse failed: ") + e.what());
        return false;
    }

    const auto& entities = scene.contains("entities") ? scene["entities"] : scene;
    if (!entities.is_array())
    {
        SetError(outError, "SceneSerializer: scene must contain an entities array");
        return false;
    }

    outEntities.clear();
    for (const auto& entityJson : entities)
        outEntities.push_back(EntityFromJson(entityJson));

    Logger::Get().Info(
        "SceneSerializer: loaded scene=" + path.string() +
        " entities=" + std::to_string(outEntities.size()));
    return true;
}

bool SceneSerializer::SaveEntityConfigs(
    const std::filesystem::path& path,
    const std::vector<EcsDemoEntityConfig>& entities,
    std::string* outError)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    nlohmann::json scene;
    scene["version"] = 1;
    scene["entities"] = nlohmann::json::array();
    for (const auto& entity : entities)
        scene["entities"].push_back(EntityToJson(entity));

    std::ofstream file(path);
    if (!file.is_open())
    {
        SetError(outError, "SceneSerializer: cannot write scene file: " + path.string());
        return false;
    }

    file << scene.dump(2);
    Logger::Get().Info("SceneSerializer: saved scene=" + path.string());
    return true;
}

bool SceneSerializer::SaveWorld(
    const std::filesystem::path& path,
    ecs::World& world,
    std::string* outError)
{
    std::vector<EcsDemoEntityConfig> entities;
    world.ForEach<ecs::TransformComponent, ecs::MeshRendererComponent>(
        [&](ecs::Entity entity, ecs::TransformComponent& transform, ecs::MeshRendererComponent& meshRenderer)
        {
            EcsDemoEntityConfig config;
            if (const auto* tag = world.GetComponent<ecs::TagComponent>(entity))
                config.tag = tag->name;
            config.meshPath = meshRenderer.meshPath;
            config.texturePath = meshRenderer.texturePath;
            config.shaderPath = meshRenderer.shaderPath;
            config.visible = meshRenderer.visible;
            if (const auto* material = world.GetComponent<ecs::MaterialComponent>(entity))
            {
                config.materialPath = material->materialPath;
                config.shaderPath = material->shaderPath.empty() ? config.shaderPath : material->shaderPath;
                config.texturePath = material->texturePath.empty() ? config.texturePath : material->texturePath;
                for (std::size_t i = 0; i < config.materialTint.size(); ++i)
                    config.materialTint[i] = material->tint[i];
            }
            config.bounce = world.HasComponent<ecs::BoundsBounceComponent>(entity);
            config.position = transform.position;
            config.rotation = transform.rotation;
            config.scale = transform.scale;
            if (const auto* velocity = world.GetComponent<ecs::VelocityComponent>(entity))
            {
                config.linearVelocity = velocity->linear;
                config.angularVelocity = velocity->angular;
            }
            entities.push_back(config);
        });

    return SaveEntityConfigs(path, entities, outError);
}
