#include "../ecs/ComponentRegistry.h"
#include "Application.h"
#include "../editor/SunSynchronization.h"
#include "../ecs/components/LightComponent.h"
#include "AssetDependencyValidation.h"
#include "AssetPaths.h"
#include "Logger.h"
#include "ConfigLoader.h"

#include "../ecs/components/BoundsBounceComponent.h"
#include "../ecs/components/ColliderComponent.h"
#include "../ecs/components/RigidbodyComponent.h"
#include "../ecs/components/MaterialComponent.h"
#include "../ecs/components/MeshRendererComponent.h"
#include "../ecs/components/TagComponent.h"
#include "../ecs/components/TransformComponent.h"
#include "../ecs/components/VelocityComponent.h"
#include "../ecs/components/PlayerStartComponent.h"
#include "../ecs/PlayerSpawn.h"
#include "../ecs/components/PlayerComponent.h"
#include "../ecs/components/CharacterMovementComponent.h"
#include "../ecs/systems/BoundsBounceSystem.h"
#include "../ecs/systems/PhysicsSystem.h"

#include "../ecs/PhysicsStressScene.h"
#include "PhysicsDiagnosticSample.h"
#include "RenderDiagnosticSample.h"
#include <chrono>
#include <thread>
#include "../platform/GlfwWindow.h"
#include "../render/IRenderAdapter.h"
#include "../resources/ResourceManager.h"
#include "../scene/SceneSerializer.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include "../jobs/JobSystem.h"
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <tracy/Tracy.hpp>

#include "../game/states/LoadingState.h"

#include <algorithm>
#include <cfloat>
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

constexpr float kMaxCameraPitch = ecs::PlayerMaximumPitch;
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

        for(const auto& slot:entity.slotMaterials) {const auto key=AssetPaths::NormalizeAssetKey(slot);if(!key.empty()&&materialKeys.insert(key).second)(void)m_ResourceManager->LoadAsync<MaterialResource>(key);}
        if(entity.materialOverrides.texturePath&&!entity.materialOverrides.texturePath->empty())(void)m_ResourceManager->LoadAsync<TextureResource>(*entity.materialOverrides.texturePath);
        if(entity.materialOverrides.shaderPath&&!entity.materialOverrides.shaderPath->empty())(void)m_ResourceManager->LoadAsync<ShaderResource>(*entity.materialOverrides.shaderPath);
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

    const ecs::Entity first = GetWorld().CreateEntity();
    const ecs::Entity second = GetWorld().CreateEntity();
    const ecs::Entity third = GetWorld().CreateEntity();

    Logger::Get().Info("ECS bootstrap: created " + GetWorld().DebugDescribeEntity(first));
    Logger::Get().Info("ECS bootstrap: created " + GetWorld().DebugDescribeEntity(second));
    Logger::Get().Info("ECS bootstrap: created " + GetWorld().DebugDescribeEntity(third));

    const bool destroyed = GetWorld().DestroyEntity(second);
    Logger::Get().Info(std::string("ECS bootstrap: destroy second entity -> ") + (destroyed ? "ok" : "failed"));
    Logger::Get().Info("ECS bootstrap: second entity after destroy -> " + GetWorld().DebugDescribeEntity(second));

    const ecs::Entity recycled = GetWorld().CreateEntity();
    Logger::Get().Info("ECS bootstrap: recycled slot into " + GetWorld().DebugDescribeEntity(recycled));

    auto& firstTransform = GetWorld().AddComponent<ecs::TransformComponent>(first);
    firstTransform.position = ecs::Vec3{ 1.5f, -0.5f, 0.0f };
    firstTransform.scale = ecs::Vec3{ 2.0f, 2.0f, 1.0f };

    Logger::Get().Info(std::string("ECS bootstrap: first has transform -> ") +
        (GetWorld().HasComponent<ecs::TransformComponent>(first) ? "true" : "false"));

    if (const auto* transform = GetWorld().GetComponent<ecs::TransformComponent>(first))
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

    auto& firstTag = GetWorld().AddComponent<ecs::TagComponent>(first);
    firstTag.name = "BootstrapEntity";
    Logger::Get().Info(std::string("ECS bootstrap: first tag -> ") + firstTag.name);

    auto& firstMeshRenderer = GetWorld().AddComponent<ecs::MeshRendererComponent>(first);
    firstMeshRenderer.meshPath = "models/validation_triangle.obj";
    auto& firstMaterial = GetWorld().AddComponent<ecs::MaterialComponent>(first);
    firstMaterial.materialPath = "materials/validation_checker.material.json";
    Logger::Get().Info(
        "ECS bootstrap: first mesh renderer asset refs -> mesh=" + firstMeshRenderer.meshPath +
        " material=" + firstMaterial.materialPath);

    auto& recycledTransform = GetWorld().AddComponent<ecs::TransformComponent>(recycled);
    recycledTransform.position = ecs::Vec3{ -2.0f, 3.0f, 0.0f };
    recycledTransform.rotation = ecs::Vec3{ 0.0f, 0.0f, 0.5f };
    recycledTransform.scale = ecs::Vec3{ 0.75f, 0.75f, 1.0f };
    Logger::Get().Info(std::string("ECS bootstrap: recycled has transform before remove -> ") +
        (GetWorld().HasComponent<ecs::TransformComponent>(recycled) ? "true" : "false"));

    const bool removedTransform = GetWorld().RemoveComponent<ecs::TransformComponent>(recycled);
    Logger::Get().Info(std::string("ECS bootstrap: remove transform from recycled -> ") +
        (removedTransform ? "ok" : "failed"));
    Logger::Get().Info(std::string("ECS bootstrap: recycled has transform after remove -> ") +
        (GetWorld().HasComponent<ecs::TransformComponent>(recycled) ? "true" : "false"));

    auto& thirdTransform = GetWorld().AddComponent<ecs::TransformComponent>(third);
    thirdTransform.position = ecs::Vec3{ 4.0f, 2.0f, 0.0f };
    thirdTransform.rotation = ecs::Vec3{};
    thirdTransform.scale = ecs::Vec3{ 1.25f, 1.25f, 1.0f };
    const bool destroyedThird = GetWorld().DestroyEntity(third);
    Logger::Get().Info(std::string("ECS bootstrap: destroy third entity with transform -> ") +
        (destroyedThird ? "ok" : "failed"));
    Logger::Get().Info(std::string("ECS bootstrap: third has transform after destroy -> ") +
        (GetWorld().HasComponent<ecs::TransformComponent>(third) ? "true" : "false"));
    Logger::Get().Info(std::string("ECS bootstrap: first has tag -> ") +
        (GetWorld().HasComponent<ecs::TagComponent>(first) ? "true" : "false"));
    Logger::Get().Info(std::string("ECS bootstrap: first has mesh renderer -> ") +
        (GetWorld().HasComponent<ecs::MeshRendererComponent>(first) ? "true" : "false"));

    Logger::Get().Info("ECS bootstrap: alive entities=" + std::to_string(GetWorld().GetAliveCount()) +
        ", capacity=" + std::to_string(GetWorld().GetCapacity()));

    GetWorld().Clear();
    Logger::Get().Info("ECS bootstrap: world reset after self-check");
}

void Application::SetupEcsRuntimeDemo(bool populate)
{
    ClearPhysicsStressScene();
    GetWorld().Clear();
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
    m_PhysicsSystem->SetJobSystem(
        m_JobSystem.get());
    m_PhysicsSystem->SetEnabled(m_EditorPlayMode);
    m_RenderSystem = &m_World.AddSystem<ecs::RenderSystem>();
    m_RenderSystem->SetResourceManager(m_ResourceManager.get());
    m_RenderSystem->SetInterpolation(&m_RenderInterpolation);
    m_RenderSystem->SetPhysicsDebugPose(m_PhysicsDebugPose);
    m_RenderSystem->SetInstancingEnabled(m_GpuInstancing);
    m_RenderSystem->SetShadingModelOverride(m_ShadingModelOverride);m_RenderSystem->SetTextureMapMask(m_PbrTextureMask);
    m_RenderSystem->SetLightingSettings(&m_LightingSettings);m_RenderSystem->SetLightRenderingMode(m_LightMode);
    m_RenderSystem->SetEnvironmentSettings(&m_EnvironmentSettings);
    m_RenderSystem->SetShadowSettings(&m_ShadowSettings);
    m_RenderSystem->SetDebugCollidersEnabled(m_DebugCollidersEnabled);

    m_EcsDebugEntities.clear();
    if(!populate)return;
    const std::vector<EcsDemoEntityConfig> entities =
        m_Config.ecsDemo.initialEntities.empty() ? BuildDefaultEcsDemoEntities() : m_Config.ecsDemo.initialEntities;
    PreloadSceneResourcesAsync(entities);

    for (const auto& entityCfg : entities)
    {
        m_EcsDebugEntities.push_back(SpawnEcsDemoEntity(entityCfg));
    }

    constexpr float cubeSize = 0.18f;
    constexpr float cubeHalf = cubeSize * 0.5f;

    constexpr float groundCenterY = -1.0f;
    constexpr float groundHalfHeight = 0.025f;
    constexpr float groundTop =
        groundCenterY + groundHalfHeight;

    for (int layer = 0; layer < 4; ++layer)
    {
        const int count = 4 - layer;

        const float startX =
            -0.5f *
            static_cast<float>(count - 1) *
            cubeSize;

        for (int i = 0; i < count; ++i)
        {
            EcsDemoEntityConfig cubeCfg;

            cubeCfg.tag =
                "PyramidCube_" +
                std::to_string(layer) +
                "_" +
                std::to_string(i);

            cubeCfg.meshPath =
                "models/validation_cube.obj";

            cubeCfg.materialPath =
                "materials/blue.material.json";

            cubeCfg.scale =
                ecs::Vec3{
                    cubeSize,
                    cubeSize,
                    cubeSize
            };

            cubeCfg.position =
                ecs::Vec3{
                    startX +
                        static_cast<float>(i) *
                        cubeSize,

                    groundTop +
                        cubeHalf +
                        static_cast<float>(layer) *
                        cubeSize,

                    0.45f
            };

            cubeCfg.colliderManual = true;

            cubeCfg.colliderHalfExtents =
                ecs::Vec3{
                    cubeHalf,
                    cubeHalf,
                    cubeHalf
            };

            m_EcsDebugEntities.push_back(
                SpawnEcsDemoEntity(cubeCfg));
        }
    }

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
    if (!SceneSerializer::SaveWorld(snapshotPath, GetWorld(), &saveError) && !saveError.empty())
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
    if (!m_ProjectBrowserActive && !m_Config.ecsDemo.sceneFile.empty())
    {
        m_SceneWatchPath = AssetPaths::ResolveAssetPath(m_Config.ecsDemo.sceneFile);
        if (!m_SceneWatchPath.empty() && std::filesystem::exists(m_SceneWatchPath, ec))
        {
            m_SceneWriteTime = std::filesystem::last_write_time(m_SceneWatchPath, ec);
            m_HasSceneWatch = !static_cast<bool>(ec);
        }
    }
}

