#include "Application.h"
#include "AssetDependencyValidation.h"
#include "AssetPaths.h"
#include "Logger.h"
#include "ConfigLoader.h"

#include "../ecs/components/BoundsBounceComponent.h"
#include "../ecs/components/ColliderComponent.h"
#include "../ecs/components/RigidbodyComponent.h"
#include "../ecs/components/MaterialComponent.h"
#include "../ecs/components/LightComponent.h"
#include "../ecs/components/MeshRendererComponent.h"
#include "../ecs/components/TagComponent.h"
#include "../ecs/components/TransformComponent.h"
#include "../ecs/components/VelocityComponent.h"
#include "../ecs/systems/BoundsBounceSystem.h"
#include "../ecs/systems/PhysicsSystem.h"
#include "../platform/GlfwWindow.h"
#include "../render/IRenderAdapter.h"
#include "../resources/ResourceManager.h"
#include "../scene/SceneSerializer.h"
#include <GLFW/glfw3.h>
#include <imgui.h>

#include "../game/states/LoadingState.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <array>
#include <sstream>
#include <unordered_set>

Application::Application() = default;
Application::~Application() = default;

static const char* BackendToString(RenderBackend b)
{
    switch (b)
    {
    case RenderBackend::DX12:   return "DX12";
    case RenderBackend::Vulkan: return "Vulkan";
    default:                    return "Unknown";
    }
}

static WindowConfig BuildDefaultWindowConfig(RenderBackend backend)
{
    WindowConfig cfg;
    cfg.backend = backend;
    cfg.title = "WhispEngine";

    if (backend == RenderBackend::Vulkan)
    {
        cfg.clear[0] = 0.85f;
        cfg.clear[1] = 0.25f;
        cfg.clear[2] = 0.25f;
        cfg.clear[3] = 1.0f;
    }
    else
    {
        cfg.clear[0] = 0.25f;
        cfg.clear[1] = 0.85f;
        cfg.clear[2] = 0.25f;
        cfg.clear[3] = 1.0f;
    }

    return cfg;
}

static std::string ResolveConfigPath()
{
    namespace fs = std::filesystem;

    const fs::path relativeConfig = fs::path("engine") / "config" / "app.json";
    const fs::path cwd = fs::current_path();

    for (fs::path probe = cwd; !probe.empty(); )
    {
        const fs::path candidate = probe / relativeConfig;
        if (fs::exists(candidate))
            return candidate.string();

        if (!probe.has_parent_path() || probe == probe.parent_path())
            break;

        probe = probe.parent_path();
    }

    const fs::path localCandidate = cwd / relativeConfig;
    if (fs::exists(localCandidate))
        return localCandidate.string();

    return relativeConfig.string();
}

static bool HasNonDefaultTint(const std::array<float, 4>& tint)
{
    return tint[0] != 1.0f || tint[1] != 1.0f || tint[2] != 1.0f || tint[3] != 1.0f;
}

static std::uint64_t BuildCollisionPairKey(std::size_t a, std::size_t b)
{
    const std::uint64_t lo = static_cast<std::uint64_t>(std::min(a, b));
    const std::uint64_t hi = static_cast<std::uint64_t>(std::max(a, b));
    return (hi << 32) ^ lo;
}

static bool TryBuildMeshCollider(
    ResourceManager* resourceManager,
    const std::string& meshPath,
    const ecs::Vec3& scale,
    ecs::Vec3& outHalfExtents,
    ecs::Vec3& outOffset)
{
    if (resourceManager == nullptr)
        return false;
    const std::string meshKey = AssetPaths::NormalizeAssetKey(meshPath);
    if (meshKey.empty())
        return false;
    const auto meshResource = resourceManager->Load<MeshResource>(meshKey);
    if (meshResource == nullptr || !meshResource->IsUsable())
        return false;
    const auto& vertices = meshResource->GetData().meshData.vertices;
    if (vertices.empty())
        return false;

    ecs::Vec3 minV{ vertices[0].position[0], vertices[0].position[1], vertices[0].position[2] };
    ecs::Vec3 maxV = minV;
    for (const auto& v : vertices)
    {
        if (v.position[0] < minV.x) minV.x = v.position[0];
        if (v.position[1] < minV.y) minV.y = v.position[1];
        if (v.position[2] < minV.z) minV.z = v.position[2];
        if (v.position[0] > maxV.x) maxV.x = v.position[0];
        if (v.position[1] > maxV.y) maxV.y = v.position[1];
        if (v.position[2] > maxV.z) maxV.z = v.position[2];
    }

    outHalfExtents = ecs::Vec3{
        (maxV.x - minV.x) * 0.5f * scale.x,
        (maxV.y - minV.y) * 0.5f * scale.y,
        (maxV.z - minV.z) * 0.5f * scale.z
    };
    outOffset = ecs::Vec3{
        (maxV.x + minV.x) * 0.5f * scale.x,
        (maxV.y + minV.y) * 0.5f * scale.y,
        (maxV.z + minV.z) * 0.5f * scale.z
    };
    return true;
}

