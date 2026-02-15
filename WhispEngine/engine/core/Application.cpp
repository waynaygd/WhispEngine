#include "Application.h"
#include "Logger.h"

#include "../platform/GlfwWindow.h"
#include "../render/IRenderAdapter.h"
#include <GLFW/glfw3.h>

#include "../game/states/LoadingState.h"

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


bool Application::Initialize()
{
    Logger::Get().Initialize("engine.log");
    Logger::Get().Info("Application Initialize");

    m_Time.Initialize();

    m_Window = std::make_unique<GlfwWindow>();
    if (!m_Window->Create(1280, 720, "WhispEngine"))
        return false;

    m_Renderer = RenderFactory::Create(m_Backend);
    if (!m_Renderer)
        return false;

    if (!m_Renderer->Initialize(m_Window.get()))
        return false;

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

    while (m_IsRunning && !m_Window->ShouldClose())
    {
        m_Window->PollEvents();
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

            char title[256];
            snprintf(title, sizeof(title),
                "WhispEngine | FPS: %.1f | dt: %.3f ms",
                fps,
                dt * 1000.0f);

            m_Window->SetTitle(title);

            std::ostringstream ss;
            ss << "FPS=" << fps
                << " dt(ms)=" << dt * 1000.0f;

            Logger::Get().Info(ss.str());

            fpsTimer = 0.0f;
            fpsFrames = 0;
        }

        float mvp[16];
        BuildMVP(mvp, m_Obj.x, m_Obj.y, m_Obj.scale, m_Obj.angle);
        m_Renderer->SetTestTransform(mvp);

        m_Renderer->BeginFrame();
        m_Renderer->Clear(0.08f, 0.08f, 0.12f, 1.0f);
        m_StateMachine.Render(*this, *m_Renderer);
        m_Renderer->DrawTestTriangle();
        m_Renderer->EndFrame();
        m_Renderer->Present();
    }

    return 0;
}

void Application::Shutdown()
{
    if (m_Renderer) m_Renderer->Shutdown();
    Logger::Get().Shutdown();
}

void Application::UpdateInputAndTransform(float dt)
{
    auto* gw = static_cast<GlfwWindow*>(m_Window.get());
    GLFWwindow* w = gw->GetGlfwHandle();

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