bool Application::ReloadSceneFromCurrentConfig(const char* reason){
    if(m_Config.ecsDemo.sceneFile.empty()){SetupEcsRuntimeDemo();return true;}
    std::string error;if(!OpenScene(m_Config.ecsDemo.sceneFile,&error)){Logger::Get().Error(error);return false;}
    Logger::Get().Info(std::string("Scene reload: ")+reason);return true;
}

bool Application::SaveCurrentScene(std::string* error){
    return SaveSceneAs(m_SceneKey.empty()?DefaultSceneKey():m_SceneKey,!m_SceneKey.empty(),error);
}
bool Application::LoadCurrentScene(std::string* error){
    auto key=m_SceneKey.empty()?m_Config.ecsDemo.sceneFile:m_SceneKey;
    if(key.empty()){if(error)*error="No scene selected; use Open Scene";return false;}return OpenScene(key,error);
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
    m_InputManager.BindAction("FireProjectile", GLFW_KEY_R);
    m_InputManager.BindAction("LuaAttack", GLFW_KEY_F);
    m_InputManager.BindAction("ToggleDebugColliders", GLFW_KEY_F3);
}

void Application::PollConfigHotReload()
{
    if(m_ProjectBrowserActive||ProjectManager::Active())return;
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
    const ecs::Entity entity = GetWorld().CreateEntity();
    GetWorld().AddComponent<ecs::SceneIdentityComponent>(entity);
    const std::size_t entityOrdinal = m_EcsDebugEntities.size();
    if(entityCfg.light){
        auto& t=GetWorld().AddComponent<ecs::TransformComponent>(entity);
        t.position=entityCfg.position;t.rotation=entityCfg.rotation;t.scale=entityCfg.scale;
        GetWorld().AddComponent<ecs::TagComponent>(entity).name=entityCfg.tag;
        ecs::AddSceneLight(GetWorld(),entity,*entityCfg.light);return entity;
    }

    auto& transform = GetWorld().AddComponent<ecs::TransformComponent>(entity);
    transform.position = entityCfg.position;
    transform.rotation = entityCfg.rotation;
    transform.scale = entityCfg.scale;

    auto& velocity = GetWorld().AddComponent<ecs::VelocityComponent>(entity);
    velocity.linear = entityCfg.linearVelocity;
    velocity.angular = entityCfg.angularVelocity;

    auto& tag = GetWorld().AddComponent<ecs::TagComponent>(entity);
    tag.name = entityCfg.tag.empty() ? ("DemoEntity_" + std::to_string(entityOrdinal)) : entityCfg.tag;

    auto& meshRenderer = GetWorld().AddComponent<ecs::MeshRendererComponent>(entity);
    meshRenderer.meshPath = entityCfg.meshPath;
    meshRenderer.visible = entityCfg.visible;

    if (!entityCfg.slotMaterials.empty() || entityCfg.materialOverrides.shaderPath || entityCfg.materialOverrides.texturePath ||
        entityCfg.materialOverrides.doubleSided || entityCfg.materialOverrides.castShadows || entityCfg.materialOverrides.receiveShadows ||
        !entityCfg.materialPath.empty() ||
        !entityCfg.texturePath.empty() ||
        !entityCfg.shaderPath.empty() ||
        HasNonDefaultTint(entityCfg.materialTint))
    {
        auto& material = GetWorld().AddComponent<ecs::MaterialComponent>(entity);
        material.materialPath = entityCfg.materialPath;
        material.slotMaterials=entityCfg.slotMaterials;material.overrides=entityCfg.materialOverrides;
        material.texturePath = entityCfg.texturePath;
        material.shaderPath = entityCfg.shaderPath;
        for (std::size_t i = 0; i < entityCfg.materialTint.size(); ++i)
            material.tint[i] = entityCfg.materialTint[i];
    }

    if (entityCfg.bounce)
        GetWorld().AddComponent<ecs::BoundsBounceComponent>(entity);

    auto& rigidbody = GetWorld().AddComponent<ecs::RigidbodyComponent>(entity);
    rigidbody.useGravity = entityCfg.useGravity;
    rigidbody.mass = 1.0f;
    rigidbody.isStatic = entityCfg.isStatic || tag.name == "GroundPlane";
    rigidbody.simulatePhysics = entityCfg.simulatePhysics;
    rigidbody.velocity = entityCfg.linearVelocity;
    rigidbody.angularVelocity = entityCfg.angularVelocity;
    auto& collider = GetWorld().AddComponent<ecs::ColliderComponent>(entity);
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
    ss << "ECS runtime: spawned demo entity -> " << GetWorld().DebugDescribeEntity(entity)
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

    if (auto* rb = GetWorld().GetComponent<ecs::RigidbodyComponent>(projectile))
    {
        rb->velocity = projectileCfg.linearVelocity;
        rb->mass = 2.0f;
    }
    if (auto* collider = GetWorld().GetComponent<ecs::ColliderComponent>(projectile))
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
    if(m_ProjectBrowserActive)return;
    auto* primary = dynamic_cast<GlfwWindow*>(GetWindow());
    if (primary == nullptr)
        return;

    GLFWwindow* window = primary->GetGlfwHandle();
    if (window == nullptr)
        return;

    // A Lua camera lease excludes editor navigation, independent of Player presence.
    if(m_EditorPlayMode&&m_LuaRuntime.HasCameraOwner()){
        if(m_Camera.controlsActive){
            if(!m_PlayerMouseCaptured){glfwSetInputMode(window,GLFW_CURSOR,GLFW_CURSOR_NORMAL);if(glfwRawMouseMotionSupported())glfwSetInputMode(window,GLFW_RAW_MOUSE_MOTION,GLFW_FALSE);}
            m_Camera.controlsActive=false;m_Camera.previousRightMouseDown=false;
        }
        return;
    }

    const double scrollDeltaY = primary->ConsumeScrollDeltaY();
    const bool focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window, &cursorX, &cursorY);

    const bool editorWantsMouse =
        ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
    const bool mouseOverViewport = m_EditorLayer.IsPointInsideViewport(cursorX, cursorY);
    const auto inputOwner=m_EditorLayer.InputOwnership(glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_RIGHT)==GLFW_PRESS,glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS,m_Camera.controlsActive,IsEditorPlayMode());
    const bool editorBlocksMouseLook = (!m_Camera.controlsActive&&inputOwner!=editor::InputOwner::Camera) || m_EditorLayer.BlocksCamera() || (!m_Camera.controlsActive&&glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS) ||
        (!m_Camera.controlsActive&&(!mouseOverViewport||!m_EditorLayer.IsViewportHovered())) ||
        (editorWantsMouse&&!mouseOverViewport&&!m_Camera.controlsActive);
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

        if (!GetWorld().IsAlive(entity))
            continue;

        const bool destroyed = GetWorld().DestroyEntity(entity);
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
    if(IsEditorPlayMode()&&(action=="SpawnEntity"||action=="DestroyEntity"||action=="PauseToMenu"))return false;
    if(action=="FireProjectile"&&(!IsSimulationRunning()||!m_EditorLayer.ProjectileTestEnabled()))return false;
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard)
        return false;

    if(m_EditorLayer.BlocksCamera()||m_Camera.controlsActive||(IsEditorPlayMode()&&!m_EditorLayer.OwnsViewportKeyboard()))return false;
    return m_InputManager.IsActionActive(action);
}