static float Dot(const ecs::Vec3& lhs, const ecs::Vec3& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

static ecs::Vec3 Cross(const ecs::Vec3& lhs, const ecs::Vec3& rhs)
{
    return ecs::Vec3{
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x
    };
}

static float Length(const ecs::Vec3& vector)
{
    return std::sqrt(Dot(vector, vector));
}

static ecs::Vec3 Normalize(const ecs::Vec3& vector)
{
    const float length = Length(vector);
    if (length <= 0.0001f)
        return ecs::Vec3{};

    const float invLength = 1.0f / length;
    return ecs::Vec3{ vector.x * invLength, vector.y * invLength, vector.z * invLength };
}

static ecs::Vec3 Add(const ecs::Vec3& lhs, const ecs::Vec3& rhs)
{
    return ecs::Vec3{ lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
}

static ecs::Vec3 Scale(const ecs::Vec3& vector, float scalar)
{
    return ecs::Vec3{ vector.x * scalar, vector.y * scalar, vector.z * scalar };
}

static ecs::Vec3 BuildCameraForward(float yaw, float pitch)
{
    const float cosPitch = std::cos(pitch);
    return Normalize(ecs::Vec3{
        std::sin(yaw) * cosPitch,
        std::sin(pitch),
        std::cos(yaw) * cosPitch
    });
}

constexpr float kMaxCameraPitch = 1.55334306f;
constexpr float kFallbackAspectRatio = 16.0f / 9.0f;

void Application::RunResourceBootstrapCheck()
{
    if (m_ResourceManager == nullptr)
        return;

    Logger::Get().Info("Resource bootstrap: starting CPU-side resource loading self-check");

    const auto mesh = m_ResourceManager->Load<MeshResource>("models/validation_triangle.obj");
    if (mesh != nullptr)
    {
        Logger::Get().Info(
            "Resource bootstrap: mesh key=" + mesh->GetKey() +
            " loaded=" + std::string(mesh->IsLoaded() ? "true" : "false") +
            " fallback=" + std::string(mesh->UsesFallback() ? "true" : "false"));
    }

    const auto texture = m_ResourceManager->Load<TextureResource>("textures/validation_checker.png");
    if (texture != nullptr)
    {
        Logger::Get().Info(
            "Resource bootstrap: texture key=" + texture->GetKey() +
            " loaded=" + std::string(texture->IsLoaded() ? "true" : "false") +
            " fallback=" + std::string(texture->UsesFallback() ? "true" : "false"));
    }

    const auto shader = m_ResourceManager->Load<ShaderResource>("dx12/textured.hlsl");
    if (shader != nullptr)
    {
        Logger::Get().Info(
            "Resource bootstrap: shader key=" + shader->GetKey() +
            " loaded=" + std::string(shader->IsLoaded() ? "true" : "false") +
            " fallback=" + std::string(shader->UsesFallback() ? "true" : "false"));
    }

    const auto material = m_ResourceManager->Load<MaterialResource>("materials/validation_checker.material.json");
    if (material != nullptr)
    {
        Logger::Get().Info(
            "Resource bootstrap: material key=" + material->GetKey() +
            " loaded=" + std::string(material->IsLoaded() ? "true" : "false") +
            " fallback=" + std::string(material->UsesFallback() ? "true" : "false"));
    }
}

void Application::PreloadSceneResourcesAsync(const std::vector<EcsDemoEntityConfig>& entities)
{
    if (m_ResourceManager == nullptr)
        return;

    std::unordered_set<std::string> meshKeys;
    std::unordered_set<std::string> textureKeys;
    std::unordered_set<std::string> shaderKeys;
    std::unordered_set<std::string> materialKeys;

    for (const auto& entity : entities)
    {
        const std::string meshKey = AssetPaths::NormalizeAssetKey(entity.meshPath);
        if (!meshKey.empty() && meshKeys.insert(meshKey).second)
            (void)m_ResourceManager->LoadAsync<MeshResource>(meshKey);

        const std::string materialKey = AssetPaths::NormalizeAssetKey(entity.materialPath);
        if (!materialKey.empty() && materialKeys.insert(materialKey).second)
            (void)m_ResourceManager->LoadAsync<MaterialResource>(materialKey);

        const std::string textureKey = AssetPaths::NormalizeAssetKey(entity.texturePath);
        if (!textureKey.empty() && textureKeys.insert(textureKey).second)
            (void)m_ResourceManager->LoadAsync<TextureResource>(textureKey);

        const std::string shaderKey = AssetPaths::NormalizeShaderKey(entity.shaderPath);
        if (!shaderKey.empty() && shaderKeys.insert(shaderKey).second)
            (void)m_ResourceManager->Load<ShaderResource>(shaderKey);
    }

    Logger::Get().Info(
        "Application: preloading scene resources async mesh=" + std::to_string(meshKeys.size()) +
        " texture=" + std::to_string(textureKeys.size()) +
        " shader=" + std::to_string(shaderKeys.size()) +
        " material=" + std::to_string(materialKeys.size()));
}

std::vector<EcsDemoEntityConfig> Application::BuildDefaultEcsDemoEntities()
{
    std::vector<EcsDemoEntityConfig> entities(4);

    entities[0].tag = "GroundPlane";
    entities[0].meshPath = "models/validation_cube.obj";
    entities[0].materialPath = "materials/white.material.json";
    entities[0].position = ecs::Vec3{ 0.0f, -1.0f, 0.0f };
    entities[0].scale = ecs::Vec3{ 8.0f, 0.05f, 8.0f };
    entities[0].bounce = false;
    entities[0].linearVelocity = ecs::Vec3{};

    entities[1].tag = "AfricanHead_Center";
    entities[1].meshPath = "models/african_head.obj";
    entities[1].materialPath = "materials/african_head.material.json";
    entities[1].position = ecs::Vec3{ 0.0f, 1.1f, 0.0f };
    entities[1].rotation = ecs::Vec3{ 0.0f, 3.1415926f, 0.0f };
    entities[1].scale = ecs::Vec3{ 0.68f, 0.68f, 0.68f };
    entities[1].bounce = false;

    entities[2].tag = "AfricanHead_Left";
    entities[2].meshPath = "models/african_head.obj";
    entities[2].materialPath = "materials/african_head.material.json";
    entities[2].materialTint = { 0.80f, 0.88f, 1.0f, 1.0f };
    entities[2].position = ecs::Vec3{ -0.60f, 0.9f, 0.0f };
    entities[2].rotation = ecs::Vec3{ 0.0f, 2.72f, 0.0f };
    entities[2].scale = ecs::Vec3{ 0.32f, 0.32f, 0.32f };
    entities[2].bounce = false;

    entities[3].tag = "AfricanHead_Right";
    entities[3].meshPath = "models/african_head.obj";
    entities[3].materialPath = "materials/african_head.material.json";
    entities[3].materialTint = { 1.0f, 0.90f, 0.82f, 1.0f };
    entities[3].position = ecs::Vec3{ 0.60f, 0.9f, 0.0f };
    entities[3].rotation = ecs::Vec3{ 0.0f, 3.56f, 0.0f };
    entities[3].scale = ecs::Vec3{ 0.32f, 0.32f, 0.32f };
    entities[3].bounce = false;

    return entities;
}

void Application::RunEcsBootstrapCheck()
{
    Logger::Get().Info("ECS bootstrap: starting entity and component self-check");

    const ecs::Entity first = m_World.CreateEntity();
    const ecs::Entity second = m_World.CreateEntity();
    const ecs::Entity third = m_World.CreateEntity();

    Logger::Get().Info("ECS bootstrap: created " + m_World.DebugDescribeEntity(first));
    Logger::Get().Info("ECS bootstrap: created " + m_World.DebugDescribeEntity(second));
    Logger::Get().Info("ECS bootstrap: created " + m_World.DebugDescribeEntity(third));

    const bool destroyed = m_World.DestroyEntity(second);
    Logger::Get().Info(std::string("ECS bootstrap: destroy second entity -> ") + (destroyed ? "ok" : "failed"));
    Logger::Get().Info("ECS bootstrap: second entity after destroy -> " + m_World.DebugDescribeEntity(second));

    const ecs::Entity recycled = m_World.CreateEntity();
    Logger::Get().Info("ECS bootstrap: recycled slot into " + m_World.DebugDescribeEntity(recycled));

    auto& firstTransform = m_World.AddComponent<ecs::TransformComponent>(first);
    firstTransform.position = ecs::Vec3{ 1.5f, -0.5f, 0.0f };
    firstTransform.scale = ecs::Vec3{ 2.0f, 2.0f, 1.0f };

    Logger::Get().Info(std::string("ECS bootstrap: first has transform -> ") +
        (m_World.HasComponent<ecs::TransformComponent>(first) ? "true" : "false"));

    if (const auto* transform = m_World.GetComponent<ecs::TransformComponent>(first))
    {
        std::ostringstream ss;
        ss << "ECS bootstrap: first transform pos=("
           << transform->position.x << ", " << transform->position.y << ", " << transform->position.z
           << ") scale=("
           << transform->scale.x << ", " << transform->scale.y << ", " << transform->scale.z
           << ") rot=("
           << transform->rotation.x << ", " << transform->rotation.y << ", " << transform->rotation.z << ")";
        Logger::Get().Info(ss.str());
    }

    auto& firstTag = m_World.AddComponent<ecs::TagComponent>(first);
    firstTag.name = "BootstrapEntity";
    Logger::Get().Info(std::string("ECS bootstrap: first tag -> ") + firstTag.name);

    auto& firstMeshRenderer = m_World.AddComponent<ecs::MeshRendererComponent>(first);
    firstMeshRenderer.meshPath = "models/validation_triangle.obj";
    auto& firstMaterial = m_World.AddComponent<ecs::MaterialComponent>(first);
    firstMaterial.materialPath = "materials/validation_checker.material.json";
    Logger::Get().Info(
        "ECS bootstrap: first mesh renderer asset refs -> mesh=" + firstMeshRenderer.meshPath +
        " material=" + firstMaterial.materialPath);

    auto& recycledTransform = m_World.AddComponent<ecs::TransformComponent>(recycled);
    recycledTransform.position = ecs::Vec3{ -2.0f, 3.0f, 0.0f };
    recycledTransform.rotation = ecs::Vec3{ 0.0f, 0.0f, 0.5f };
    recycledTransform.scale = ecs::Vec3{ 0.75f, 0.75f, 1.0f };
    Logger::Get().Info(std::string("ECS bootstrap: recycled has transform before remove -> ") +
        (m_World.HasComponent<ecs::TransformComponent>(recycled) ? "true" : "false"));

    const bool removedTransform = m_World.RemoveComponent<ecs::TransformComponent>(recycled);
    Logger::Get().Info(std::string("ECS bootstrap: remove transform from recycled -> ") +
        (removedTransform ? "ok" : "failed"));
    Logger::Get().Info(std::string("ECS bootstrap: recycled has transform after remove -> ") +
        (m_World.HasComponent<ecs::TransformComponent>(recycled) ? "true" : "false"));

    auto& thirdTransform = m_World.AddComponent<ecs::TransformComponent>(third);
    thirdTransform.position = ecs::Vec3{ 4.0f, 2.0f, 0.0f };
    thirdTransform.rotation = ecs::Vec3{};
    thirdTransform.scale = ecs::Vec3{ 1.25f, 1.25f, 1.0f };
    const bool destroyedThird = m_World.DestroyEntity(third);
    Logger::Get().Info(std::string("ECS bootstrap: destroy third entity with transform -> ") +
        (destroyedThird ? "ok" : "failed"));
    Logger::Get().Info(std::string("ECS bootstrap: third has transform after destroy -> ") +
        (m_World.HasComponent<ecs::TransformComponent>(third) ? "true" : "false"));
    Logger::Get().Info(std::string("ECS bootstrap: first has tag -> ") +
        (m_World.HasComponent<ecs::TagComponent>(first) ? "true" : "false"));
    Logger::Get().Info(std::string("ECS bootstrap: first has mesh renderer -> ") +
        (m_World.HasComponent<ecs::MeshRendererComponent>(first) ? "true" : "false"));

    Logger::Get().Info("ECS bootstrap: alive entities=" + std::to_string(m_World.GetAliveCount()) +
        ", capacity=" + std::to_string(m_World.GetCapacity()));

    m_World.Clear();
    Logger::Get().Info("ECS bootstrap: world reset after self-check");
}

void Application::SetupEcsRuntimeDemo()
{
    m_World.ClearSystems();
    m_PhysicsSystem = &m_World.AddSystem<ecs::PhysicsSystem>(
        &m_EventBus,
        m_Config.physics.gravity,
        m_Config.physics.linearDamping,
        m_Config.physics.substeps,
        m_Config.physics.restitution,
        m_Config.physics.friction,
        m_Config.physics.solverIterations,
        m_Config.physics.sphereMaxSpeed,
        m_Config.physics.spherePenetrationEpsilon,
        m_Config.physics.sphereVelocityEpsilon,
        m_Config.physics.dynamicBoxSphereCorrectionPercent);
    m_PhysicsSystem->SetEnabled(m_EditorPlayMode);
    m_RenderSystem = &m_World.AddSystem<ecs::RenderSystem>();
    m_RenderSystem->SetResourceManager(m_ResourceManager.get());
    m_RenderSystem->SetDebugCollidersEnabled(m_DebugCollidersEnabled);

    m_EcsDebugEntities.clear();
    const std::vector<EcsDemoEntityConfig> entities =
        m_Config.ecsDemo.initialEntities.empty() ? BuildDefaultEcsDemoEntities() : m_Config.ecsDemo.initialEntities;
    PreloadSceneResourcesAsync(entities);

    for (const auto& entityCfg : entities)
    {
        m_EcsDebugEntities.push_back(SpawnEcsDemoEntity(entityCfg));
    }

    // Small cube pyramid for interactive shooting tests.
    for (int layer = 0; layer < 4; ++layer)
    {
        const int count = 4 - layer;
        for (int i = 0; i < count; ++i)
        {
            EcsDemoEntityConfig cubeCfg;
            cubeCfg.tag = "PyramidCube_" + std::to_string(layer) + "_" + std::to_string(i);
            cubeCfg.meshPath = "models/validation_cube.obj";
            cubeCfg.materialPath = "materials/blue.material.json";
            cubeCfg.scale = ecs::Vec3{ 0.18f, 0.18f, 0.18f };
            cubeCfg.position = ecs::Vec3{
                -0.35f + static_cast<float>(i) * 0.20f + static_cast<float>(layer) * 0.10f,
                -0.92f + static_cast<float>(layer) * 0.22f,
                0.45f
            };
            m_EcsDebugEntities.push_back(SpawnEcsDemoEntity(cubeCfg));
        }
    }


    auto spawnLight = [&](const char* name, ecs::LightType type, const ecs::Vec3& pos, const ecs::Vec3& rot, const ecs::Vec3& color, float intensity, float range)
    {
        const ecs::Entity e = m_World.CreateEntity();
        auto& t = m_World.AddComponent<ecs::TransformComponent>(e);
        t.position = pos; t.rotation = rot; t.scale = ecs::Vec3{1.0f,1.0f,1.0f};
        auto& tag = m_World.AddComponent<ecs::TagComponent>(e);
        tag.name = name;
        auto& l = m_World.AddComponent<ecs::LightComponent>(e);
        l.type = type; l.color = color; l.intensity = intensity; l.range = range;
    };
    spawnLight("Directional Light", ecs::LightType::Directional, ecs::Vec3{0.0f, 4.0f, 0.0f}, ecs::Vec3{-0.7f, 0.6f, 0.0f}, ecs::Vec3{1.0f,0.98f,0.9f}, 1.2f, 0.0f);
    spawnLight("Point Light", ecs::LightType::Point, ecs::Vec3{1.2f, 1.7f, 0.6f}, ecs::Vec3{}, ecs::Vec3{1.0f,0.45f,0.3f}, 4.0f, 5.5f);
    spawnLight("Spot Light", ecs::LightType::Spot, ecs::Vec3{-1.5f, 2.0f, -0.5f}, ecs::Vec3{-0.6f, -0.6f, 0.0f}, ecs::Vec3{0.4f,0.7f,1.0f}, 5.0f, 7.0f);

    if (m_ResourceManager != nullptr)
    {
        m_ResourceManager->WatchForHotReload<MeshResource>("models/african_head.obj");
        m_ResourceManager->WatchForHotReload<TextureResource>("textures/african_head_diffuse.dds");
        m_ResourceManager->WatchForHotReload<MaterialResource>("materials/african_head.material.json");
        m_ResourceManager->WatchForHotReload<MeshResource>("models/validation_cube.obj");
        m_ResourceManager->WatchForHotReload<TextureResource>("textures/validation_checker.png");
        m_ResourceManager->WatchForHotReload<ShaderResource>("dx12/textured.hlsl");
        m_ResourceManager->WatchForHotReload<MaterialResource>("materials/validation_checker.material.json");
    }

    m_EcsDebugLogTimer = 0.0f;

    Logger::Get().Info("ECS runtime: physics system registered (motion system disabled to avoid double integration)");
    Logger::Get().Info("ECS runtime: demo scene created with " + std::to_string(m_EcsDebugEntities.size()) + " ECS entities");
    Logger::Get().Info("ECS runtime: render system registered");

    std::string saveError;
    const auto snapshotPath = AssetPaths::ResolveAssetOutputPath("scenes/pz3_runtime_snapshot.json");
    if (!SceneSerializer::SaveWorld(snapshotPath, m_World, &saveError) && !saveError.empty())
        Logger::Get().Warn(saveError);
}

void Application::InitializeConfigHotReload()
{
    std::error_code ec;
    m_HasConfigWatch = false;
    m_HasSceneWatch = false;

    if (!m_ConfigWatchPath.empty() && std::filesystem::exists(m_ConfigWatchPath, ec))
    {
        m_ConfigWriteTime = std::filesystem::last_write_time(m_ConfigWatchPath, ec);
        m_HasConfigWatch = !static_cast<bool>(ec);
    }

    m_SceneWatchPath.clear();
    if (!m_Config.ecsDemo.sceneFile.empty())
    {
        m_SceneWatchPath = AssetPaths::ResolveAssetPath(m_Config.ecsDemo.sceneFile);
        if (!m_SceneWatchPath.empty() && std::filesystem::exists(m_SceneWatchPath, ec))
        {
            m_SceneWriteTime = std::filesystem::last_write_time(m_SceneWatchPath, ec);
            m_HasSceneWatch = !static_cast<bool>(ec);
        }
    }
}

bool Application::ReloadSceneFromCurrentConfig(const char* reason)
{
    if (m_Config.ecsDemo.sceneFile.empty())
    {
        SetupEcsRuntimeDemo();
        Logger::Get().Info(std::string("Application: rebuilt ECS demo from config entities because ") + reason);
        return true;
    }

    std::string sceneError;
    std::vector<EcsDemoEntityConfig> sceneEntities;
    const auto scenePath = AssetPaths::ResolveAssetPath(m_Config.ecsDemo.sceneFile);
    if (!SceneSerializer::LoadEntityConfigs(scenePath, sceneEntities, &sceneError))
    {
        if (!sceneError.empty())
            Logger::Get().Warn(sceneError);
        return false;
    }

    m_Config.ecsDemo.initialEntities = std::move(sceneEntities);
    SetupEcsRuntimeDemo();
    Logger::Get().Info(std::string("Application: hot-reloaded ECS scene because ") + reason);
    return true;
}

bool Application::SaveCurrentScene(std::string* outError)
{
    std::string sceneKey = m_Config.ecsDemo.sceneFile;
    if (sceneKey.empty())
        sceneKey = "scenes/editor_scene.json";

    const std::filesystem::path scenePath = AssetPaths::ResolveAssetOutputPath(sceneKey);
    if (scenePath.empty())
    {
        const std::string error = "Application: cannot resolve scene output path for " + sceneKey;
        if (outError != nullptr)
            *outError = error;
        Logger::Get().Warn(error);
        return false;
    }

    std::string saveError;
    if (!SceneSerializer::SaveWorld(scenePath, m_World, &saveError))
    {
        if (outError != nullptr)
            *outError = saveError;
        if (!saveError.empty())
            Logger::Get().Warn(saveError);
        return false;
    }

    m_Config.ecsDemo.sceneFile = sceneKey;
    std::error_code ec;
    m_SceneWatchPath = scenePath;
    m_SceneWriteTime = std::filesystem::last_write_time(scenePath, ec);
    m_HasSceneWatch = !static_cast<bool>(ec);
    Logger::Get().Info("Application: editor saved scene " + scenePath.string());
    return true;
}

bool Application::LoadCurrentScene(std::string* outError)
{
    if (!ReloadSceneFromCurrentConfig("editor load requested"))
    {
        const std::string error = "Application: editor scene load failed";
        if (outError != nullptr)
            *outError = error;
        Logger::Get().Warn(error);
        return false;
    }

    InitializeConfigHotReload();
    return true;
}

void Application::ConfigureInputBindings()
{
    auto* primary = dynamic_cast<GlfwWindow*>(GetWindow());
    if (primary == nullptr)
        return;

    m_InputManager.SetWindow(primary->GetGlfwHandle());
    m_InputManager.BindAction("EnterGameplay", GLFW_KEY_ENTER);
    m_InputManager.BindAction("MoveForward", GLFW_KEY_W);
    m_InputManager.BindAction("MoveBackward", GLFW_KEY_S);
    m_InputManager.BindAction("MoveLeft", GLFW_KEY_A);
    m_InputManager.BindAction("MoveRight", GLFW_KEY_D);
    m_InputManager.BindAction("MoveUp", GLFW_KEY_SPACE);
    m_InputManager.BindAction("MoveDown", GLFW_KEY_LEFT_CONTROL);
    m_InputManager.BindAction("PauseToMenu", GLFW_KEY_ESCAPE);
    m_InputManager.BindAction("SpawnEntity", GLFW_KEY_SPACE);
    m_InputManager.BindAction("DestroyEntity", GLFW_KEY_BACKSPACE);
    m_InputManager.BindAction("FireProjectile", GLFW_KEY_F);
    m_InputManager.BindAction("ToggleDebugColliders", GLFW_KEY_F3);
    m_InputManager.BindAction("ToggleLightDebug", GLFW_KEY_F4);
    m_InputManager.BindAction("ToggleShadows", GLFW_KEY_F5);
}

void Application::PollConfigHotReload()
{
    std::error_code ec;

    if (m_HasConfigWatch && std::filesystem::exists(m_ConfigWatchPath, ec))
    {
        const auto currentWriteTime = std::filesystem::last_write_time(m_ConfigWatchPath, ec);
        if (!ec && currentWriteTime > m_ConfigWriteTime)
        {
            m_ConfigWriteTime = currentWriteTime;

            AppConfig reloadedConfig;
            std::string configError;
            if (ConfigLoader::Load(m_ConfigWatchPath.string(), reloadedConfig, &configError))
            {
                if (reloadedConfig.activeBackend != m_Config.activeBackend)
                {
                    Logger::Get().Warn(
                        "Application: active renderer change detected in app.json. Runtime backend switch is deferred until restart.");
                    reloadedConfig.activeBackend = m_Config.activeBackend;
                }

                for (auto& wc : m_Windows)
                {
                    for (const auto& windowCfg : reloadedConfig.windows)
                    {
                        if (windowCfg.backend != wc.backend)
                            continue;

                        wc.baseTitle = windowCfg.title + " | " + BackendToString(wc.backend);
                        wc.clear[0] = windowCfg.clear[0];
                        wc.clear[1] = windowCfg.clear[1];
                        wc.clear[2] = windowCfg.clear[2];
                        wc.clear[3] = windowCfg.clear[3];
                        break;
                    }
                }

                const bool sceneSourceChanged = reloadedConfig.ecsDemo.sceneFile != m_Config.ecsDemo.sceneFile;
                const bool rebuildFromConfigEntities = reloadedConfig.ecsDemo.sceneFile.empty();
                m_Config = std::move(reloadedConfig);
                InitializeConfigHotReload();
                Logger::Get().Info("Application: hot-reloaded app config");

                if (sceneSourceChanged || rebuildFromConfigEntities)
                    (void)ReloadSceneFromCurrentConfig("app config changed");
            }
            else if (!configError.empty())
            {
                Logger::Get().Warn(configError);
            }
        }
    }

    if (m_HasSceneWatch && std::filesystem::exists(m_SceneWatchPath, ec))
    {
        const auto currentWriteTime = std::filesystem::last_write_time(m_SceneWatchPath, ec);
        if (!ec && currentWriteTime > m_SceneWriteTime)
        {
            m_SceneWriteTime = currentWriteTime;
            (void)ReloadSceneFromCurrentConfig("scene file changed");
            InitializeConfigHotReload();
        }
    }
}

ecs::Entity Application::SpawnEcsDemoEntity(const EcsDemoEntityConfig& entityCfg)
{
    const ecs::Entity entity = m_World.CreateEntity();
    const std::size_t entityOrdinal = m_EcsDebugEntities.size();

    auto& transform = m_World.AddComponent<ecs::TransformComponent>(entity);
    transform.position = entityCfg.position;
    transform.rotation = entityCfg.rotation;
    transform.scale = entityCfg.scale;

    auto& velocity = m_World.AddComponent<ecs::VelocityComponent>(entity);
    velocity.linear = entityCfg.linearVelocity;
    velocity.angular = entityCfg.angularVelocity;

    auto& tag = m_World.AddComponent<ecs::TagComponent>(entity);
    tag.name = entityCfg.tag.empty() ? ("DemoEntity_" + std::to_string(entityOrdinal)) : entityCfg.tag;

    auto& meshRenderer = m_World.AddComponent<ecs::MeshRendererComponent>(entity);
    meshRenderer.meshPath = entityCfg.meshPath;
    meshRenderer.visible = entityCfg.visible;

    if (!entityCfg.materialPath.empty() ||
        !entityCfg.texturePath.empty() ||
        !entityCfg.shaderPath.empty() ||
        HasNonDefaultTint(entityCfg.materialTint))
    {
        auto& material = m_World.AddComponent<ecs::MaterialComponent>(entity);
        material.materialPath = entityCfg.materialPath;
        material.texturePath = entityCfg.texturePath;
        material.shaderPath = entityCfg.shaderPath;
        for (std::size_t i = 0; i < entityCfg.materialTint.size(); ++i)
            material.tint[i] = entityCfg.materialTint[i];
    }

    if (entityCfg.bounce)
        m_World.AddComponent<ecs::BoundsBounceComponent>(entity);

    auto& rigidbody = m_World.AddComponent<ecs::RigidbodyComponent>(entity);
    rigidbody.useGravity = entityCfg.useGravity;
    rigidbody.mass = 1.0f;
    rigidbody.isStatic = entityCfg.isStatic || tag.name == "GroundPlane";
    rigidbody.simulatePhysics = entityCfg.simulatePhysics;
    rigidbody.velocity = entityCfg.linearVelocity;
    auto& collider = m_World.AddComponent<ecs::ColliderComponent>(entity);
    collider.type = (entityCfg.colliderType == "sphere" || entityCfg.colliderType == "Sphere")
        ? ecs::ColliderType::Sphere
        : ecs::ColliderType::Box;
    collider.autoFitFromMesh = !entityCfg.colliderManual;
    collider.halfExtents = ecs::Vec3{ entityCfg.scale.x * 0.5f, entityCfg.scale.y * 0.5f, entityCfg.scale.z * 0.5f };
    collider.offset = ecs::Vec3{};
    if (!entityCfg.colliderManual)
    {
        ecs::Vec3 halfExtents{};
        ecs::Vec3 offset{};
        if (TryBuildMeshCollider(m_ResourceManager.get(), meshRenderer.meshPath, entityCfg.scale, halfExtents, offset))
        {
            collider.halfExtents = halfExtents;
            collider.offset = offset;
            if (meshRenderer.meshPath.find("african_head") != std::string::npos)
            {
                collider.halfExtents.x *= 1.08f;
                collider.halfExtents.y *= 1.10f;
                collider.halfExtents.z *= 1.18f;
                collider.offset.y += collider.halfExtents.y * 0.04f;
            }
        }
    }
    if (entityCfg.colliderManual)
    {
        collider.halfExtents = entityCfg.colliderHalfExtents;
        collider.offset = entityCfg.colliderOffset;
        collider.autoFitFromMesh = false;
    }

    if (tag.name == "RollingSphere")
    {
        const bool arcadeProfile = (m_Config.physics.rollingSphereProfile == "arcade");
        rigidbody.mass = arcadeProfile ? 0.62f : 0.70f;
        rigidbody.linearDampingMultiplier = arcadeProfile ? 0.0f : 0.10f;
        rigidbody.useAdvancedSphereStabilization = true;
        collider.friction = arcadeProfile ? 0.04f : 0.08f;
        collider.restitution = arcadeProfile ? 0.02f : 0.03f;
    }

    std::ostringstream ss;
    ss << "ECS runtime: spawned demo entity -> " << m_World.DebugDescribeEntity(entity)
       << " tag=" << tag.name
       << " mesh=" << meshRenderer.meshPath
       << " material=" << entityCfg.materialPath
       << " texture=" << entityCfg.texturePath
       << " shader=" << entityCfg.shaderPath;
    Logger::Get().Info(ss.str());
    return entity;
}

ecs::Entity Application::SpawnPhysicsProjectile()
{
    EcsDemoEntityConfig projectileCfg;
    projectileCfg.tag = "Projectile_" + std::to_string(m_EcsDebugEntities.size());
    projectileCfg.meshPath = "models/validation_cube.obj";
    projectileCfg.materialPath = "materials/blue.material.json";
    projectileCfg.scale = ecs::Vec3{ 0.15f, 0.15f, 0.15f };

    const ecs::Vec3 forward = BuildCameraForward(m_Camera.yaw, m_Camera.pitch);
    projectileCfg.position = m_Camera.position;
    projectileCfg.linearVelocity = Scale(forward, 14.0f);

    const ecs::Entity projectile = SpawnEcsDemoEntity(projectileCfg);
    m_EcsDebugEntities.push_back(projectile);

    if (auto* rb = m_World.GetComponent<ecs::RigidbodyComponent>(projectile))
    {
        rb->velocity = projectileCfg.linearVelocity;
        rb->mass = 2.0f;
    }
    if (auto* collider = m_World.GetComponent<ecs::ColliderComponent>(projectile))
    {
        collider->friction = 0.25f;
        collider->restitution = 0.10f;
    }

    Logger::Get().Info("Gameplay: F detected -> spawned projectile entity");
    return projectile;
}

void Application::EnterGameplayScene()
{
    Logger::Get().Info("Application: enter gameplay scene");
}

void Application::ExitGameplayScene()
{
    Logger::Get().Info("Application: exit gameplay scene");
}

void Application::UpdateCameraController(float dt)
{
    auto* primary = dynamic_cast<GlfwWindow*>(GetWindow());
    if (primary == nullptr)
        return;

    GLFWwindow* window = primary->GetGlfwHandle();
    if (window == nullptr)
        return;

    const double scrollDeltaY = primary->ConsumeScrollDeltaY();
    const bool focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window, &cursorX, &cursorY);

    const bool editorWantsMouse =
        ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
    const bool mouseOverViewport = m_EditorLayer.IsPointInsideViewport(cursorX, cursorY);
    const bool editorBlocksMouseLook = editorWantsMouse && !mouseOverViewport && !m_Camera.controlsActive;
    const bool rightMouseDown =
        focused &&
        !editorBlocksMouseLook &&
        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

    if (rightMouseDown && !m_Camera.previousRightMouseDown)
    {
        glfwGetCursorPos(window, &m_Camera.cursorXBeforeCapture, &m_Camera.cursorYBeforeCapture);

        int width = 0;
        int height = 0;
        glfwGetWindowSize(window, &width, &height);
        const double centerX = static_cast<double>(width) * 0.5;
        const double centerY = static_cast<double>(height) * 0.5;

        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

        glfwSetCursorPos(window, centerX, centerY);
        m_Camera.lastMouseX = centerX;
        m_Camera.lastMouseY = centerY;
        m_Camera.controlsActive = true;
    }
    else if ((!rightMouseDown || !focused) && m_Camera.controlsActive)
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);

        glfwSetCursorPos(window, m_Camera.cursorXBeforeCapture, m_Camera.cursorYBeforeCapture);
        m_Camera.controlsActive = false;
    }

    m_Camera.previousRightMouseDown = rightMouseDown;

    if (!m_Camera.controlsActive)
        return;

    if (std::abs(scrollDeltaY) > 0.001)
    {
        const float speedMultiplier =
            std::pow(m_Camera.scrollSpeedStepMultiplier, static_cast<float>(scrollDeltaY));
        m_Camera.moveSpeed = std::clamp(
            m_Camera.moveSpeed * speedMultiplier,
            m_Camera.minMoveSpeed,
            m_Camera.maxMoveSpeed);
    }

    glfwGetCursorPos(window, &cursorX, &cursorY);

    const float deltaX = static_cast<float>(cursorX - m_Camera.lastMouseX);
    const float deltaY = static_cast<float>(cursorY - m_Camera.lastMouseY);
    m_Camera.lastMouseX = cursorX;
    m_Camera.lastMouseY = cursorY;

    m_Camera.yaw += deltaX * m_Camera.mouseSensitivity;
    m_Camera.pitch = std::clamp(
        m_Camera.pitch - deltaY * m_Camera.mouseSensitivity,
        -kMaxCameraPitch,
        kMaxCameraPitch);

    const ecs::Vec3 worldUp{ 0.0f, 1.0f, 0.0f };
    const ecs::Vec3 forward = BuildCameraForward(m_Camera.yaw, m_Camera.pitch);
    const ecs::Vec3 right = Normalize(Cross(worldUp, forward));

    ecs::Vec3 movement{};
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        movement = Add(movement, forward);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        movement = Add(movement, Scale(forward, -1.0f));
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        movement = Add(movement, right);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        movement = Add(movement, Scale(right, -1.0f));
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        movement = Add(movement, worldUp);

    const bool downPressed =
        glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    if (downPressed)
        movement = Add(movement, Scale(worldUp, -1.0f));

    if (Length(movement) <= 0.0001f)
        return;

    float speed = m_Camera.moveSpeed;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        speed *= m_Camera.boostMultiplier;

    m_Camera.position = Add(
        m_Camera.position,
        Scale(Normalize(movement), speed * dt));
}

