#include "Application.h"
#include "Logger.h"
#include "ConfigLoader.h"

#include "../ecs/components/BoundsBounceComponent.h"
#include "../ecs/components/TransformComponent.h"
#include "../ecs/components/TriangleRenderComponent.h"
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

#include <cmath>

static float SmoothFactor(float smooth, float dt)
{
    return 1.0f - std::exp(-smooth * dt);
}

static void BuildMVP(float* out16, float x, float y, float s, float a)
{
    float c = std::cos(a);
    float sn = std::sin(a);

    out16[0] = s * c;  out16[4] = -s * sn; out16[8] = 0.0f; out16[12] = x;
    out16[1] = s * sn; out16[5] = s * c;  out16[9] = 0.0f; out16[13] = y;
    out16[2] = 0.0f;   out16[6] = 0.0f;    out16[10] = 1.0f; out16[14] = 0.0f;
    out16[3] = 0.0f;   out16[7] = 0.0f;    out16[11] = 0.0f; out16[15] = 1.0f;
}

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
}

ecs::Entity Application::SpawnEcsDemoEntity(
    float x, float y, float scale, float angle, float vx, float vy, float angularVelocity)
{
    const ecs::Entity entity = m_World.CreateEntity();

    auto& transform = m_World.AddComponent<ecs::TransformComponent>(entity);
    transform.x = x;
    transform.y = y;
    transform.scale = scale;
    transform.angle = angle;

    auto& velocity = m_World.AddComponent<ecs::VelocityComponent>(entity);
    velocity.vx = vx;
    velocity.vy = vy;
    velocity.angularVelocity = angularVelocity;

    m_World.AddComponent<ecs::TriangleRenderComponent>(entity);
    m_World.AddComponent<ecs::BoundsBounceComponent>(entity);

    Logger::Get().Info("ECS runtime: spawned demo entity -> " + m_World.DebugDescribeEntity(entity));
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
        const auto* transform = m_World.GetComponent<ecs::TransformComponent>(entity);
        const auto* velocity = m_World.GetComponent<ecs::VelocityComponent>(entity);
        if (transform == nullptr || velocity == nullptr)
            continue;

        ss << " | e" << i
           << " pos=(" << transform->x << ", " << transform->y << ")"
           << " scale=" << transform->scale
           << " angle=" << transform->angle
           << " vel=(" << velocity->vx << ", " << velocity->vy << ")";
    }
    Logger::Get().Info(ss.str());
}

void Application::RenderEcs(IRenderAdapter& renderer)
{
    m_World.ForEach<ecs::TransformComponent, ecs::TriangleRenderComponent>(
        [&](ecs::Entity, ecs::TransformComponent& transform, ecs::TriangleRenderComponent& renderable)
        {
            if (!renderable.visible)
                return;

            float mvp[16];
            BuildMVP(mvp, transform.x, transform.y, transform.scale, transform.angle);
            renderer.SetTestTransform(mvp);
            renderer.DrawTestTriangle();
        });
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

        float mvp[16];
        BuildMVP(mvp, m_Obj.x, m_Obj.y, m_Obj.scale, m_Obj.angle);

        for (auto& wc : m_Windows)
        {
            if (!wc.window || wc.window->ShouldClose()) continue;

            wc.renderer->SetTestTransform(mvp);

            wc.renderer->BeginFrame();
            wc.renderer->Clear(wc.clear[0], wc.clear[1], wc.clear[2], wc.clear[3]);

            m_StateMachine.Render(*this, *wc.renderer);
            wc.renderer->DrawTestTriangle();
            RenderEcs(*wc.renderer);

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


void Application::UpdateInputAndTransform(IWindow* srcWindow, float dt)
{
    auto* gw = dynamic_cast<GlfwWindow*>(srcWindow);
    if (!gw) return;

    GLFWwindow* w = gw->GetGlfwHandle();
    if (!w) return;

    const float moveSpeed = 0.8f;    
    const float rotSpeed = 2.0f;    
    const float scaleStep = 1.15f;    
    const float smooth = 12.0f;   

    if (glfwGetKey(w, GLFW_KEY_LEFT) == GLFW_PRESS) m_Obj.x -= moveSpeed * dt;
    if (glfwGetKey(w, GLFW_KEY_RIGHT) == GLFW_PRESS) m_Obj.x += moveSpeed * dt;
    if (glfwGetKey(w, GLFW_KEY_UP) == GLFW_PRESS) m_Obj.y += moveSpeed * dt;
    if (glfwGetKey(w, GLFW_KEY_DOWN) == GLFW_PRESS) m_Obj.y -= moveSpeed * dt;

    bool lmb = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool rmb = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

    if (lmb && !m_Obj.prevLMB)
    {
        m_Obj.targetScale *= scaleStep;
        if (m_Obj.targetScale > 3.0f) m_Obj.targetScale = 1.0f;
    }

    if (rmb)
    {
        m_Obj.targetAngle += rotSpeed * dt;
    }

    m_Obj.prevLMB = lmb;
    m_Obj.prevRMB = rmb;

    float a = SmoothFactor(smooth, dt);
    m_Obj.scale = m_Obj.scale + (m_Obj.targetScale - m_Obj.scale) * a;
    m_Obj.angle = m_Obj.angle + (m_Obj.targetAngle - m_Obj.angle) * a;

    if (m_Obj.x < -0.9f) m_Obj.x = -0.9f;
    if (m_Obj.x > 0.9f) m_Obj.x = 0.9f;
    if (m_Obj.y < -0.9f) m_Obj.y = -0.9f;
    if (m_Obj.y > 0.9f) m_Obj.y = 0.9f;
}

void Application::RequestStateChange(std::unique_ptr<IGameState> s)
{
    m_StateMachine.ChangeState(std::move(s));
}