void Application::SetEditorPlayMode(bool enabled)
{
    if(enabled&&m_ProjectBrowserActive)return;
    if(m_EditorPlayMode==enabled)return;
    // A Play/Stop transition must release editor free-fly capture as well.
    // Keep previousRightMouseDown until release, so a held RMB cannot recapture
    // and move the restored editor pose in the Stop frame.
    if(m_Camera.controlsActive){
        if(auto* primary=dynamic_cast<GlfwWindow*>(GetWindow()))if(auto* window=primary->GetGlfwHandle()){
            glfwSetInputMode(window,GLFW_CURSOR,GLFW_CURSOR_NORMAL);
            if(glfwRawMouseMotionSupported())glfwSetInputMode(window,GLFW_RAW_MOUSE_MOTION,GLFW_FALSE);
            glfwSetCursorPos(window,m_Camera.cursorXBeforeCapture,m_Camera.cursorYBeforeCapture);
        }
        m_Camera.controlsActive=false;
    }
    if(enabled){
        m_EditorLayer.FinishTransaction(*this);
        m_EditInputBindings=m_InputManager.CaptureBindings();
        m_SavedEditorCamera.position = m_Camera.position;
        m_SavedEditorCamera.yaw = m_Camera.yaw;
        m_SavedEditorCamera.pitch = m_Camera.pitch;
        const auto start=std::chrono::steady_clock::now();
        auto runtime=m_World.CloneData(&m_CloneMapping);
        m_EditDebugEntities=m_EcsDebugEntities;m_EditStressEntities=m_StressEntities;
        m_EditLighting=m_LightingSettings;m_EditEnvironment=m_EnvironmentSettings;m_EditShadows=m_ShadowSettings;m_EditBackground=m_Background;m_EditAntiAliasing=m_AntiAliasing;
        auto map=[&](std::vector<ecs::Entity>& entities){std::erase_if(entities,[&](auto e){return !m_CloneMapping.contains(e);});for(auto& e:entities)e=m_CloneMapping.at(e);};
        map(m_EcsDebugEntities);map(m_StressEntities);
        m_EditorLayer.EnterRuntime(m_CloneMapping);
        m_RuntimeWorld=std::move(runtime);
        ecs::TransformComponent spawnPoint;
        m_PlayerEntity=ecs::SpawnPlayer(*m_RuntimeWorld,&spawnPoint);
        if(m_RuntimeWorld->IsAlive(m_PlayerEntity)){
            const auto& t=*m_RuntimeWorld->GetComponent<ecs::TransformComponent>(m_PlayerEntity);
            const auto& player=*m_RuntimeWorld->GetComponent<ecs::PlayerComponent>(m_PlayerEntity);
            m_Camera.position=ecs::PlayerEyePosition(t.position,player.eyeHeight,m_RuntimeWorld->GetComponent<ecs::ColliderComponent>(m_PlayerEntity)->halfExtents.y);
            m_Camera.yaw=t.rotation.y;m_Camera.pitch=std::clamp(spawnPoint.rotation.x,-kMaxCameraPitch,kMaxCameraPitch);
        }
        m_GameplayCamera={};m_GameplayCamera.position=m_Camera.position;m_GameplayCamera.yaw=m_Camera.yaw;m_GameplayCamera.pitch=m_Camera.pitch;
        m_LuaRuntime.Start(*m_RuntimeWorld,[this](const std::string& action){return GameplayInputAllowed()&&m_InputManager.FrameDown(action);},GameplayCapabilities());
        m_StateMachine.Clear(*this);RequestStateChange(std::make_unique<LoadingState>());m_StateMachine.ApplyPending(*this);
        m_LastCloneMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }else{
        ReleasePlayerMouse();
        if(m_PhysicsSystem)m_PhysicsSystem->ResetState();
        m_StateMachine.Clear(*this);
        m_LuaRuntime.Stop();m_InputManager.RestoreBindings(std::move(m_EditInputBindings));m_Mutations.Clear();m_RenderInterpolation.Clear();m_EditorLayer.LeaveRuntime();m_RuntimeWorld.reset();m_CloneMapping.clear();
        m_Camera.position = m_SavedEditorCamera.position;
        m_Camera.yaw = m_SavedEditorCamera.yaw;
        m_Camera.pitch = m_SavedEditorCamera.pitch;
        m_EcsDebugEntities=std::move(m_EditDebugEntities);m_StressEntities=std::move(m_EditStressEntities);
        m_LightingSettings=m_EditLighting;m_EnvironmentSettings=m_EditEnvironment;m_ShadowSettings=m_EditShadows;m_Background=m_EditBackground;m_AntiAliasing=m_EditAntiAliasing;
    }
    m_EditorPlayMode=enabled;m_EditorPaused=false;ResetPhysicsClock();m_ActiveCollisionPairs.clear();
    if(m_PhysicsSystem){m_PhysicsSystem->ResetState();m_PhysicsSystem->SetEnabled(enabled);}
    m_EditorLayer.PruneSelection(*this);
    Logger::Get().Info(enabled?"Editor: Play isolated Runtime World":"Editor: Stop discarded Runtime World");
}
void Application::SetEditorPaused(bool paused){
    if(!m_EditorPlayMode||m_EditorPaused==paused)return;m_EditorPaused=paused;if(paused)m_LuaRuntime.SuspendInput();
    m_FixedClock.Reset();m_ResetPhysicsDelta=true;
    if(m_PhysicsSystem)m_PhysicsSystem->SetEnabled(!paused);
}

void Application::UpdateEcs(float dt)
{
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
        const auto* tag = GetWorld().GetComponent<ecs::TagComponent>(entity);
        const auto* transform = GetWorld().GetComponent<ecs::TransformComponent>(entity);
        const auto* velocity = GetWorld().GetComponent<ecs::VelocityComponent>(entity);
        const auto* meshRenderer = GetWorld().GetComponent<ecs::MeshRendererComponent>(entity);
        const auto* material = GetWorld().GetComponent<ecs::MaterialComponent>(entity);
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

bool Application::Initialize(StartupMode mode)
{
    Logger::Get().Initialize("engine.log");
    Logger::Get().Info("Application Initialize");

    if (!ValidateAssetDependencyAvailability())
        return false;

    m_JobSystem = std::make_unique<JobSystem>();

    if (!m_JobSystem->Initialize())
    {
        Logger::Get().Error("Application: failed to initialize JobSystem");
        return false;
    }

    m_ResourceManager =
        std::make_unique<ResourceManager>(m_JobSystem.get());
    m_ProjectBrowserActive=mode==StartupMode::ProjectBrowser;
    if(!m_ProjectBrowserActive)RunResourceBootstrapCheck();

    RunEcsBootstrapCheck();

    m_Time.Initialize();

    m_ConfigWatchPath = ResolveConfigPath();
    Logger::Get().Info("Application: using config file: " + m_ConfigWatchPath.string());

    m_Config = AppConfig{};
    m_Config.ecsDemo.initialEntities = BuildDefaultEcsDemoEntities();

    const bool configLoaded = ConfigLoader::Load(m_ConfigWatchPath.string(), m_Config);
    if (!m_ProjectBrowserActive && !m_Config.ecsDemo.sceneFile.empty())
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
        if(e.kind!=ecs::CollisionKind::Contact){m_LuaRuntime.QueueCollision(e);return;}
        m_LuaRuntime.QueueCollision(e);
        m_ActiveCollisionPairs.insert(BuildCollisionPairKey(e.a.index, e.b.index));

        if(m_RuntimeWorld){ecs::GroundCharacter(*m_RuntimeWorld,e.a,e);ecs::GroundCharacter(*m_RuntimeWorld,e.b,e);}
    });
    m_LegacyStartupScene=m_Config.ecsDemo.sceneFile;
    SetupEcsRuntimeDemo(!m_ProjectBrowserActive);
    if(!m_ProjectBrowserActive&&!m_Config.ecsDemo.sceneFile.empty()){std::string error;if(!OpenScene(m_Config.ecsDemo.sceneFile,&error))Logger::Get().Error(error);}
    m_EditorLayer.InitializePreferences(*this);
    if(m_ProjectBrowserActive)m_EditorLayer.ShowProjectBrowser();
    // SetupRenderStressScene();
    // Stress scene is created explicitly from the editor panel.
    InitializeConfigHotReload();

    m_IsRunning = true;

    if(!m_ProjectBrowserActive){
        RequestStateChange(std::make_unique<LoadingState>());
        m_StateMachine.ApplyPending(*this);
    }

    return true;
}


