#pragma once
#include <memory>
#include <vector>
#include <string>

#include "Time.h"
#include "../render/RenderFactory.h"
#include "../platform/IWindow.h"
#include "../game/StateMachine.h"

class IWindow;
class IRenderAdapter;
class IGameState;

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

enum class UpdateMode { 
    Variable, 
    Fixed 
};

class Application
{
public:
    Application();
    ~Application();       

    bool Initialize();  
    int Run();
    void Shutdown();

    void UpdateInputAndTransform(IWindow* srcWindow, float dt);
    void SetUpdateMode(UpdateMode m) { m_UpdateMode = m; }

    IWindow* GetWindow() { return m_Windows.empty() ? nullptr : m_Windows[0].window.get(); }

    void RequestStateChange(std::unique_ptr<IGameState> s);


private:
    TransformState m_Obj;

    struct WindowContext
    {
        std::unique_ptr<IWindow> window;
        std::unique_ptr<IRenderAdapter> renderer;

        RenderBackend backend = RenderBackend::DX12;

        std::string baseTitle;  
        float clear[4] = { 0.08f, 0.08f, 0.12f, 1.0f };
    };

    std::vector<WindowContext> m_Windows;

    Time m_Time;
    StateMachine m_StateMachine;

    UpdateMode m_UpdateMode = UpdateMode::Variable;

    bool m_IsRunning = false;
};