void Application::UpdateRenderSystemCamera(IWindow* window)
{
    if (m_RenderSystem == nullptr || window == nullptr)
        return;

    auto* glfwWindow = dynamic_cast<GlfwWindow*>(window);
    if (glfwWindow == nullptr || glfwWindow->GetGlfwHandle() == nullptr)
        return;

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(glfwWindow->GetGlfwHandle(), &framebufferWidth, &framebufferHeight);

    const float aspectRatio =
        framebufferHeight > 0
            ? static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight)
            : kFallbackAspectRatio;

    UpdateRenderSystemCameraAspect(aspectRatio);
}

void Application::UpdateRenderSystemCameraAspect(float aspectRatio)
{
    if (m_RenderSystem == nullptr)
        return;

    if (aspectRatio <= 0.0001f)
        aspectRatio = kFallbackAspectRatio;

    m_RenderSystem->SetCameraTransform(m_Camera.position, m_Camera.yaw, m_Camera.pitch);
    m_RenderSystem->SetCameraProjection(
        m_Camera.verticalFovRadians,
        aspectRatio,
        m_Camera.nearPlane,
        m_Camera.farPlane);
}

ecs::Entity Application::SpawnGameplayEntity()
{
    struct SpawnPreset
    {
        float x;
        float y;
        float sx;
        float sy;
        float sz;
        float yaw;
    };

    static constexpr std::array<SpawnPreset, 6> presets =
    {{
        { -0.65f,  0.15f, 0.30f, 0.30f, 0.30f, 2.84f },
        {  0.68f,  0.05f, 0.30f, 0.30f, 0.30f, 3.46f },
        { -0.15f,  0.72f, 0.26f, 0.26f, 0.26f, 3.14f },
        {  0.12f, -0.68f, 0.28f, 0.28f, 0.28f, 3.66f },
        { -0.78f, -0.12f, 0.24f, 0.24f, 0.24f, 2.58f },
        {  0.78f, -0.58f, 0.24f, 0.24f, 0.24f, 3.72f },
    }};

    const SpawnPreset& preset = presets[m_EcsDebugEntities.size() % presets.size()];
    EcsDemoEntityConfig entityCfg;
    entityCfg.tag = "SpawnedEntity_" + std::to_string(m_EcsDebugEntities.size());
    entityCfg.position = ecs::Vec3{ preset.x, preset.y, 0.0f };
    entityCfg.rotation = ecs::Vec3{ 0.0f, preset.yaw, 0.0f };
    entityCfg.scale = ecs::Vec3{ preset.sx, preset.sy, preset.sz };
    entityCfg.meshPath = "models/african_head.obj";
    entityCfg.materialPath = "materials/african_head.material.json";
    entityCfg.bounce = false;

    const ecs::Entity entity = SpawnEcsDemoEntity(entityCfg);

    m_EcsDebugEntities.push_back(entity);

    Logger::Get().Info(
        "ECS runtime: gameplay spawn -> total entities=" + std::to_string(m_EcsDebugEntities.size()));
    return entity;
}