int Application::Run()
{
    ResetPhysicsClock();

    if (m_WaitForTracy) {
#if defined(TRACY_ENABLE)
        Logger::Get().Info("Diagnostics: waiting for Tracy connection (30 seconds)");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!TracyIsConnected && std::chrono::steady_clock::now() < deadline) {
            if (GetWindow()) GetWindow()->PollEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!TracyIsConnected) { Logger::Get().Error("Diagnostics: Tracy connection timed out"); return 2; }
        m_Time.Initialize();
#else
        Logger::Get().Error("Diagnostics: Tracy is disabled"); return 2;
#endif
    }
    std::vector<RenderDiagnosticSample> renderSamples;
    if(m_RenderBenchmark)renderSamples.reserve(m_ShadowBenchmark?1800:(m_LightingBenchmark?2400:1200));
    std::vector<PhysicsDiagnosticSample> samples;
    if (!m_DiagnosticOutput.empty()) samples.reserve(m_FrameLimit ? std::min(m_FrameLimit, 50000u) : 1000);
    const char* diagnosticPhase = "Visual";
    const auto lifecycleShadowSettings=m_ShadowSettings;
    const auto lifecycleEnvironmentSettings=m_EnvironmentSettings;
    std::uint32_t frameCount = 0;
    while (m_IsRunning)
    {
        if (m_FrameLimit != 0 && frameCount >= m_FrameLimit) break;
        const auto frameStart = std::chrono::steady_clock::now();
        const auto currentFrame = frameCount++;
        ZoneScopedN("Frame");
        if(m_RenderBenchmark) {
            if(m_IblBenchmark&&!m_IblBenchmarkStart&&!m_Windows.empty()&&m_Windows.front().renderer->GetEnvironmentDiagnostics().gpuBytes){m_IblBenchmarkStart=currentFrame+100;m_FrameLimit=m_IblBenchmarkStart+2400;}
            const auto warmup=m_IblBenchmark?m_IblBenchmarkStart:100;
            const auto block=currentFrame>=warmup?(currentFrame-warmup)/300:0;
            if(!m_LightBenchmark&&!m_PbrTexturesBenchmark&&!m_IblBenchmark)SetGpuInstancingEnabled(currentFrame>=100 && block%2==1);
            if(m_IblBenchmark){unsigned mask=(!m_IblBenchmarkStart||currentFrame<m_IblBenchmarkStart)?3:block%4;m_EnvironmentSettings.enabled=mask!=0;m_EnvironmentSettings.diffuse=(mask&1)!=0;m_EnvironmentSettings.specular=(mask&2)!=0;}
            if(m_LightBenchmark)SetLightRenderingMode(block%2?LightRenderingMode::Tiled:LightRenderingMode::Forward);
            if(m_LightingBenchmark)m_LightingSettings.enabled=block%4>=2;
            if(m_PbrTexturesBenchmark){const unsigned masks[]={0,1,6,7};SetPbrTextureMask(masks[block%4]);SetShadingModelOverride(1);m_LightingSettings.enabled=true;m_ShadowSettings.enabled=true;m_ShadowSettings.pcf=true;}
            if(m_PbrBenchmark){SetGpuInstancingEnabled(true);SetShadingModelOverride(block%2);m_LightingSettings.enabled=true;m_ShadowSettings.enabled=true;m_ShadowSettings.pcf=true;}
            if(m_ShadowBenchmark){m_LightingSettings.enabled=true;m_ShadowSettings.enabled=block%6>=2;m_ShadowSettings.pcf=block%6>=4;}
        }
        if (m_DiagnosticLifecycle) {
            switch (currentFrame) {
            case 0: SetupPhysicsStressScene(500); SetEditorPlayMode(true); diagnosticPhase = "InitialDrop"; break;
            case 120: SetEditorPlayMode(false); diagnosticPhase = "Stopped"; break;
            case 132: SetEditorPlayMode(true); diagnosticPhase = "Resumed"; break;
            case 240: m_PhysicsSystem->SetParallel(false); diagnosticPhase = "Serial"; break;
            case 360: m_PhysicsSystem->SetParallel(true); diagnosticPhase = "Parallel"; break;
            case 480: SetupPhysicsStressScene(500); diagnosticPhase = "Restart"; break;
            case 600: SetEditorPlayMode(false); diagnosticPhase = "StoppedAfterRestart"; break;
            case 612: ClearPhysicsStressScene(); diagnosticPhase = "Cleared"; break;
            case 624: SetupPhysicsStressScene(500); SetEditorPlayMode(true); diagnosticPhase = "Recreated"; break;
            case 744:
                if (auto* window = dynamic_cast<GlfwWindow*>(GetWindow()))
                    glfwSetWindowShouldClose(window->GetGlfwHandle(), GLFW_TRUE);
                break;
            }
            if(m_EnvironmentSettings!=lifecycleEnvironmentSettings){Logger::Get().Error("Diagnostics: lifecycle changed environment settings");return 3;}
            if(m_ShadowSettings!=lifecycleShadowSettings) {
                Logger::Get().Error("Diagnostics: lifecycle changed shadow settings");return 3;
            }
            // These checks use the same public operations as the ImGui panel.
            if (currentFrame == 612 && (GetStressEntityCount() != 0 || GetWorld().GetAliveCount() != 0)) {
                Logger::Get().Error("Diagnostics: Clear left scene entities"); return 3;
            }
            if ((currentFrame == 0 || currentFrame == 480 || currentFrame == 624) && GetStressEntityCount() != 501) {
                Logger::Get().Error("Diagnostics: Create/Restart count mismatch"); return 3;
            }
        }

        if (!m_Windows.empty())
            m_Windows[0].window->PollEvents();

        auto* primary = dynamic_cast<GlfwWindow*>(GetWindow());
        if (primary&&!m_ProjectBrowserActive)
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

            static bool prevF8 = false;

            const bool f8 =
                glfwGetKey(w, GLFW_KEY_F8) == GLFW_PRESS;

            if (f8 && !prevF8)
            {
                RunAsyncResourceStressTest();
            }

            prevF8 = f8;

            static bool prevF7 = false;

            const bool f7 =
                glfwGetKey(w, GLFW_KEY_F7) == GLFW_PRESS;

            if (f7 && !prevF7)
            {
                RunSyncResourceStressTest();
            }

            prevF7 = f7;



        }

        if(GetProject()&&m_EditorLayer.HasProjectEdits()&&!m_EditorLayer.ProjectExitConfirmed()){
            if(auto* native=dynamic_cast<GlfwWindow*>(GetWindow()))if(native->ShouldClose()){
                glfwSetWindowShouldClose(native->GetGlfwHandle(),GLFW_FALSE);m_EditorLayer.RequestProjectExit();
            }
        }
        bool anyAlive = false;
        for (auto& wc : m_Windows)
            anyAlive |= (wc.window && !wc.window->ShouldClose());

        if (!anyAlive || m_EditorLayer.ProjectExitConfirmed()) break;

        if (currentFrame == m_SlowFrame)
            std::this_thread::sleep_for(std::chrono::milliseconds(m_SlowFrameMilliseconds));
        float dt = m_Time.Tick();

        {
            ZoneScopedN("ResourceUpdate");

            if (m_ResourceManager != nullptr)
            {
                constexpr std::size_t maxResourceFinalizationsPerFrame = 4;

                m_ResourceManager->PollAsyncLoads();
                m_ResourceManager->PumpFinalization(
                    maxResourceFinalizationsPerFrame);

                m_ResourceManager->PollHotReload();

                GetWorld().ForEach<ecs::ColliderComponent, ecs::MeshRendererComponent, ecs::TransformComponent>(
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
        }

        {
            ZoneScopedN("GameUpdate");

            if(m_EditorPlayMode)UpdateGameplayInput();
            if(IsSimulationRunning())m_LuaRuntime.Update(dt);
            UpdateCameraController(dt);

            if (IsSimulationRunning() && m_UpdateMode == UpdateMode::Variable) {
                m_StateMachine.Update(*this, dt);
                m_StateMachine.ApplyPending(*this);
            }
        }
        {
            ZoneScopedN("PhysicsFixedFrame");
            m_ActiveCollisionPairs.clear();
            m_PhysicsFrame = {};
            const double physicsDelta = m_ResetPhysicsDelta ? 0.0 : m_Time.GetFrameDeltaTime();
            m_ResetPhysicsDelta = false;
            const auto plan = m_FixedClock.Advance(physicsDelta,
                IsSimulationRunning());
            m_PhysicsFrame.droppedSeconds = plan.droppedSeconds;
            for (int tick = 0; tick < plan.ticks; ++tick) {
                ZoneScopedN("PhysicsFixedTick");
                if (IsSimulationRunning() && m_UpdateMode == UpdateMode::Fixed) {
                    m_StateMachine.Update(*this, float(FixedStepClock::TickSeconds));
                    m_StateMachine.ApplyPending(*this);
                }
                if (IsSimulationRunning() && m_PhysicsSystem) {
                    m_RenderInterpolation.BeginTick(GetWorld());
                    m_LuaRuntime.FixedUpdate(float(FixedStepClock::TickSeconds));
                    GetWorld().ForEach<ecs::CharacterMovementComponent>([&](ecs::Entity e,auto&){ecs::ApplyCharacterMovement(GetWorld(),e);});
                    m_World.UpdateFixedSystemsOn(GetWorld(),float(FixedStepClock::TickSeconds));
                    m_RenderInterpolation.EndTick(GetWorld());
                    m_PhysicsFrame.AddTick(m_PhysicsSystem->GetStatistics());
                    m_FixedSimulationSeconds += FixedStepClock::TickSeconds;
                }
                if (m_ResetPhysicsDelta) break;
            }
            if (m_PhysicsFrame.ticks == 0 && m_PhysicsSystem) {
                m_PhysicsSystem->Update(GetWorld(), 0);
                m_PhysicsFrame.physics = m_PhysicsSystem->GetStatistics();
            }
            m_PhysicsFrame.accumulatorSeconds = m_FixedClock.Accumulator();
            m_PhysicsFrame.totalDroppedSeconds = m_FixedClock.DroppedSeconds();
            m_PhysicsFrame.simulationSeconds = m_FixedSimulationSeconds;
        }
        ApplyStructuralCommands();
        UpdateEcs(dt);
        m_RenderInterpolation.SetFrame(GetWorld(),IsSimulationRunning(),m_FixedClock.Accumulator()/FixedStepClock::TickSeconds);
        if(m_EditorPlayMode&&m_LuaRuntime.HasCameraOwner()){
            if(m_GameplayCamera.following){
                auto* t=GetWorld().GetComponent<ecs::TransformComponent>(m_GameplayCamera.target);
                if(t){auto p=m_RenderInterpolation.Resolve(m_GameplayCamera.target,*t).position;m_GameplayCamera.position=Add(p,m_GameplayCamera.offset);}
                else m_GameplayCamera.following=false;
            }
            m_Camera.position=m_GameplayCamera.position;m_Camera.yaw=m_GameplayCamera.yaw;m_Camera.pitch=m_GameplayCamera.pitch;
        }
        static float fpsTimer = 0.0f;
        static int fpsFrames = 0;

        fpsTimer += m_Time.GetFrameDeltaTime();
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
                    m_Time.GetFrameDeltaTime() * 1000.0f);

                wc.window->SetTitle(title);
            }

            std::ostringstream ss;
            ss << "FPS=" << fps << " dt(ms)=" << m_Time.GetFrameDeltaTime() * 1000.0f;
            Logger::Get().Info(ss.str());

            fpsTimer = 0.0f;
            fpsFrames = 0;
        }

        {
            ZoneScopedN("Render");

            for (auto& wc : m_Windows)
            {
                if (!wc.window || wc.window->ShouldClose()) continue;

                wc.renderer->BeginFrame();
                // Route mouse ownership before the GLFW/ImGui NewFrame pair.
                // Keep the guard through ImGui::Render/EndFrame as well.
                bool mouseButtonsDown=false;double mouseX=0,mouseY=0;
                if(auto* glfwWindow=dynamic_cast<GlfwWindow*>(wc.window.get())){
                    auto* native=glfwWindow->GetGlfwHandle();glfwGetCursorPos(native,&mouseX,&mouseY);
                    for(int b=0;b<=GLFW_MOUSE_BUTTON_LAST;++b)mouseButtonsDown|=glfwGetMouseButton(native,b)==GLFW_PRESS;
                }
                auto cameraMouseFrame=m_EditorLayer.RouteCameraMouse(IsViewportMouseCaptured(),mouseButtonsDown,ImVec2(float(mouseX),float(mouseY)));
                if (wc.editorUiAvailable)
                {
                    wc.renderer->BeginEditorUiFrame();
                    m_EditorLayer.Render(*this, wc.renderer.get(), dt);
                    if(&wc==&m_Windows.front()){
                        const bool capture=ImGui::GetIO().WantCaptureKeyboard||ImGui::GetIO().WantTextInput||m_EditorLayer.BlocksCamera();
                        if(m_ProjectileInput.Update(m_InputManager.IsActionActive("FireProjectile"),m_EditorLayer.ProjectileTestEnabled(),IsEditorPlayMode(),IsEditorPaused(),m_EditorLayer.OwnsViewportKeyboard(),capture,m_Camera.controlsActive))SpawnPhysicsProjectile();
                    }
                }

                if(m_RenderSystem){
                    const auto& preview=m_EditorLayer.GetPlacementPreview();
                    m_RenderSystem->SetPlacementPreview(!IsEditorPlayMode()&&wc.editorUiAvailable&&preview.active&&preview.ready?preview.key:"",preview.transform);
                }
                // Diagnostics own the comparison switches after editor input as well.
                // Ordinary runs continue to apply UI edits immediately.
                if(m_LightingBenchmark||m_ShadowBenchmark||m_PbrBenchmark||m_PbrTexturesBenchmark) {
                    if(m_IblBenchmark&&!m_IblBenchmarkStart&&!m_Windows.empty()&&m_Windows.front().renderer->GetEnvironmentDiagnostics().gpuBytes){m_IblBenchmarkStart=currentFrame+100;m_FrameLimit=m_IblBenchmarkStart+2400;}
            const auto warmup=m_IblBenchmark?m_IblBenchmarkStart:100;
            const auto block=currentFrame>=warmup?(currentFrame-warmup)/300:0;
                    if(!m_LightBenchmark&&!m_PbrTexturesBenchmark&&!m_IblBenchmark)SetGpuInstancingEnabled(currentFrame>=100 && block%2==1);
                    if(m_IblBenchmark){unsigned mask=(!m_IblBenchmarkStart||currentFrame<m_IblBenchmarkStart)?3:block%4;m_EnvironmentSettings.enabled=mask!=0;m_EnvironmentSettings.diffuse=(mask&1)!=0;m_EnvironmentSettings.specular=(mask&2)!=0;}
            if(m_LightBenchmark)SetLightRenderingMode(block%2?LightRenderingMode::Tiled:LightRenderingMode::Forward);
            if(m_LightingBenchmark)m_LightingSettings.enabled=block%4>=2;
            if(m_PbrTexturesBenchmark){const unsigned masks[]={0,1,6,7};SetPbrTextureMask(masks[block%4]);SetShadingModelOverride(1);m_LightingSettings.enabled=true;m_ShadowSettings.enabled=true;m_ShadowSettings.pcf=true;}
            if(m_PbrBenchmark){SetGpuInstancingEnabled(true);SetShadingModelOverride(block%2);m_LightingSettings.enabled=true;m_ShadowSettings.enabled=true;m_ShadowSettings.pcf=true;}
                    if(m_ShadowBenchmark){m_LightingSettings.enabled=true;m_ShadowSettings.enabled=block%6>=2;m_ShadowSettings.pcf=block%6>=4;}
                }

                wc.renderer->SetAntiAliasing(m_AntiAliasing);
                wc.renderer->SetEditorVisuals(m_EditorLayer.BuildVisualFrame(*this));
                const int viewportWidth = m_EditorLayer.GetViewportPixelWidth();
                const int viewportHeight = m_EditorLayer.GetViewportPixelHeight();
                const bool renderSceneToViewport =
                    !m_ProjectBrowserActive && wc.editorUiAvailable &&
                    viewportWidth > 1 &&
                    viewportHeight > 1 &&
                    wc.renderer->BeginViewportRender(viewportWidth, viewportHeight, wc.clear);

                if (renderSceneToViewport)
                {
                    if (m_RenderSystem != nullptr)
                    {
                        m_RenderSystem->SetRenderAdapter(wc.renderer.get());
                        UpdateRenderSystemCameraAspect(static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight));
                    }

                    {
                        ZoneScopedN("WorldSystems");
                        m_RenderInterpolation.SetFrame(GetWorld(), IsSimulationRunning(),
                            m_FixedClock.Accumulator()/FixedStepClock::TickSeconds);
                        editor::SynchronizeSun(GetWorld(),m_Background,m_Background.useIblEnvironment?m_EnvironmentSettings.rotationDegrees:m_Background.skyRotation);
                        m_World.UpdateFrameSystemsOn(GetWorld(),dt);
                    }
                    m_StateMachine.Render(*this, *wc.renderer);
                    wc.renderer->EndViewportRender();
                }

                wc.renderer->Clear(wc.clear[0], wc.clear[1], wc.clear[2], wc.clear[3]);

                if (!renderSceneToViewport&&!m_ProjectBrowserActive)
                {
                    if (m_RenderSystem != nullptr)
                    {
                        m_RenderSystem->SetRenderAdapter(wc.renderer.get());
                        UpdateRenderSystemCamera(wc.window.get());
                    }

                    {
                        ZoneScopedN("WorldSystems");
                        m_RenderInterpolation.SetFrame(GetWorld(), IsSimulationRunning(),
                            m_FixedClock.Accumulator()/FixedStepClock::TickSeconds);
                        editor::SynchronizeSun(GetWorld(),m_Background,m_Background.useIblEnvironment?m_EnvironmentSettings.rotationDegrees:m_Background.skyRotation);
                        m_World.UpdateFrameSystemsOn(GetWorld(),dt);
                    }

                    m_StateMachine.Render(*this, *wc.renderer);
                }

                if (wc.editorUiAvailable)
                    wc.renderer->RenderEditorUiFrame();

                wc.renderer->EndFrame();
                if(!m_FrameCaptureOutput.empty()&&m_FrameLimit&&currentFrame+1==m_FrameLimit)
                    if(!wc.renderer->SaveFrameImage(m_FrameCaptureOutput))Logger::Get().Warn("Frame capture unavailable");

                {
                    ZoneScopedN("Present");
                    wc.renderer->Present();
                    m_LastGpuTimings=wc.renderer->GetGpuTimings();
                }

                if (m_RenderSystem != nullptr)
                    m_RenderSystem->SetRenderAdapter(nullptr);
            }
        }

        if (!m_DiagnosticOutput.empty() && m_PhysicsSystem) {
            const double frameMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count();
            samples.push_back({diagnosticPhase, m_PhysicsSystem->IsParallel(), currentFrame, frameMs, m_PhysicsFrame.physics, m_PhysicsFrame});
        }
        if(m_RenderBenchmark && currentFrame>=(m_IblBenchmark?m_IblBenchmarkStart:100) && (!m_IblBenchmark||m_IblBenchmarkStart) && m_RenderSystem) {
            const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frameStart).count();
            renderSamples.push_back({currentFrame,m_GpuInstancing,ms,m_RenderSystem->GetStatistics(),m_LightingSettings.enabled,m_ShadowSettings.enabled,m_ShadowSettings.pcf,m_ShadowSettings.resolution,m_LastGpuTimings,m_ShadingModelOverride,m_PbrTextureMask,unsigned(m_EnvironmentSettings.enabled?((m_EnvironmentSettings.diffuse?1:0)|(m_EnvironmentSettings.specular?2:0)):0),m_Windows.empty()?EnvironmentDiagnostics{}:m_Windows.front().renderer->GetEnvironmentDiagnostics(),GetLightDiagnostics(),m_EditorLayer.GetStatistics().uiMs,m_EditorLayer.GetStatistics().hierarchyMs,m_EditorLayer.GetStatistics().inspectorMs,unsigned(m_EditorLayer.GetViewportPixelWidth()),unsigned(m_EditorLayer.GetViewportPixelHeight()),m_Windows.empty()?EditorVisualDiagnostics{}:m_Windows.front().renderer->GetEditorVisualDiagnostics(),m_EditorLayer.GetStatistics().assetBrowserMs,m_ShadowSettings.filterTaps,m_Windows.empty()?AntiAliasingDiagnostics{}:m_Windows.front().renderer->GetAntiAliasingDiagnostics(),m_EditorLayer.IsGameView(),m_EditorLayer.GetStatistics().lightGizmoMs});
        }
        FrameMark;
    }
    if (!m_DiagnosticOutput.empty() && !SavePhysicsDiagnostics(m_DiagnosticOutput, samples)) {
        Logger::Get().Error("Diagnostics: cannot write CSV " + m_DiagnosticOutput); return 4;
    }
    if(m_RenderBenchmark && !SaveRenderDiagnostics(m_RenderDiagnosticOutput,renderSamples))return 4;
    return 0;
}

