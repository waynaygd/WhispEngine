#include "Application.h"
#include "Logger.h"

#include "../platform/GlfwWindow.h"
#include "../render/IRenderAdapter.h"

Application::Application() = default;
Application::~Application() = default;

bool Application::Initialize()
{
    Logger::Get().Initialize("engine.log");
    Logger::Get().Info("Application Initialize");

    m_Time.Initialize();

    m_Window = std::make_unique<GlfwWindow>();
    if (!m_Window->Create(1280, 720, "WhispEngine"))
        return false;

    // пока временно выберем Null/DX12/Vulkan
    // позже заменим на конфиг/аргумент
    RenderBackend backend = RenderBackend::Null;

#if defined(ENABLE_DX12)
    backend = RenderBackend::DX12;
#elif defined(ENABLE_VULKAN)
    backend = RenderBackend::Vulkan;
#endif

    m_Renderer = RenderFactory::Create(backend);
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