bool Application::DestroyLastGameplayEntity()
{
    while (!m_EcsDebugEntities.empty())
    {
        const ecs::Entity entity = m_EcsDebugEntities.back();
        m_EcsDebugEntities.pop_back();

        if (!m_World.IsAlive(entity))
            continue;

        const bool destroyed = m_World.DestroyEntity(entity);
        Logger::Get().Info(
            std::string("ECS runtime: gameplay destroy last entity -> ") +
            (destroyed ? "ok" : "failed") +
            ", total entities=" + std::to_string(m_EcsDebugEntities.size()));
        return destroyed;
    }

    Logger::Get().Info("ECS runtime: gameplay destroy requested, but scene is already empty");
    return false;
}

void Application::ToggleDebugColliders()
{
    m_DebugCollidersEnabled = !m_DebugCollidersEnabled;
    if (m_RenderSystem != nullptr)
        m_RenderSystem->SetDebugCollidersEnabled(m_DebugCollidersEnabled);
    Logger::Get().Info(std::string("Application: debug colliders ") + (m_DebugCollidersEnabled ? "enabled" : "disabled"));
}

bool Application::IsInputActionActive(const std::string& action) const
{
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard)
        return false;

    return m_InputManager.IsActionActive(action);
}

void Application::SetEditorPlayMode(bool enabled)
{
    if (m_EditorPlayMode == enabled)
        return;

    m_EditorPlayMode = enabled;
    if (m_PhysicsSystem != nullptr)
        m_PhysicsSystem->SetEnabled(enabled);

    Logger::Get().Info(std::string("Application: editor mode -> ") + (enabled ? "Play" : "Edit"));
}