void Application::ReleasePlayerMouse()
{
    if(!m_PlayerMouseCaptured)return;
    if(auto* primary=dynamic_cast<GlfwWindow*>(GetWindow()))if(auto* window=primary->GetGlfwHandle()){
        glfwSetInputMode(window,GLFW_CURSOR,GLFW_CURSOR_NORMAL);
        if(glfwRawMouseMotionSupported())glfwSetInputMode(window,GLFW_RAW_MOUSE_MOTION,GLFW_FALSE);
    }
    m_PlayerMouseCaptured=false;m_InputManager.ResetMouseDelta();
}
bool Application::GameplayInputAllowed() const
{
    if(!IsSimulationRunning())return false;
    auto* primary=dynamic_cast<GlfwWindow*>(const_cast<Application*>(this)->GetWindow());
    auto* window=primary?primary->GetGlfwHandle():nullptr;
    if(!window||!glfwGetWindowAttrib(window,GLFW_FOCUSED)||m_EditorLayer.BlocksGameplayInput())return false;
    double x=0,y=0;glfwGetCursorPos(window,&x,&y);
    return m_PlayerMouseCaptured||m_EditorLayer.IsPointInsideViewport(x,y);
}
bool Application::CaptureGameplayCursor(bool capture)
{
    if(!capture){ReleasePlayerMouse();return true;}
    if(!GameplayInputAllowed())return false;
    auto* primary=dynamic_cast<GlfwWindow*>(GetWindow());auto* window=primary?primary->GetGlfwHandle():nullptr;
    if(!window)return false;
    if(!m_PlayerMouseCaptured){glfwSetInputMode(window,GLFW_CURSOR,GLFW_CURSOR_DISABLED);if(glfwRawMouseMotionSupported())glfwSetInputMode(window,GLFW_RAW_MOUSE_MOTION,GLFW_TRUE);m_PlayerMouseCaptured=true;m_InputManager.ResetMouseDelta();}
    return true;
}
void Application::UpdateGameplayInput()
{
    m_InputManager.BeginFrame();
    auto* primary=dynamic_cast<GlfwWindow*>(GetWindow());auto* window=primary?primary->GetGlfwHandle():nullptr;
    if(!window)return;
    const bool focused=glfwGetWindowAttrib(window,GLFW_FOCUSED)==GLFW_TRUE;
    const bool text=m_EditorLayer.BlocksGameplayInput();
    const bool tab=glfwGetKey(window,GLFW_KEY_TAB)==GLFW_PRESS;
    if(focused&&!text&&glfwGetKey(window,GLFW_KEY_ESCAPE)==GLFW_PRESS){SetEditorPlayMode(false);return;}
    if(!focused||!IsSimulationRunning()||text||(tab&&!m_PlayerAltWasDown))ReleasePlayerMouse();
    m_PlayerAltWasDown=tab;
}
scripting::GameplayServices Application::GameplayCapabilities()
{
    scripting::GameplayServices host;
    host.inputAllowed=[this]{return GameplayInputAllowed();};
    host.pressed=[this](const std::string& a){return GameplayInputAllowed()&&m_InputManager.FramePressed(a);};
    host.released=[this](const std::string& a){return GameplayInputAllowed()&&m_InputManager.FrameReleased(a);};
    host.mouseDelta=[this]{return GameplayInputAllowed()&&m_PlayerMouseCaptured?m_InputManager.MouseDelta():ecs::Vec3{};};
    host.cursorCaptured=[this]{return m_PlayerMouseCaptured;};
    host.captureCursor=[this](bool capture){return CaptureGameplayCursor(capture);};
    host.bindAction=[this](const std::string& action,const std::string& key){return m_InputManager.BindNamedAction(action,key);};
    host.actions=[this]{return m_InputManager.Actions();};
    host.camera=&m_GameplayCamera;return host;
}
void Application::Shutdown()
{
    SetEditorPlayMode(false);m_Mutations.Clear();
    if(!m_Windows.empty())m_EditorLayer.PersistPreferences(*this);
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

    m_EditorLayer.ReleaseAssetPreview(GetRenderAdapter());
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

    if (m_ResourceManager != nullptr)
        m_ResourceManager->Shutdown();

    m_ResourceManager.reset();
    ProjectManager::Activate({});


    if (m_JobSystem != nullptr)
    {
        m_JobSystem->Shutdown();
        m_JobSystem.reset();
    }

    m_World.ClearSystems();
    m_PhysicsSystem = nullptr;
    m_RenderSystem = nullptr;
    m_EcsDebugEntities.clear();
    m_StressEntities.clear();
    m_EcsDebugLogTimer = 0.0f;
    GetWorld().Clear();
    Logger::Get().Shutdown();
}


