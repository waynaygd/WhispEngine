#pragma once
#include <memory>
#include <vector>
#include <string>

#include "ConfigLoader.h"
#include "Time.h"
#include "../ecs/World.h"
#include "../ecs/systems/RenderSystem.h"
#include "../ecs/systems/SystemPipeline.h"
#include "../render/RenderFactory.h"
#include "../platform/IWindow.h"
#include "../game/StateMachine.h"

class IWindow;
class IRenderAdapter;
class IGameState;

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
    void SetUpdateMode(UpdateMode m) { m_UpdateMode = m; }

    IWindow* GetWindow() { return m_Windows.empty() ? nullptr : m_Windows[0].window.get(); }
    ecs::World& GetWorld() { return m_World; }
    const ecs::World& GetWorld() const { return m_World; }
    void EnterGameplayScene();
    void ExitGameplayScene();
    ecs::Entity SpawnGameplayEntity();
    bool DestroyLastGameplayEntity();
    std::size_t GetGameplayEntityCount() const { return m_EcsDebugEntities.size(); }

    void RequestStateChange(std::unique_ptr<IGameState> s);


private:
    static std::vector<EcsDemoEntityConfig> BuildDefaultEcsDemoEntities();
    void RunEcsBootstrapCheck();
    void SetupEcsRuntimeDemo();
    ecs::Entity SpawnEcsDemoEntity(float x, float y, float scale, float angle, float vx, float vy, float angularVelocity);
    void UpdateEcs(float dt);

    struct WindowContext
    {
        std::unique_ptr<IWindow> window;
        std::unique_ptr<IRenderAdapter> renderer;

        RenderBackend backend = RenderBackend::DX12;

        std::string baseTitle;  
        float clear[4] = { 0.08f, 0.08f, 0.12f, 1.0f };
    };

    std::vector<WindowContext> m_Windows;
    AppConfig m_Config;

    ecs::World m_World;
    ecs::RenderSystem m_RenderSystem;
    ecs::SystemPipeline m_EcsSystems;
    std::vector<ecs::Entity> m_EcsDebugEntities;
    float m_EcsDebugLogTimer = 0.0f;

    Time m_Time;
    StateMachine m_StateMachine;

    UpdateMode m_UpdateMode = UpdateMode::Variable;

    bool m_IsRunning = false;
};