void Application::UpdateEcs(float dt)
{
    if (IsInputActionActive("ToggleDebugColliders")) ToggleDebugColliders();
    if (IsInputActionActive("ToggleLightDebug") && m_RenderSystem != nullptr) { m_RenderSystem->SetLightDebugEnabled(!m_RenderSystem->IsLightDebugEnabled()); Logger::Get().Info(std::string("Application: light debug ") + (m_RenderSystem->IsLightDebugEnabled()?"enabled":"disabled")); }
    if (IsInputActionActive("ToggleShadows") && m_RenderSystem != nullptr) { m_RenderSystem->SetShadowsEnabled(!m_RenderSystem->AreShadowsEnabled()); Logger::Get().Info(std::string("Application: shadows ") + (m_RenderSystem->AreShadowsEnabled()?"enabled":"disabled")); }

    if (m_EcsDebugEntities.empty())
        return;

    if (!m_Config.ecsDemo.logSnapshots)
        return;

    m_EcsDebugLogTimer += dt;
    if (m_EcsDebugLogTimer < 1.0f)
        return;

    m_EcsDebugLogTimer = 0.0f;

    std::ostringstream ss;
    ss << "ECS runtime: scene snapshot";
    for (std::size_t i = 0; i < m_EcsDebugEntities.size(); ++i)
    {
        const ecs::Entity entity = m_EcsDebugEntities[i];
        const auto* tag = m_World.GetComponent<ecs::TagComponent>(entity);
        const auto* transform = m_World.GetComponent<ecs::TransformComponent>(entity);
        const auto* velocity = m_World.GetComponent<ecs::VelocityComponent>(entity);
        const auto* meshRenderer = m_World.GetComponent<ecs::MeshRendererComponent>(entity);
        const auto* material = m_World.GetComponent<ecs::MaterialComponent>(entity);
        if (transform == nullptr || velocity == nullptr || meshRenderer == nullptr)
            continue;

        ss << " \n e" << i
           << " tag=" << (tag != nullptr ? tag->name : "<unnamed>")
           << " pos=(" << transform->position.x << ", " << transform->position.y << ", " << transform->position.z << ")"
           << " scale=(" << transform->scale.x << ", " << transform->scale.y << ", " << transform->scale.z << ")"
           << " rot=(" << transform->rotation.x << ", " << transform->rotation.y << ", " << transform->rotation.z << ")"
           << " vel=(" << velocity->linear.x << ", " << velocity->linear.y << ", " << velocity->linear.z << ")"
           << " mesh=" << meshRenderer->meshPath
           << " material=" << (material != nullptr ? material->materialPath : "<direct>");
    }
    Logger::Get().Info(ss.str());
}

