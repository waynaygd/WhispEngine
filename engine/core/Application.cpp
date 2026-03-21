#include "Application.h"
#include "Logger.h"
#include "ConfigLoader.h"

#include "../ecs/components/BoundsBounceComponent.h"
#include "../ecs/components/MeshRendererComponent.h"
#include "../ecs/components/TagComponent.h"
#include "../ecs/components/TransformComponent.h"
#include "../ecs/components/VelocityComponent.h"
#include "../ecs/systems/BoundsBounceSystem.h"
#include "../ecs/systems/MotionSystem.h"
#include "../platform/GlfwWindow.h"
#include "../render/IRenderAdapter.h"
#include <GLFW/glfw3.h>

#include "../game/states/LoadingState.h"

#include <filesystem>
#include <array>
#include <sstream>

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

    for (fs::path probe = cwd.parent_path(); !probe.empty(); probe = probe.parent_path())
    {
        const fs::path candidate = probe / relativeConfig;
        if (fs::exists(candidate))
            return candidate.string();
    }

    const fs::path localCandidate = cwd / relativeConfig;
    if (fs::exists(localCandidate))
        return localCandidate.string();

    return relativeConfig.string();
}

static const char* PrimitiveTypeToString(ecs::PrimitiveType primitive)
{
    switch (primitive)
    {
    case ecs::PrimitiveType::Line:     return "Line";
    case ecs::PrimitiveType::Triangle: return "Triangle";
    case ecs::PrimitiveType::Quad:     return "Quad";
    case ecs::PrimitiveType::Cube:     return "Cube";
    default:                           return "Unknown";
    }
}

std::vector<EcsDemoEntityConfig> Application::BuildDefaultEcsDemoEntities()
{
    return {
        EcsDemoEntityConfig{ -0.80f,  0.45f, 0.65f,  0.00f,  0.35f, -0.08f,  1.25f },
        EcsDemoEntityConfig{  0.55f,  0.55f, 0.40f,  0.20f, -0.28f, -0.16f, -1.80f },
        EcsDemoEntityConfig{ -0.20f, -0.55f, 0.50f, -0.40f,  0.18f,  0.22f,  0.95f },
        EcsDemoEntityConfig{  0.72f, -0.22f, 0.30f,  0.00f, -0.20f,  0.14f,  2.40f }
    };
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
    firstTransform.x = 1.5f;
    firstTransform.y = -0.5f;
    firstTransform.scale = 2.0f;

    Logger::Get().Info(std::string("ECS bootstrap: first has transform -> ") +
        (m_World.HasComponent<ecs::TransformComponent>(first) ? "true" : "false"));

    if (const auto* transform = m_World.GetComponent<ecs::TransformComponent>(first))
    {
        std::ostringstream ss;
        ss << "ECS bootstrap: first transform x=" << transform->x
           << " y=" << transform->y
           << " scale=" << transform->scale
           << " angle=" << transform->angle;
        Logger::Get().Info(ss.str());
    }

    auto& firstTag = m_World.AddComponent<ecs::TagComponent>(first);
    firstTag.name = "BootstrapEntity";
    Logger::Get().Info(std::string("ECS bootstrap: first tag -> ") + firstTag.name);

    auto& firstMeshRenderer = m_World.AddComponent<ecs::MeshRendererComponent>(first);
    firstMeshRenderer.primitive = ecs::PrimitiveType::Triangle;
    firstMeshRenderer.color[0] = 0.85f;
    firstMeshRenderer.color[1] = 0.25f;
    firstMeshRenderer.color[2] = 0.25f;
    firstMeshRenderer.color[3] = 1.0f;
    Logger::Get().Info(
        std::string("ECS bootstrap: first mesh renderer primitive -> ") +
        PrimitiveTypeToString(firstMeshRenderer.primitive));

    m_World.AddComponent<ecs::TransformComponent>(recycled, ecs::TransformComponent{ -2.0f, 3.0f, 0.75f, 0.5f });
    Logger::Get().Info(std::string("ECS bootstrap: recycled has transform before remove -> ") +
        (m_World.HasComponent<ecs::TransformComponent>(recycled) ? "true" : "false"));

    const bool removedTransform = m_World.RemoveComponent<ecs::TransformComponent>(recycled);
    Logger::Get().Info(std::string("ECS bootstrap: remove transform from recycled -> ") +
        (removedTransform ? "ok" : "failed"));
    Logger::Get().Info(std::string("ECS bootstrap: recycled has transform after remove -> ") +
        (m_World.HasComponent<ecs::TransformComponent>(recycled) ? "true" : "false"));

    m_World.AddComponent<ecs::TransformComponent>(third, ecs::TransformComponent{ 4.0f, 2.0f, 1.25f, 0.0f });
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
    m_EcsSystems.Clear();
    m_EcsSystems.AddSystem<ecs::MotionSystem>();
    m_EcsSystems.AddSystem<ecs::BoundsBounceSystem>();

    m_EcsDebugEntities.clear();
    const std::vector<EcsDemoEntityConfig> entities =
        m_Config.ecsDemo.initialEntities.empty() ? BuildDefaultEcsDemoEntities() : m_Config.ecsDemo.initialEntities;

    for (const auto& entityCfg : entities)
    {
        m_EcsDebugEntities.push_back(SpawnEcsDemoEntity(
            entityCfg.x,
            entityCfg.y,
            entityCfg.scale,
            entityCfg.angle,
            entityCfg.vx,
            entityCfg.vy,
            entityCfg.angularVelocity));
    }

    m_EcsDebugLogTimer = 0.0f;

    Logger::Get().Info("ECS runtime: motion system registered");
    Logger::Get().Info("ECS runtime: bounds bounce system registered");
    Logger::Get().Info("ECS runtime: demo scene created with " + std::to_string(m_EcsDebugEntities.size()) + " ECS entities");
    Logger::Get().Info("ECS runtime: render system ready");
}

