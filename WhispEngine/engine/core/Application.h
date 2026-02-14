#pragma once
#include <memory>
#include "Time.h"
#include "../render/RenderFactory.h"
#include "../platform/IWindow.h"
#include "../game/StateMachine.h"

class IWindow;
class IRenderAdapter;

class Application
{
public:
    Application();
    ~Application();        // объявили, но НЕ определяем тут

    bool Initialize();  
    int Run();
    void Shutdown();

private:
    std::unique_ptr<IWindow> m_Window;
    std::unique_ptr<IRenderAdapter> m_Renderer;

    Time m_Time;
    StateMachine m_StateMachine;

    bool m_IsRunning = false;
};