bool Application::Initialize()
{
    Logger::Get().Initialize("engine.log");
    Logger::Get().Info("Application Initialize");

    if (!ValidateAssetDependencyAvailability())
        return false;

    m_ResourceManager = std::make_unique<ResourceManager>();
    RunResourceBootstrapCheck();

    RunEcsBootstrapCheck();

    m_Time.Initialize();

    m_ConfigWatchPath = ResolveConfigPath();
    Logger::Get().Info("Application: using config file: " + m_ConfigWatchPath.string());

    m_Config = AppConfig{};
    m_Config.ecsDemo.initialEntities = BuildDefaultEcsDemoEntities();

    const bool configLoaded = ConfigLoader::Load(m_ConfigWatchPath.string(), m_Config);
    if (!m_Config.ecsDemo.sceneFile.empty())
    {
        std::string sceneError;
        std::vector<EcsDemoEntityConfig> sceneEntities;
        const auto scenePath = AssetPaths::ResolveAssetPath(m_Config.ecsDemo.sceneFile);
        if (SceneSerializer::LoadEntityConfigs(scenePath, sceneEntities, &sceneError))
            m_Config.ecsDemo.initialEntities = std::move(sceneEntities);
        else if (!sceneError.empty())
            Logger::Get().Warn(sceneError);
    }

    WindowConfig selectedWindow = BuildDefaultWindowConfig(m_Config.activeBackend);
    bool foundWindowConfig = false;

    if (!configLoaded)
    {
        Logger::Get().Error("Application: failed to load config. Using default single-window setup.");
    }
    else
    {
        for (const auto& wcfg : m_Config.windows)
        {
            if (wcfg.backend == m_Config.activeBackend)
            {
                selectedWindow = wcfg;
                foundWindowConfig = true;
                break;
            }
        }

        if (!foundWindowConfig)
        {
            Logger::Get().Error(
                std::string("Application: no window config found for active renderer ") +
                BackendToString(m_Config.activeBackend) +
                ". Using default single-window setup.");
        }
    }

    WindowContext ctx;
    ctx.backend = selectedWindow.backend;
    ctx.baseTitle = selectedWindow.title + " | " + BackendToString(selectedWindow.backend);
    ctx.clear[0] = selectedWindow.clear[0];
    ctx.clear[1] = selectedWindow.clear[1];
    ctx.clear[2] = selectedWindow.clear[2];
    ctx.clear[3] = selectedWindow.clear[3];

    ctx.window = std::make_unique<GlfwWindow>();
    if (!ctx.window->Create(selectedWindow.width, selectedWindow.height, ctx.baseTitle))
        return false;

    ctx.renderer = RenderFactory::Create(ctx.backend);
    if (!ctx.renderer)
    {
        Logger::Get().Error(
            std::string("Application: renderer backend is unavailable: ") + BackendToString(ctx.backend));
        return false;
    }

    if (!ctx.renderer->Initialize(ctx.window.get()))
        return false;

    ctx.editorUiAvailable = ctx.renderer->InitializeEditorUi(ctx.window.get());
    if (!ctx.editorUiAvailable)
        Logger::Get().Warn("Application: editor UI is unavailable for this renderer");

    m_Windows.push_back(std::move(ctx));
    ConfigureInputBindings();
    m_EventBus.SubscribeCollision([this](const ecs::CollisionEvent& e){
        m_ActiveCollisionPairs.insert(BuildCollisionPairKey(e.a.index, e.b.index));
    });
    SetupEcsRuntimeDemo();
    InitializeConfigHotReload();

    m_IsRunning = true;

    RequestStateChange(std::make_unique<LoadingState>());
    m_StateMachine.ApplyPending(*this);

    return true;
}