void Application::RequestStateChange(std::unique_ptr<IGameState> s)
{
    m_StateMachine.ChangeState(std::move(s));
}

void Application::SetupRenderStressScene()
{
    constexpr int rows = 50;
    constexpr int columns = 50;
    constexpr float spacing = 0.18f;

    for (int row = 0; row < rows; ++row)
    {
        for (int column = 0; column < columns; ++column)
        {
            const ecs::Entity entity = GetWorld().CreateEntity();

            auto& transform =
                GetWorld().AddComponent<ecs::TransformComponent>(entity);

            transform.position = ecs::Vec3{
                (column - columns / 2) * spacing,
                (row - rows / 2) * spacing,
                1.5f
            };

            transform.scale = ecs::Vec3{
                0.08f,
                0.08f,
                0.08f
            };

            auto& meshRenderer =
                GetWorld().AddComponent<ecs::MeshRendererComponent>(entity);

            meshRenderer.meshPath =
                "models/validation_cube.obj";

            auto& material =
                GetWorld().AddComponent<ecs::MaterialComponent>(entity);

            material.materialPath =
                "materials/blue.material.json";

            m_EcsDebugEntities.push_back(entity);
        }
    }

    Logger::Get().Info(
        "JobSystem stress scene: spawned " +
        std::to_string(rows * columns) +
        " render entities");
}

void Application::ClearPhysicsStressScene()
{
    ecs::ClearSceneLights(GetWorld());std::erase_if(m_EcsDebugEntities,[&](auto e){return !GetWorld().IsAlive(e);});m_EditorLayer.ResetHistory();
    ResetPhysicsClock();
    ecs::ClearPhysicsStressScene(GetWorld(), m_StressEntities);
    m_EditorLayer.MarkDirty();
    m_ActiveCollisionPairs.clear();
    if (m_PhysicsSystem) m_PhysicsSystem->ResetState();
}

void Application::SetupPhysicsStressScene(int count)
{
    ClearPhysicsStressScene();
    // Replace tracked demo bodies so their speed/size cannot drive the stress timestep.
    for (auto entity : m_EcsDebugEntities) GetWorld().DestroyEntity(entity);
    m_EcsDebugEntities.clear();
    ecs::CreatePhysicsStressScene(GetWorld(), m_StressEntities, count);
    for(auto e:m_StressEntities)GetWorld().AddComponent<ecs::SceneIdentityComponent>(e);
    m_Camera.position = {22.0f, 20.0f, 0.0f};
    m_Camera.yaw = std::atan2(-22.0f, 30.0f);
    m_Camera.pitch = -0.35f;
    m_Camera.farPlane = 150.0f;
    Logger::Get().Info("Physics stress scene: " + std::to_string(m_StressEntities.size() - 1) + " visible cubes");
}
void Application::SetupMaterialDemoScene(int copies)
{
    ClearPhysicsStressScene();for(auto entity:m_EcsDebugEntities)GetWorld().DestroyEntity(entity);m_EcsDebugEntities.clear();
    copies=std::clamp(copies,1,500);
    for(int i=0;i<copies;++i)for(int model=0;model<2;++model) {
        EcsDemoEntityConfig config;config.tag=(model?"glTF":"OBJ")+std::string(" Material Slots ")+std::to_string(i);
        config.meshPath=model?"models/material_slots.gltf":"models/material_slots.obj";
        config.scale={1,1,1};config.simulatePhysics=false;config.useGravity=false;config.bounce=false;
        config.position=copies==1?ecs::Vec3{model?1.5f:-1.5f,0,3}:ecs::Vec3{(i%10-4.5f)*6.f+model*2.8f,(i/100)*2.f,10.f+(i/10%10)*3.f};
        m_EcsDebugEntities.push_back(SpawnEcsDemoEntity(config));
    }
    m_Camera.position=copies==1?ecs::Vec3{0,0,2}:ecs::Vec3{22,20,-12};
    m_Camera.yaw=copies==1?0:std::atan2(-22.f,42.f);m_Camera.pitch=copies==1?0:-.35f;m_Camera.farPlane=150;
    if(m_ResourceManager){(void)m_ResourceManager->LoadAsync<MeshResource>("models/material_slots.obj");(void)m_ResourceManager->LoadAsync<MeshResource>("models/material_slots.gltf");(void)m_ResourceManager->LoadAsync<ShaderResource>("dx12/textured.hlsl");}
}

void Application::SetupPbrDemoScene()
{
    ClearPhysicsStressScene();for(auto entity:m_EcsDebugEntities)GetWorld().DestroyEntity(entity);m_EcsDebugEntities.clear();
    for(int row=0;row<3;++row)for(int column=0;column<5;++column) {
        EcsDemoEntityConfig config;config.tag="PBR metallic="+std::to_string(row*.5f)+" roughness="+std::to_string(.1f+column*.2f);
        config.meshPath="models/pbr_sphere.obj";config.shaderPath="dx12/textured.hlsl";config.texturePath="defaults/texture";
        config.position={float(column-2)*2.5f,float(2-row)*2.5f+1,8};config.scale={1,1,1};
        config.simulatePhysics=false;config.useGravity=false;config.bounce=false;
        config.materialTint[0]=.9f;config.materialTint[1]=.55f;config.materialTint[2]=.22f;
        config.materialOverrides.shadingModel=ShadingModel::PBR;config.materialOverrides.metallic=row*.5f;config.materialOverrides.roughness=.1f+column*.2f;
        m_EcsDebugEntities.push_back(SpawnEcsDemoEntity(config));
    }
    EcsDemoEntityConfig floor;floor.tag="PBR floor";floor.meshPath="models/validation_cube.obj";floor.texturePath="defaults/texture";floor.shaderPath="dx12/textured.hlsl";
    floor.position={0,-.15f,8};floor.scale={18,.3f,12};floor.simulatePhysics=false;floor.useGravity=false;floor.bounce=false;
    floor.materialOverrides.shadingModel=ShadingModel::PBR;floor.materialOverrides.roughness=.8f;
    m_EcsDebugEntities.push_back(SpawnEcsDemoEntity(floor));
    m_Camera.position={0,4,-8};m_Camera.yaw=0;m_Camera.pitch=-.06f;m_Camera.farPlane=150;
    m_LightingSettings.directional.direction={-.3f,-.5f,1};m_LightingSettings.directional.color={1,1,1};m_LightingSettings.directional.intensity=2;
    m_LightingSettings.ambientIntensity=.15f;m_ShadowSettings.enabled=true;
    if(m_ResourceManager)(void)m_ResourceManager->LoadAsync<MeshResource>("models/pbr_sphere.obj");
    SetShadingModelOverride(-1);SetEditorPlayMode(false);
}