ecs::Entity Application::SpawnEcsDemoEntity(
    float x, float y, float scale, float angle, float vx, float vy, float angularVelocity)
{
    const ecs::Entity entity = m_World.CreateEntity();
    const std::size_t entityOrdinal = m_EcsDebugEntities.size();

    auto& transform = m_World.AddComponent<ecs::TransformComponent>(entity);
    transform.x = x;
    transform.y = y;
    transform.scale = scale;
    transform.angle = angle;

    auto& velocity = m_World.AddComponent<ecs::VelocityComponent>(entity);
    velocity.vx = vx;
    velocity.vy = vy;
    velocity.angularVelocity = angularVelocity;

    auto& tag = m_World.AddComponent<ecs::TagComponent>(entity);
    tag.name = "DemoEntity_" + std::to_string(entityOrdinal);

    auto& meshRenderer = m_World.AddComponent<ecs::MeshRendererComponent>(entity);
    meshRenderer.primitive = ecs::PrimitiveType::Triangle;
    static constexpr float palette[][4] =
    {
        { 0.94f, 0.42f, 0.32f, 1.0f },
        { 0.30f, 0.78f, 0.44f, 1.0f },
        { 0.26f, 0.57f, 0.92f, 1.0f },
        { 0.95f, 0.80f, 0.28f, 1.0f },
        { 0.71f, 0.38f, 0.88f, 1.0f },
        { 0.22f, 0.82f, 0.83f, 1.0f },
    };
    const auto& color = palette[entityOrdinal % (sizeof(palette) / sizeof(palette[0]))];
    meshRenderer.color[0] = color[0];
    meshRenderer.color[1] = color[1];
    meshRenderer.color[2] = color[2];
    meshRenderer.color[3] = color[3];
    m_World.AddComponent<ecs::BoundsBounceComponent>(entity);

    std::ostringstream ss;
    ss << "ECS runtime: spawned demo entity -> " << m_World.DebugDescribeEntity(entity)
       << " tag=" << tag.name
       << " primitive=" << PrimitiveTypeToString(meshRenderer.primitive)
       << " color=(" << meshRenderer.color[0] << ", " << meshRenderer.color[1]
       << ", " << meshRenderer.color[2] << ", " << meshRenderer.color[3] << ")";
    Logger::Get().Info(ss.str());
    return entity;
}