int Application::Run()
{
    double accumulator = 0.0;
    const double fixedDt = 1.0 / 60.0;
    const int maxSteps = 5;

    while (m_IsRunning)
    {
        if (!m_Windows.empty())
            m_Windows[0].window->PollEvents();

        auto* primary = dynamic_cast<GlfwWindow*>(GetWindow());
        if (primary)
        {
            GLFWwindow* w = primary->GetGlfwHandle();
            static bool prevF5 = false;
            bool f5 = glfwGetKey(w, GLFW_KEY_F5) == GLFW_PRESS;

            if (f5 && !prevF5)
            {
                for (auto& wc : m_Windows)
                {
                    if (wc.backend == RenderBackend::Vulkan && wc.renderer)
                        wc.renderer->HotReloadShaders();
                }
            }
            prevF5 = f5;
        }

        bool anyAlive = false;
        for (auto& wc : m_Windows)
            anyAlive |= (wc.window && !wc.window->ShouldClose());

        if (!anyAlive) break;

        float dt = m_Time.Tick();
    
    auto spawnLight = [&](const char* name, ecs::LightType type, const ecs::Vec3& pos, const ecs::Vec3& rot, const ecs::Vec3& color, float intensity, float range)
    {
        const ecs::Entity e = m_World.CreateEntity();
        auto& t = m_World.AddComponent<ecs::TransformComponent>(e);
        t.position = pos; t.rotation = rot; t.scale = ecs::Vec3{1.0f,1.0f,1.0f};
        auto& tag = m_World.AddComponent<ecs::TagComponent>(e);
        tag.name = name;
        auto& l = m_World.AddComponent<ecs::LightComponent>(e);
        l.type = type; l.color = color; l.intensity = intensity; l.range = range;
    };
    spawnLight("Directional Light", ecs::LightType::Directional, ecs::Vec3{0.0f, 4.0f, 0.0f}, ecs::Vec3{-0.7f, 0.6f, 0.0f}, ecs::Vec3{1.0f,0.98f,0.9f}, 1.2f, 0.0f);
    spawnLight("Point Light", ecs::LightType::Point, ecs::Vec3{1.2f, 1.7f, 0.6f}, ecs::Vec3{}, ecs::Vec3{1.0f,0.45f,0.3f}, 4.0f, 5.5f);
    spawnLight("Spot Light", ecs::LightType::Spot, ecs::Vec3{-1.5f, 2.0f, -0.5f}, ecs::Vec3{-0.6f, -0.6f, 0.0f}, ecs::Vec3{0.4f,0.7f,1.0f}, 5.0f, 7.0f);

    if (m_ResourceManager != nullptr)
        {
            m_ResourceManager->PollAsyncLoads();
            m_ResourceManager->PollHotReload();
            m_World.ForEach<ecs::ColliderComponent, ecs::MeshRendererComponent, ecs::TransformComponent>(
                [&](ecs::Entity, ecs::ColliderComponent& collider, ecs::MeshRendererComponent& meshRenderer, ecs::TransformComponent& transform)
                {
                    if (!collider.autoFitFromMesh)
                        return;
                    ecs::Vec3 halfExtents{};
                    ecs::Vec3 offset{};
                    if (TryBuildMeshCollider(m_ResourceManager.get(), meshRenderer.meshPath, transform.scale, halfExtents, offset))
                    {
                        collider.halfExtents = halfExtents;
                        collider.offset = offset;
                        if (meshRenderer.meshPath.find("african_head") != std::string::npos)
                        {
                            collider.halfExtents.x *= 1.08f;
                            collider.halfExtents.y *= 1.10f;
                            collider.halfExtents.z *= 1.18f;
                            collider.offset.y += collider.halfExtents.y * 0.04f;
                        }
                        collider.autoFitFromMesh = false;
                    }
                });
        }
        PollConfigHotReload();
        UpdateCameraController(dt);

        if (m_UpdateMode == UpdateMode::Fixed)
        {
            accumulator += dt;

            int steps = 0;
            while (accumulator >= fixedDt && steps < maxSteps)
            {
                m_StateMachine.Update(*this, fixedDt);
                m_StateMachine.ApplyPending(*this);

                accumulator -= fixedDt;
                ++steps;
            }
        }
        else
        {
            m_StateMachine.Update(*this, dt);
            m_StateMachine.ApplyPending(*this);
        }

        static float fpsTimer = 0.0f;
        static int fpsFrames = 0;

        fpsTimer += dt;
        fpsFrames++;

        if (fpsTimer >= 1.0f)
        {
            float fps = fpsFrames / fpsTimer;

            for (auto& wc : m_Windows)
            {
                if (!wc.window || wc.window->ShouldClose()) continue;

                char title[256];
                snprintf(title, sizeof(title),
                    "%s | FPS: %.1f | dt: %.3f ms",
                    wc.baseTitle.c_str(),
                    fps,
                    dt * 1000.0f);

                wc.window->SetTitle(title);
            }

            std::ostringstream ss;
            ss << "FPS=" << fps << " dt(ms)=" << dt * 1000.0f;
            Logger::Get().Info(ss.str());

            fpsTimer = 0.0f;
            fpsFrames = 0;
        }

        for (auto& wc : m_Windows)
        {
            if (!wc.window || wc.window->ShouldClose()) continue;

            wc.renderer->BeginFrame();
            if (wc.editorUiAvailable)
            {
                wc.renderer->BeginEditorUiFrame();
                m_EditorLayer.Render(*this, wc.renderer.get(), dt);
            }

            const int viewportWidth = m_EditorLayer.GetViewportPixelWidth();
            const int viewportHeight = m_EditorLayer.GetViewportPixelHeight();
            const bool renderSceneToViewport =
                wc.editorUiAvailable &&
                viewportWidth > 1 &&
                viewportHeight > 1 &&
                wc.renderer->BeginViewportRender(viewportWidth, viewportHeight, wc.clear);

            if (renderSceneToViewport)
            {
                m_ActiveCollisionPairs.clear();
                if (m_RenderSystem != nullptr)
                {
                    m_RenderSystem->SetRenderAdapter(wc.renderer.get());
                    UpdateRenderSystemCameraAspect(static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight));
                }

                m_World.UpdateSystems(dt);
                UpdateEcs(dt);
                m_StateMachine.Render(*this, *wc.renderer);
                wc.renderer->EndViewportRender();
            }

            wc.renderer->Clear(wc.clear[0], wc.clear[1], wc.clear[2], wc.clear[3]);

            if (!renderSceneToViewport)
            {
                m_ActiveCollisionPairs.clear();
                if (m_RenderSystem != nullptr)
                {
                    m_RenderSystem->SetRenderAdapter(wc.renderer.get());
                    UpdateRenderSystemCamera(wc.window.get());
                }

                m_World.UpdateSystems(dt);
                UpdateEcs(dt);
                m_StateMachine.Render(*this, *wc.renderer);
            }

            if (wc.editorUiAvailable)
                wc.renderer->RenderEditorUiFrame();

            wc.renderer->EndFrame();
            wc.renderer->Present();

            if (m_RenderSystem != nullptr)
                m_RenderSystem->SetRenderAdapter(nullptr);
        }
    }
    return 0;
}

void Application::Shutdown()
{
    if (auto* primary = dynamic_cast<GlfwWindow*>(GetWindow()))
    {
        if (GLFWwindow* window = primary->GetGlfwHandle(); window != nullptr)
        {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            if (glfwRawMouseMotionSupported())
                glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        }
    }

    m_Camera.controlsActive = false;
    m_Camera.previousRightMouseDown = false;

    if (m_RenderSystem != nullptr)
        m_RenderSystem->ReleaseGpuResources();

    for (auto& wc : m_Windows)
    {
        if (wc.renderer)
        {
            wc.renderer->ShutdownEditorUi();
            wc.renderer->Shutdown();
        }
    }

    m_Windows.clear();
    m_ResourceManager.reset();
    m_World.ClearSystems();
    m_PhysicsSystem = nullptr;
    m_RenderSystem = nullptr;
    m_EcsDebugEntities.clear();
    m_EcsDebugLogTimer = 0.0f;
    m_World.Clear();
    Logger::Get().Shutdown();
}


void Application::RequestStateChange(std::unique_ptr<IGameState> s)
{
    m_StateMachine.ChangeState(std::move(s));
}
