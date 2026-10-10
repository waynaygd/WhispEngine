#pragma once
#include <filesystem>
#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_set>

#include "ConfigLoader.h"
#include "Time.h"
#include "FixedStepClock.h"
#include "PhysicsFrameStatistics.h"
#include "../ecs/World.h"
#include "../ecs/systems/PhysicsSystem.h"
#include "../ecs/systems/RenderSystem.h"
#include "../render/RenderFactory.h"
#include "../platform/IWindow.h"
#include "../game/StateMachine.h"
#include "../ecs/events/EventBus.h"
#include "../platform/InputManager.h"
#include "../editor/EditorLayer.h"

class IWindow;
class IRenderAdapter;
class IGameState;
class ResourceManager;
class JobSystem;
namespace ecs { class PhysicsSystem; }

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
    void SetUpdateMode(UpdateMode m) {
        if (m_UpdateMode != m) { m_UpdateMode = m; ResetPhysicsClock(); }
    }

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
    std::size_t GetActiveCollisionCount() const { return m_ActiveCollisionPairs.size(); }
    bool IsCameraControlActive() const { return m_Camera.controlsActive; }
    const ecs::Vec3& GetCameraPosition() const { return m_Camera.position; }
    float GetCameraYaw() const { return m_Camera.yaw; }
    float GetCameraPitch() const { return m_Camera.pitch; }
    float GetCameraVerticalFovRadians() const { return m_Camera.verticalFovRadians; }
    float GetCameraNearPlane() const { return m_Camera.nearPlane; }
    float GetCameraFarPlane() const { return m_Camera.farPlane; }
    bool IsEditorPlayMode() const { return m_EditorPlayMode; }
    void SetEditorPlayMode(bool enabled);
    void SetupPhysicsStressScene(int count = 500);
    void ClearPhysicsStressScene();
    std::size_t GetStressEntityCount() const { return m_StressEntities.size(); }
    ecs::PhysicsSystem* GetPhysicsSystem() { return m_PhysicsSystem; }
    bool IsGpuInstancingEnabled() const { return m_GpuInstancing; }
    void SetGpuInstancingEnabled(bool enabled) { m_GpuInstancing=enabled;if(m_RenderSystem)m_RenderSystem->SetInstancingEnabled(enabled); }
    const ecs::RenderSystem::Statistics& GetRenderStatistics() const { return m_RenderSystem->GetStatistics(); }
    void SetRenderBenchmarkOutput(std::string path) { m_RenderDiagnosticOutput=std::move(path);m_RenderBenchmark=true;m_FrameLimit=1300; }
    float GetFrameDeltaTime() const { return m_Time.GetFrameDeltaTime(); }
    const PhysicsFrameStatistics& GetPhysicsFrameStatistics() const { return m_PhysicsFrame; }
    ecs::RenderInterpolation& GetRenderInterpolation() { return m_RenderInterpolation; }
    void NotifyTeleport(ecs::Entity entity) { m_RenderInterpolation.ResetEntity(m_World, entity); }
    bool IsPhysicsDebugPose() const { return m_PhysicsDebugPose; }
    void SetPhysicsDebugPose(bool enabled) { m_PhysicsDebugPose = enabled; if(m_RenderSystem) m_RenderSystem->SetPhysicsDebugPose(enabled); }
    void SetFrameLimit(std::uint32_t frames) { m_FrameLimit = frames; }
    void SetDiagnosticOutput(std::string path, bool lifecycle = false) {
        m_DiagnosticOutput = std::move(path); m_DiagnosticLifecycle = lifecycle;
    }
    void WaitForTracyConnection() { m_WaitForTracy = true; }
    void SetDiagnosticSlowFrame(unsigned frame, unsigned milliseconds) {
        m_SlowFrame = frame; m_SlowFrameMilliseconds = milliseconds;
    }
    void ToggleDebugColliders();
    bool IsInputActionActive(const std::string& action) const;
    bool SaveCurrentScene(std::string* outError = nullptr);
    bool LoadCurrentScene(std::string* outError = nullptr);

    void RequestStateChange(std::unique_ptr<IGameState> s);


private:
    void ResetPhysicsClock() {
        m_FixedClock.Reset(); m_FixedSimulationSeconds = 0;
        m_ResetPhysicsDelta = true;
        m_RenderInterpolation.Clear();
    }
    static std::vector<EcsDemoEntityConfig> BuildDefaultEcsDemoEntities();
    void RunEcsBootstrapCheck();
    void RunResourceBootstrapCheck();
    void PreloadSceneResourcesAsync(const std::vector<EcsDemoEntityConfig>& entities);
    void SetupEcsRuntimeDemo();
    void InitializeConfigHotReload();
    void PollConfigHotReload();
    bool ReloadSceneFromCurrentConfig(const char* reason);
    void ConfigureInputBindings();
    ecs::Entity SpawnEcsDemoEntity(const EcsDemoEntityConfig& entityCfg);
    void UpdateEcs(float dt);
    void UpdateCameraController(float dt);
    void UpdateRenderSystemCamera(IWindow* window);
    void UpdateRenderSystemCameraAspect(float aspectRatio);
    void SetupRenderStressScene();
    void RunAsyncResourceStressTest();
    void RunSyncResourceStressTest();
    bool m_AsyncResourceStressStarted = false;

    struct WindowContext
    {
        std::unique_ptr<IWindow> window;
        std::unique_ptr<IRenderAdapter> renderer;

        RenderBackend backend = RenderBackend::DX12;

        std::string baseTitle;  
        float clear[4] = { 0.08f, 0.08f, 0.12f, 1.0f };
        bool editorUiAvailable = false;
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
    std::unique_ptr<JobSystem> m_JobSystem;

    ecs::World m_World;
    ecs::PhysicsSystem* m_PhysicsSystem = nullptr;
    ecs::RenderSystem* m_RenderSystem = nullptr;
    ecs::RenderInterpolation m_RenderInterpolation;
    std::vector<ecs::Entity> m_EcsDebugEntities;
    std::vector<ecs::Entity> m_StressEntities;
    float m_EcsDebugLogTimer = 0.0f;
    std::filesystem::path m_ConfigWatchPath;
    std::filesystem::path m_SceneWatchPath;
    std::filesystem::file_time_type m_ConfigWriteTime{};
    std::filesystem::file_time_type m_SceneWriteTime{};
    bool m_HasConfigWatch = false;
    bool m_HasSceneWatch = false;

    Time m_Time;
    FixedStepClock m_FixedClock;
    PhysicsFrameStatistics m_PhysicsFrame;
    double m_FixedSimulationSeconds = 0;
    bool m_ResetPhysicsDelta = true;
    StateMachine m_StateMachine;
    CameraControllerState m_Camera;
    bool m_DebugCollidersEnabled = false;
    bool m_PhysicsDebugPose = false;
    bool m_GpuInstancing = true;
    bool m_EditorPlayMode = false;
    ecs::EventBus m_EventBus;
    InputManager m_InputManager;
    editor::EditorLayer m_EditorLayer;
    std::unordered_set<std::uint64_t> m_ActiveCollisionPairs;

    UpdateMode m_UpdateMode = UpdateMode::Variable;

    bool m_IsRunning = false;
    std::uint32_t m_FrameLimit = 0;
    std::string m_DiagnosticOutput;
    std::string m_RenderDiagnosticOutput;
    bool m_RenderBenchmark=false;
    bool m_DiagnosticLifecycle = false;
    bool m_WaitForTracy = false;
    unsigned m_SlowFrame = UINT32_MAX, m_SlowFrameMilliseconds = 0;
};