void Application::EnterGameplayScene()
{
    Logger::Get().Info("Application: enter gameplay scene -> setup ECS scene");
    m_World.Clear();
    SetupEcsRuntimeDemo();
}

void Application::ExitGameplayScene()
{
    Logger::Get().Info("Application: exit gameplay scene -> clear ECS scene");
    m_EcsSystems.Clear();
    m_EcsDebugEntities.clear();
    m_EcsDebugLogTimer = 0.0f;
    m_World.Clear();
}

ecs::Entity Application::SpawnGameplayEntity()
{
    struct SpawnPreset
    {
        float x;
        float y;
        float scale;
        float angle;
        float vx;
        float vy;
        float angularVelocity;
    };

    static constexpr std::array<SpawnPreset, 6> presets =
    {{
        { -0.65f,  0.15f, 0.32f,  0.15f,  0.42f,  0.21f,  1.70f },
        {  0.68f,  0.05f, 0.28f, -0.35f, -0.36f,  0.18f, -1.45f },
        { -0.15f,  0.72f, 0.36f,  0.00f,  0.17f, -0.33f,  2.15f },
        {  0.12f, -0.68f, 0.34f,  0.40f, -0.24f,  0.29f, -2.30f },
        { -0.78f, -0.12f, 0.26f, -0.20f,  0.48f, -0.12f,  1.25f },
        {  0.78f, -0.58f, 0.30f,  0.55f, -0.31f,  0.27f,  1.95f },
    }};

    const SpawnPreset& preset = presets[m_EcsDebugEntities.size() % presets.size()];
    const ecs::Entity entity = SpawnEcsDemoEntity(
        preset.x,
        preset.y,
        preset.scale,
        preset.angle,
        preset.vx,
        preset.vy,
        preset.angularVelocity);

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

void Application::UpdateEcs(float dt)
{
    m_EcsSystems.Update(m_World, dt);

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
        if (transform == nullptr || velocity == nullptr || meshRenderer == nullptr)
            continue;

        ss << " \n e" << i
           << " tag=" << (tag != nullptr ? tag->name : "<unnamed>")
           << " pos=(" << transform->x << ", " << transform->y << ")"
           << " scale=" << transform->scale
           << " angle=" << transform->angle
           << " vel=(" << velocity->vx << ", " << velocity->vy << ")"
           << " primitive=" << PrimitiveTypeToString(meshRenderer->primitive);
    }
    Logger::Get().Info(ss.str());
}

bool Application::Initialize()
{
    Logger::Get().Initialize("engine.log");
    Logger::Get().Info("Application Initialize");
    RunEcsBootstrapCheck();

    m_Time.Initialize();

    const std::string configPath = ResolveConfigPath();
    Logger::Get().Info("Application: using config file: " + configPath);

    m_Config = AppConfig{};
    m_Config.ecsDemo.initialEntities = BuildDefaultEcsDemoEntities();

    const bool configLoaded = ConfigLoader::Load(configPath, m_Config);

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

    m_Windows.push_back(std::move(ctx));

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

        UpdateEcs(dt);

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
            wc.renderer->Clear(wc.clear[0], wc.clear[1], wc.clear[2], wc.clear[3]);

            m_StateMachine.Render(*this, *wc.renderer);
            m_RenderSystem.Render(m_World, *wc.renderer);

            wc.renderer->EndFrame();
            wc.renderer->Present();
        }
    }
    return 0;
}

void Application::Shutdown()
{
    for (auto& wc : m_Windows)
        if (wc.renderer) wc.renderer->Shutdown();

    m_Windows.clear();
    ExitGameplayScene();
    Logger::Get().Shutdown();
}


void Application::RequestStateChange(std::unique_ptr<IGameState> s)
{
    m_StateMachine.ChangeState(std::move(s));
}
