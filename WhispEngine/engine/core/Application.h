#pragma once
#include <memory>
#include "Time.h"
#include "../render/RenderFactory.h"
#include "../platform/IWindow.h"
#include "../game/StateMachine.h"

class IWindow;
class IRenderAdapter;

struct TransformState
{
    float x = 0.0f;
    float y = 0.0f;
    float scale = 1.0f;
    float angle = 0.0f;       

    float targetScale = 1.0f;
    float targetAngle = 0.0f;

    bool prevLMB = false;
    bool prevRMB = false;
};

class Application
{
public:
    Application();
    ~Application();       

    void SetBackend(RenderBackend b) { m_Backend = b; }

    bool Initialize();  
    int Run();
    void Shutdown();

    void UpdateInputAndTransform(float dt);

private:
    TransformState m_Obj;

    RenderBackend m_Backend = RenderBackend::DX12;
    std::unique_ptr<IWindow> m_Window;
    std::unique_ptr<IRenderAdapter> m_Renderer;

    Time m_Time;
    StateMachine m_StateMachine;

    bool m_IsRunning = false;
};