void Application::SetupPbrTexturesBenchmarkScene(int count)
{
    SetupPhysicsStressScene(count);SetEditorPlayMode(false);
    GetWorld().ForEach<ecs::MeshRendererComponent,ecs::MaterialComponent>([](ecs::Entity,auto&,auto& material) {
        material.overrides.shadingModel=ShadingModel::PBR;material.overrides.metallic=.7f;material.overrides.roughness=.7f;
        material.overrides.normalTexturePath="textures/pbr_normal.png";
        material.overrides.metallicRoughnessTexturePath="textures/pbr_orm.png";
        material.overrides.occlusionTexturePath="textures/pbr_orm.png";
    });
}
void Application::SetupPbrTexturesDemoScene()
{
    SetupPbrDemoScene();
    GetWorld().ForEach<ecs::MeshRendererComponent,ecs::MaterialComponent>([](ecs::Entity,auto& mesh,auto& material) {
        if(mesh.meshPath!="models/pbr_sphere.obj")return;
        mesh.texturePath="textures/pbr_base.png";material.texturePath.clear();
        material.overrides.normalTexturePath="textures/pbr_normal.png";
        material.overrides.metallicRoughnessTexturePath="textures/pbr_orm.png";
        material.overrides.occlusionTexturePath="textures/pbr_orm.png";
        material.overrides.occlusionStrength=.8f;
    });
    for(int i=0;i<2;++i) {
        EcsDemoEntityConfig config;config.tag=i?"Embedded glTF mirrored node":"External glTF authored tangent";
        config.meshPath=i?"models/pbr_textures_embedded.gltf":"models/pbr_textures.gltf";
        config.position={i?2.5f:-2.5f,8.5f,8};config.scale={1,1,1};config.simulatePhysics=false;config.useGravity=false;config.bounce=false;
        m_EcsDebugEntities.push_back(SpawnEcsDemoEntity(config));
    }
    m_Camera.position={0,5,-12};SetPbrTextureMask(7);
}

void Application::SetupShadowQualityDemoScene()
{
    ClearPhysicsStressScene();for(auto entity:m_EcsDebugEntities)GetWorld().DestroyEntity(entity);m_EcsDebugEntities.clear();
    auto add=[&](const char* tag,const char* mesh,ecs::Vec3 position,ecs::Vec3 scale,ecs::Vec3 rotation,std::array<float,4> tint,bool imported=false) {
        EcsDemoEntityConfig config;config.tag=tag;config.meshPath=mesh;config.position=position;config.scale=scale;config.rotation=rotation;
        config.simulatePhysics=false;config.useGravity=false;config.bounce=false;
        if(!imported){config.shaderPath="dx12/textured.hlsl";config.texturePath="defaults/texture";for(int i=0;i<4;++i)config.materialTint[i]=tint[i];}
        if(std::string(mesh)=="models/shadow_inclined.obj")config.materialOverrides.doubleSided=true;
        m_EcsDebugEntities.push_back(SpawnEcsDemoEntity(config));
        if(m_ResourceManager)(void)m_ResourceManager->LoadAsync<MeshResource>(mesh);
    };
    add("Receiver floor","models/validation_cube.obj",{0,-.1f,8},{18,.2f,18},{},{.7f,.7f,.7f,1});
    add("Contact cube","models/validation_cube.obj",{-4,.5f,6},{1,1,1},{},{1,.6f,.2f,1});
    add("Rotated nonuniform","models/validation_cube.obj",{-1,1.3f,6},{1.6f,1.2f,.8f},{.2f,.5f,.3f},{.4f,1,.5f,1});
    add("Negative scale","models/validation_cube.obj",{2,.7f,6},{-1,1.4f,1},{0,.4f,0},{1,.4f,.4f,1});
    add("Blue inclined plane","models/shadow_inclined.obj",{-4,1,10},{1,1,1},{},{.15f,.4f,1,1});
    add("African head OBJ","models/african_head.obj",{0,1.3f,10},{1.2f,1.2f,1.2f},{},{.8f,.7f,.6f,1});
    add("Sphere glTF","models/lighting_sphere.gltf",{3.7f,-1.6f,7.4f},{.65f,.65f,.65f},{},{.6f,.5f,1,1});
    add("Material slots OBJ","models/material_slots.obj",{0,1,13},{1,1,1},{},{1,1,1,1},true);
    add("Material slots glTF","models/material_slots.gltf",{3,1,13},{1,1,1},{},{1,1,1,1},true);
    m_Camera.position={11,9,-3};m_Camera.yaw=std::atan2(-11.f,11.f);m_Camera.pitch=-.42f;m_Camera.farPlane=150;
    SetEditorPlayMode(false);
}

void Application::RunAsyncResourceStressTest()
{
    if (m_AsyncResourceStressStarted)
        return;

    if (m_ResourceManager == nullptr)
        return;

    m_AsyncResourceStressStarted = true;

    constexpr int textureCount = 8;

    Logger::Get().Info(
        "Async resource stress: starting textures=" +
        std::to_string(textureCount));

    for (int i = 0; i < textureCount; ++i)
    {
        char texturePath[128];

        std::snprintf(
            texturePath,
            sizeof(texturePath),
            "textures/stress/head_%02d.dds",
            i + 1);

        //
        // Сразу ставим CPU load в JobSystem.
        //
        (void)m_ResourceManager->LoadAsync<TextureResource>(
            texturePath);

        //
        // Создаём реальный объект, который использует ресурс.
        // RenderSystem увидит Loading resource и сначала покажет fallback.
        //
        EcsDemoEntityConfig entityCfg;

        entityCfg.tag =
            "AsyncStress_" + std::to_string(i);

        entityCfg.meshPath =
            "models/validation_cube.obj";

        entityCfg.texturePath =
            texturePath;

        entityCfg.shaderPath =
            "dx12/textured.hlsl";

        entityCfg.materialPath.clear();

        const int column = i % 4;
        const int row = i / 4;

        entityCfg.position = ecs::Vec3{
            -2.25f + static_cast<float>(column) * 1.5f,
            1.5f - static_cast<float>(row) * 1.5f,
            0.0f
        };

        entityCfg.scale =
            ecs::Vec3{ 0.65f, 0.65f, 0.65f };

        //
        // Чтобы stress test ресурсов не превратился
        // одновременно в physics benchmark.
        //
        entityCfg.bounce = false;
        entityCfg.simulatePhysics = false;
        entityCfg.isStatic = true;
        entityCfg.useGravity = false;

        //
        // Очень важно:
        // иначе SpawnEcsDemoEntity() может вызвать
        // TryBuildMeshCollider(), а тот делает sync Load<Mesh>().
        //
        entityCfg.colliderManual = true;
        entityCfg.colliderHalfExtents =
            ecs::Vec3{ 0.325f, 0.325f, 0.325f };

        const ecs::Entity entity =
            SpawnEcsDemoEntity(entityCfg);

        m_EcsDebugEntities.push_back(entity);
    }

    Logger::Get().Info(
        "Async resource stress: all requests scheduled");
}

void Application::RunSyncResourceStressTest()
{
    ZoneScopedN("SyncResourceStress");

    if (m_ResourceManager == nullptr)
        return;

    constexpr int textureCount = 8;

    Logger::Get().Info(
        "Sync resource stress: starting textures=" +
        std::to_string(textureCount));

    for (int i = 0; i < textureCount; ++i)
    {
        ZoneScopedN("SyncTextureLoad");

        char texturePath[128];

        std::snprintf(
            texturePath,
            sizeof(texturePath),
            "textures/stress/head_%02d.dds",
            i + 1);

        (void)m_ResourceManager->Load<TextureResource>(
            texturePath);
    }

    Logger::Get().Info(
        "Sync resource stress: complete");
}
ecs::Entity Application::CreateLightEntity(LightType type){
    EcsDemoEntityConfig c;c.tag=type==LightType::Directional?"Directional Light":type==LightType::Point?"Point Light":"Spot Light";
    const float cp=std::cos(m_Camera.pitch);
    c.position={m_Camera.position.x+3*std::sin(m_Camera.yaw)*cp,m_Camera.position.y+3*std::sin(m_Camera.pitch),m_Camera.position.z+3*std::cos(m_Camera.yaw)*cp};c.scale={1,1,1};
    c.rotation=type==LightType::Directional?ecs::Vec3{1.1f,-.7f,0}:ecs::Vec3{-m_Camera.pitch,m_Camera.yaw,0};
    c.light=SceneLightDescription{};c.light->type=type;c.light->parameters.castShadows=type==LightType::Directional;
    c.simulatePhysics=false;c.bounce=false;auto e=SpawnEcsDemoEntity(c);m_EcsDebugEntities.push_back(e);return e;
}
bool Application::DestroySceneEntity(ecs::Entity e){bool result=GetWorld().DestroyEntity(e);std::erase(m_EcsDebugEntities,e);std::erase(m_StressEntities,e);return result;}
void Application::SetupIblDemoScene(){SetupPbrTexturesDemoScene();m_EnvironmentSettings.enabled=true;m_EnvironmentSettings.toneMapping=true;m_LightingSettings.directional.enabled=false;m_LightingSettings.ambientIntensity=0;SetEditorPlayMode(false);}

void Application::SetupIblBenchmarkScene(int count){SetupPbrTexturesBenchmarkScene(count);m_EnvironmentSettings.enabled=true;SetShadingModelOverride(1);m_ResourceManager->LoadAsync<EnvironmentResource>(m_EnvironmentSettings.assetPath);}

void Application::SetupLightStressScene(int count,bool overlapping,int objects)
{
    count=std::clamp(count,0,16384);SetupPbrTexturesBenchmarkScene(objects);
    m_LightingSettings.enabled=true;m_LightingSettings.directional.enabled=false;m_LightingSettings.ambientIntensity=0;
    m_ShadowSettings.enabled=false;m_EnvironmentSettings.enabled=true;m_EnvironmentSettings.intensity=.15f;SetShadingModelOverride(1);
    for(int i=0;i<count;++i){
        auto type=i%3==0?LightType::Spot:LightType::Point;auto e=CreateLightEntity(type);
        auto& t=*GetWorld().GetComponent<ecs::TransformComponent>(e);
        t.position=overlapping?ecs::Vec3{0,7,30}:ecs::Vec3{float(i%20-10)*1.2f,3+float(i/400)*1.5f,19+float(i/20%20)*1.2f};
        t.rotation={1.570796327f,0,0};auto& l=*ecs::GetLightParameters(GetWorld(),e);
        l.range=overlapping?45.f:3.f;l.intensity=overlapping?12.f/std::max(1,count):4.f;
        l.color={i%3==0?1.f:.2f,i%3==1?1.f:.2f,i%3==2?1.f:.2f};l.innerAngle=25;l.outerAngle=45;
    }
    m_EditorLayer.ResetHistory();m_EditorLayer.MarkDirty();
}

