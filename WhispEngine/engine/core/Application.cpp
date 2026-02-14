#include "Application.h"
#include "Application.h"
#include "Logger.h"

#include "../platform/GlfwWindow.h"
#include "../render/IRenderAdapter.h"
#include <GLFW/glfw3.h>

Application::Application() = default;
Application::~Application() = default;

#include <cmath>

static float SmoothFactor(float smooth, float dt)
{
    // alpha = 1 - exp(-k*dt)
    return 1.0f - std::exp(-smooth * dt);
}

// column-major 4x4
static void BuildMVP(float* out16, float x, float y, float s, float a)
{
    float c = std::cos(a);
    float sn = std::sin(a);

    // 2D transform in NDC
    // | s*c  -s*sn  0  x |
    // | s*sn  s*c   0  y |
    // | 0      0    1  0 |
    // | 0      0    0  1 |
    //
    // column-major array:
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
    return true;
}

int Application::Run()
{
    while (m_IsRunning && !m_Window->ShouldClose())
    {
        m_Window->PollEvents();
        float dt = m_Time.Tick();

        // NEW: input -> transform (uses dt)
        UpdateInputAndTransform(dt);

        // NEW: build matrix and send to renderer
        float mvp[16];
        BuildMVP(mvp, m_Obj.x, m_Obj.y, m_Obj.scale, m_Obj.angle);
        m_Renderer->SetTestTransform(mvp);

        m_StateMachine.Update(dt);

        m_Renderer->BeginFrame();
        m_Renderer->Clear(0.08f, 0.08f, 0.12f, 1.0f);
        m_StateMachine.Render(*m_Renderer);
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

    const float moveSpeed = 0.8f;      // NDC units per second
    const float rotSpeed = 2.0f;       // rad/s when holding RMB
    const float scaleStep = 1.15f;     // per click LMB
    const float smooth = 12.0f;        // smoothing strength

    // arrows -> move
    if (glfwGetKey(w, GLFW_KEY_LEFT) == GLFW_PRESS) m_Obj.x -= moveSpeed * dt;
    if (glfwGetKey(w, GLFW_KEY_RIGHT) == GLFW_PRESS) m_Obj.x += moveSpeed * dt;
    if (glfwGetKey(w, GLFW_KEY_UP) == GLFW_PRESS) m_Obj.y += moveSpeed * dt;
    if (glfwGetKey(w, GLFW_KEY_DOWN) == GLFW_PRESS) m_Obj.y -= moveSpeed * dt;

    // mouse buttons
    bool lmb = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool rmb = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

    // LMB click (edge) -> increase targetScale
    if (lmb && !m_Obj.prevLMB)
    {
        m_Obj.targetScale *= scaleStep;
        if (m_Obj.targetScale > 3.0f) m_Obj.targetScale = 1.0f; // чтобы не улететь
    }

    // RMB hold -> rotate smoothly (target angle moves)
    if (rmb)
    {
        m_Obj.targetAngle += rotSpeed * dt;
    }

    m_Obj.prevLMB = lmb;
    m_Obj.prevRMB = rmb;

    // smooth converge
    float a = SmoothFactor(smooth, dt);
    m_Obj.scale = m_Obj.scale + (m_Obj.targetScale - m_Obj.scale) * a;
    m_Obj.angle = m_Obj.angle + (m_Obj.targetAngle - m_Obj.angle) * a;

    // clamp position so triangle doesn't fully disappear (optional)
    if (m_Obj.x < -0.9f) m_Obj.x = -0.9f;
    if (m_Obj.x > 0.9f) m_Obj.x = 0.9f;
    if (m_Obj.y < -0.9f) m_Obj.y = -0.9f;
    if (m_Obj.y > 0.9f) m_Obj.y = 0.9f;
}
