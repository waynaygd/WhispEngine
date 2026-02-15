#pragma once
#include <memory>
#include "Time.h"
#include "../render/RenderFactory.h"
#include "../platform/IWindow.h"
#include "../game/StateMachine.h"

class IWindow;
class IRenderAdapter;

class IGameState;
class IWindow;

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

    void SetBackend(RenderBackend b) { m_Backend = b; }

    bool Initialize();  
    int Run();
    void Shutdown();

    void UpdateInputAndTransform(float dt);
    void SetUpdateMode(UpdateMode m) { m_UpdateMode = m; }

    IWindow* GetWindow() { return m_Window.get(); }

    void RequestStateChange(std::unique_ptr<IGameState> s);


private:
    TransformState m_Obj;

    std::unique_ptr<IWindow> m_Window;
    std::unique_ptr<IRenderAdapter> m_Renderer;

    Time m_Time;
    StateMachine m_StateMachine;

    RenderBackend m_Backend = RenderBackend::DX12;
    UpdateMode m_UpdateMode = UpdateMode::Variable;

    bool m_IsRunning = false;
};
