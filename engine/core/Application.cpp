#include "Application.h"
#include "Logger.h"
#include "ConfigLoader.h"

#include "../platform/GlfwWindow.h"
#include "../render/IRenderAdapter.h"
#include <GLFW/glfw3.h>

#include "../game/states/LoadingState.h"

#include <filesystem>
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

void Application::RunEcsBootstrapCheck()
{
    Logger::Get().Info("ECS bootstrap: starting entity lifecycle self-check");

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
    Logger::Get().Info("ECS bootstrap: alive entities=" + std::to_string(m_World.GetAliveCount()) +
        ", capacity=" + std::to_string(m_World.GetCapacity()));
}

bool Application::Initialize()
{
    Logger::Get().Initialize("engine.log");
    Logger::Get().Info("Application Initialize");
    RunEcsBootstrapCheck();

    m_Time.Initialize();

    AppConfig cfg;
    const std::string configPath = ResolveConfigPath();
    Logger::Get().Info("Application: using config file: " + configPath);

    const bool configLoaded = ConfigLoader::Load(configPath, cfg);

    WindowConfig selectedWindow = BuildDefaultWindowConfig(cfg.activeBackend);
    bool foundWindowConfig = false;

    if (!configLoaded)
    {
        Logger::Get().Error("Application: failed to load config. Using default single-window setup.");
    }
    else
    {
        for (const auto& wcfg : cfg.windows)
        {
            if (wcfg.backend == cfg.activeBackend)
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
                BackendToString(cfg.activeBackend) +
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
    m_World.Clear();
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