void Application::SetupLightDemoScene(){
    SetupIblDemoScene();m_EnvironmentSettings.intensity=.25f;m_ShadowSettings.enabled=true;
    auto dir=CreateLightEntity(LightType::Directional);auto& d=*GetWorld().GetComponent<ecs::DirectionalLightComponent>(dir);d.primary=true;d.intensity=.7f;
    auto& dt=*GetWorld().GetComponent<ecs::TransformComponent>(dir);dt.position={-4,5,8};dt.rotation={1.1f,-.7f,0};
    auto point=CreateLightEntity(LightType::Point);auto& p=*GetWorld().GetComponent<ecs::PointLightComponent>(point);p.range=8;p.color={1,.15f,.04f};p.intensity=18;
    GetWorld().GetComponent<ecs::TransformComponent>(point)->position={-2,3,6};
    auto spot=CreateLightEntity(LightType::Spot);auto& l=*GetWorld().GetComponent<ecs::SpotLightComponent>(spot);l.range=12;l.color={.08f,.3f,1};l.intensity=30;l.innerAngle=20;l.outerAngle=40;
    auto& t=*GetWorld().GetComponent<ecs::TransformComponent>(spot);t.position={3,5,4};t.rotation={.7f,-.35f,0};m_EditorLayer.ResetHistory();m_EditorLayer.MarkDirty();m_EditorLayer.SelectEntity(spot);
}

namespace {
std::filesystem::path SafeScenePath(const std::string& key,bool output){
    if(auto project=ProjectManager::Active())return project->ResolveScene(key);
    const auto staged=AssetPaths::ResolveAssetRoot();if(output)return editor::AssetImport::SafeTarget(staged,key);
    for(const auto& root:{staged,editor::AssetImport::ProjectRoot()}){auto candidate=editor::AssetImport::SafeTarget(root,key);if(std::filesystem::is_regular_file(candidate))return candidate;}
    return editor::AssetImport::SafeTarget(staged,key);
}
bool SceneKeyValid(const std::string& key){
    if(auto project=ProjectManager::Active()){try{project->ResolveScene(key);return true;}catch(...){return false;}}
    if(!key.starts_with("scenes/")||std::filesystem::u8path(key).extension()!=".json")return false;
    return ecs::ComponentRegistry::ValidateAssetKey(key,ecs::AssetKind::None);
}
}
SceneSettings Application::GetSceneSettings()const{
    SceneSettings s;s.lighting=m_LightingSettings;s.environment=m_EnvironmentSettings;s.shadows=m_ShadowSettings;s.background=m_Background;s.antiAliasing=m_AntiAliasing;
    s.primaryDirectional=SceneSerializer::PrimaryDirectionalId(const_cast<ecs::World&>(GetWorld()));return s;
}
bool Application::SetSceneSettings(const SceneSettings& s,std::string* error){
    try{(void)SceneSettingsToJson(s);const bool enabled=m_LightingSettings.enabled;const int debug=m_ShadowSettings.debugMode;
        m_LightingSettings=s.lighting;m_LightingSettings.enabled=enabled;m_EnvironmentSettings=s.environment;m_ShadowSettings=s.shadows;m_Background=s.background;m_AntiAliasing=s.antiAliasing;m_ShadowSettings.debugMode=debug;editor::SynchronizeSun(GetWorld(),m_Background,m_Background.useIblEnvironment?m_EnvironmentSettings.rotationDegrees:m_Background.skyRotation);return true;
    }catch(const std::exception& e){if(error)*error=e.what();return false;}
}
bool Application::SaveSceneAs(const std::string& key,bool overwrite,std::string* error){
    if(IsEditorPlayMode()){if(error)*error="Stop Play before saving the authoring scene";return false;}
    if(!SceneKeyValid(key)){if(error)*error="Scene key must be a portable scenes/*.json path";return false;}
    try{
        m_EditorLayer.FinishTransaction(*this);auto document=SceneSerializer::CaptureWorld(m_World,GetSceneSettings());
        if(!SceneSerializer::WriteAtomic(SafeScenePath(key,true),document,error,overwrite))return false;
        m_SceneKey=key;m_Config.ecsDemo.sceneFile=key;m_EditorLayer.MarkSaved();m_EditorLayer.RememberScene(key);InitializeConfigHotReload();return true;
    }catch(const std::exception& e){if(error)*error=e.what();return false;}
}
bool Application::OpenScene(const std::string& key,std::string* error){
    if(!SceneKeyValid(key)){if(error)*error="Scene key must be a portable scenes/*.json path";return false;}
    // Parse/validate completely before changing modes or discarding the current scene.
    ecs::World staged;SceneSettings settings;std::filesystem::path path;try{path=SafeScenePath(key,false);}catch(const std::exception& e){if(error)*error=e.what();return false;}
    if(!SceneSerializer::LoadWorld(path,staged,settings,error))return false;
    SetEditorPlayMode(false);m_Mutations.Clear();if(m_PhysicsSystem)m_PhysicsSystem->ResetState();ResetPhysicsClock();
    m_World.ReplaceData(staged);m_EcsDebugEntities=m_World.Entities();m_StressEntities.clear();m_ActiveCollisionPairs.clear();
    SetSceneSettings(settings);m_SceneKey=key;m_Config.ecsDemo.sceneFile=key;m_EditorLayer.ResetHistory();m_EditorLayer.MarkSaved();m_EditorLayer.RememberScene(key);InitializeConfigHotReload();return true;
}
void Application::NewScene(){
    SetEditorPlayMode(false);ClearPhysicsStressScene();m_World.Clear();m_EcsDebugEntities.clear();m_StressEntities.clear();m_Mutations.Clear();
    m_SceneKey.clear();m_Config.ecsDemo.sceneFile.clear();SetSceneSettings(SceneSettings{});m_EditorLayer.ResetHistory();m_EditorLayer.MarkSaved();
}
ecs::Entity Application::CreateEmptyEntity(const std::string& name){
    auto e=GetWorld().CreateEntity();GetWorld().AddComponent<ecs::TagComponent>(e).name=name;GetWorld().AddComponent<ecs::TransformComponent>(e);GetWorld().AddComponent<ecs::SceneIdentityComponent>(e);m_EcsDebugEntities.push_back(e);return e;
}
void Application::NotifyEditorMutation(ecs::Entity entity){
    if(entity.IsValid()&&GetWorld().IsAlive(entity)){
        NotifyTeleport(entity);if(auto* rb=GetWorld().GetComponent<ecs::RigidbodyComponent>(entity)){rb->sleeping=false;rb->sleepTimer=0;}
    }
    if(m_PhysicsSystem)m_PhysicsSystem->ResetState();
}
void Application::FocusEntity(ecs::Entity entity){
    FocusEntities({entity});
}
void Application::FocusEntities(const std::vector<ecs::Entity>& entities){
    ecs::Vec3 low{FLT_MAX,FLT_MAX,FLT_MAX},high{-FLT_MAX,-FLT_MAX,-FLT_MAX};bool valid=false;
    auto include=[&](ecs::Vec3 p){
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return;
        low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);low.z=std::min(low.z,p.z);
        high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);high.z=std::max(high.z,p.z);valid=true;
    };
    for(auto entity:entities)if(auto* t=GetWorld().GetComponent<ecs::TransformComponent>(entity)){
        bool geometry=false;
        if(m_ResourceManager)if(auto* mr=GetWorld().GetComponent<ecs::MeshRendererComponent>(entity)){
            auto mesh=m_ResourceManager->Get<MeshResource>(mr->meshPath);
            if(mesh&&mesh->IsLoaded()&&!mesh->GetData().meshData.vertices.empty()){
                ecs::Vec3 lo{FLT_MAX,FLT_MAX,FLT_MAX},hi{-FLT_MAX,-FLT_MAX,-FLT_MAX};
                for(const auto& v:mesh->GetData().meshData.vertices){lo.x=std::min(lo.x,v.position[0]);lo.y=std::min(lo.y,v.position[1]);lo.z=std::min(lo.z,v.position[2]);hi.x=std::max(hi.x,v.position[0]);hi.y=std::max(hi.y,v.position[1]);hi.z=std::max(hi.z,v.position[2]);}
                float m[16];ecs::BuildRenderModelMatrix(m,ecs::RenderPose::From(*t));
                for(int i=0;i<8;++i){const ecs::Vec3 p{i&1?hi.x:lo.x,i&2?hi.y:lo.y,i&4?hi.z:lo.z};include({m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]});}
                geometry=true;
            }
        }
        if(!geometry){
            ecs::Vec3 extent{std::max(.5f,std::abs(t->scale.x)*.5f),std::max(.5f,std::abs(t->scale.y)*.5f),std::max(.5f,std::abs(t->scale.z)*.5f)};
            if(auto* light=ecs::GetLightParameters(GetWorld(),entity)){float r=std::clamp(light->range,1.f,30.f);extent={r,r,r};}
            include({t->position.x-extent.x,t->position.y-extent.y,t->position.z-extent.z});include({t->position.x+extent.x,t->position.y+extent.y,t->position.z+extent.z});
        }
    }
    if(!valid)return;
    const ecs::Vec3 center{(low.x+high.x)*.5f,(low.y+high.y)*.5f,(low.z+high.z)*.5f};
    const float radius=.5f*std::sqrt(std::pow(high.x-low.x,2)+std::pow(high.y-low.y,2)+std::pow(high.z-low.z,2));
    const float aspect=float(std::max(1,m_EditorLayer.GetViewportPixelWidth()))/std::max(1,m_EditorLayer.GetViewportPixelHeight());
    const float halfFov=std::min(m_Camera.verticalFovRadians*.5f,std::atan(std::tan(m_Camera.verticalFovRadians*.5f)*aspect));
    const float distance=std::max(2.f,radius/std::sin(halfFov)*1.1f),cp=std::cos(m_Camera.pitch);
    m_Camera.position={center.x-distance*std::sin(m_Camera.yaw)*cp,center.y-distance*std::sin(m_Camera.pitch),center.z-distance*std::cos(m_Camera.yaw)*cp};
}