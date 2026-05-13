#pragma once
#include <filesystem>
#include <memory>
#include <vector>
#include <string>

#include "ConfigLoader.h"
#include "Time.h"
#include "../ecs/World.h"
#include "../ecs/systems/RenderSystem.h"
#include "../render/RenderFactory.h"
#include "../platform/IWindow.h"
#include "../game/StateMachine.h"
#include "../ecs/events/EventBus.h"
#include "../platform/InputManager.h"

class IWindow;
class IRenderAdapter;
class IGameState;
class ResourceManager;

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
    ResourceManager* GetResourceManager() { return m_ResourceManager.get(); }
    const ResourceManager* GetResourceManager() const { return m_ResourceManager.get(); }
    void EnterGameplayScene();
    void ExitGameplayScene();
    ecs::Entity SpawnGameplayEntity();
    ecs::Entity SpawnPhysicsProjectile();
    bool DestroyLastGameplayEntity();
    std::size_t GetGameplayEntityCount() const { return m_EcsDebugEntities.size(); }
    bool IsCameraControlActive() const { return m_Camera.controlsActive; }
    void ToggleDebugColliders();
    bool IsInputActionActive(const std::string& action) const;

    void RequestStateChange(std::unique_ptr<IGameState> s);


private:
    static std::vector<EcsDemoEntityConfig> BuildDefaultEcsDemoEntities();
    void RunEcsBootstrapCheck();
    void RunResourceBootstrapCheck();
    void PreloadSceneResourcesAsync(const std::vector<EcsDemoEntityConfig>& entities);
    void SetupEcsRuntimeDemo();
    void InitializeConfigHotReload();
    void PollConfigHotReload();
    bool ReloadSceneFromCurrentConfig(const char* reason);
    ecs::Entity SpawnEcsDemoEntity(const EcsDemoEntityConfig& entityCfg);
    void UpdateEcs(float dt);
    void UpdateCameraController(float dt);
    void UpdateRenderSystemCamera(IWindow* window);

    struct WindowContext
    {
        std::unique_ptr<IWindow> window;
        std::unique_ptr<IRenderAdapter> renderer;

        RenderBackend backend = RenderBackend::DX12;

        std::string baseTitle;  
        float clear[4] = { 0.08f, 0.08f, 0.12f, 1.0f };
    };

    struct CameraControllerState
    {
        ecs::Vec3 position{ 0.0f, 0.0f, -2.25f };
        float yaw = 0.0f;
        float pitch = 0.0f;
        float verticalFovRadians = 1.04719755f;
        float nearPlane = 0.01f;
        float farPlane = 100.0f;
        float moveSpeed = 1.8f;
        float minMoveSpeed = 0.2f;
        float maxMoveSpeed = 25.0f;
        float boostMultiplier = 3.0f;
        float scrollSpeedStepMultiplier = 1.2f;
        float mouseSensitivity = 0.0025f;
        bool controlsActive = false;
        bool previousRightMouseDown = false;
        double cursorXBeforeCapture = 0.0;
        double cursorYBeforeCapture = 0.0;
        double lastMouseX = 0.0;
        double lastMouseY = 0.0;
    };

    std::vector<WindowContext> m_Windows;
    AppConfig m_Config;
    std::unique_ptr<ResourceManager> m_ResourceManager;

    ecs::World m_World;
    ecs::RenderSystem* m_RenderSystem = nullptr;
    std::vector<ecs::Entity> m_EcsDebugEntities;
    float m_EcsDebugLogTimer = 0.0f;
    std::filesystem::path m_ConfigWatchPath;
    std::filesystem::path m_SceneWatchPath;
    std::filesystem::file_time_type m_ConfigWriteTime{};
    std::filesystem::file_time_type m_SceneWriteTime{};
    bool m_HasConfigWatch = false;
    bool m_HasSceneWatch = false;

    Time m_Time;
    StateMachine m_StateMachine;
    CameraControllerState m_Camera;
    bool m_DebugCollidersEnabled = false;
    ecs::EventBus m_EventBus;
    InputManager m_InputManager;

    UpdateMode m_UpdateMode = UpdateMode::Variable;

    bool m_IsRunning = false;
};
