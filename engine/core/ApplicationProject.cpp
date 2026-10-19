#include "Application.h"
#include "AssetPaths.h"
#include "../scene/SceneSerializer.h"
#include "../resources/ResourceManager.h"
#include "../platform/GlfwWindow.h"
#include <GLFW/glfw3.h>

bool Application::OpenProject(const std::filesystem::path& path,ProjectDecision decision,std::string* error){
    if(decision==ProjectDecision::Cancel)return false;
    try {
        auto next=ProjectManager::Prepare(path);
        ecs::World staged;SceneSettings settings;
        if(!SceneSerializer::LoadWorld(next->ResolveScene(next->startupScene),staged,settings,error))return false;
        return CommitProject(std::move(next),staged,settings,decision,error);
    }catch(const std::exception& e){if(error)*error=std::string("Open project: ")+e.what();return false;}
}
bool Application::CloseProject(ProjectDecision decision,std::string* error){
    if(decision==ProjectDecision::Cancel)return false;
    ecs::World empty;return CommitProject({},empty,SceneSettings{},decision,error);
}
bool Application::OpenLegacyWorkspace(std::string* error){
    if(!IsProjectBrowserActive()){
        if(error)*error="Choose Legacy Workspace from the startup screen";
        return false;
    }
    // OpenScene stages disk data before replacing the current (empty) workspace.
    if(!m_LegacyStartupScene.empty()){
        if(!OpenScene(m_LegacyStartupScene,error))return false;
    }else SetupEcsRuntimeDemo();
    m_ProjectBrowserActive=false;
    InitializeConfigHotReload();
    m_EditorLayer.ProjectOpened(*this);
    if(error)error->clear();
    return true;
}
bool Application::CommitProject(std::shared_ptr<const ProjectContext> next,ecs::World& staged,const SceneSettings& settings,ProjectDecision decision,std::string* error){
    // Descriptor and startup scene are staged before changing the current state.
    m_World.AssertOwnerThread();GetWorld().AssertOwnerThread();
    if(m_World.IsIterating()||GetWorld().IsIterating()){if(error)*error="Project changes require an ECS phase boundary";return false;}
    if(!m_EditorLayer.ResolveProjectEdits(*this,static_cast<int>(decision),error))return false;
    SetEditorPlayMode(false);m_StateMachine.Clear(*this);m_LuaRuntime.Stop();ReleasePlayerMouse();
    m_PlayerAltWasDown=false;m_ProjectileInput={};m_InputManager.ResetMouseDelta();
    if(auto* native=dynamic_cast<GlfwWindow*>(GetWindow()))if(auto* window=native->GetGlfwHandle()){
        glfwSetInputMode(window,GLFW_CURSOR,GLFW_CURSOR_NORMAL);
        if(glfwRawMouseMotionSupported())glfwSetInputMode(window,GLFW_RAW_MOUSE_MOTION,GLFW_FALSE);
    }
    m_Camera.controlsActive=false;m_Camera.previousRightMouseDown=true;
    if(m_ResourceManager)m_ResourceManager->DrainProjectLoads();
    m_EditorLayer.ResetProjectState(GetRenderAdapter());
    if(m_RenderSystem)m_RenderSystem->ReleaseGpuResources();
    if(m_ResourceManager)m_ResourceManager->ClearAll();
    m_Mutations.Clear();if(m_PhysicsSystem)m_PhysicsSystem->ResetState();ResetPhysicsClock();
    m_PlayerEntity={};m_GameplayCamera={};m_CloneMapping.clear();m_ActiveCollisionPairs.clear();
    ProjectManager::Activate(std::move(next));
    SetGpuInstancingEnabled(GetProject()?GetProject()->gpuInstancing:true);
    m_World.ReplaceData(staged);m_EcsDebugEntities=m_World.Entities();m_StressEntities.clear();
    m_EditDebugEntities.clear();m_EditStressEntities.clear();
    SetSceneSettings(settings);auto project=GetProject();
    m_ProjectBrowserActive=!project;
    m_SceneKey=project?project->startupScene:std::string();m_Config.ecsDemo.sceneFile=m_SceneKey;
    m_HasConfigWatch=m_HasSceneWatch=false;m_SceneWatchPath.clear();
    m_EditorLayer.ProjectOpened(*this);
    if(error)error->clear();return true;
}
bool Application::SetStartupScene(const std::string& key,std::string* error){
    auto project=GetProject();if(!project){if(error)*error="No project is open";return false;}
    auto settings=project->Settings();settings.startupScene=key;return SaveProjectSettings(settings,error);
}
bool Application::SaveProjectSettings(const ProjectSettings& values,std::string* error){
    try{
        if(IsEditorPlayMode())throw std::runtime_error("Stop Play before saving Project Settings");
        auto project=GetProject();if(!project)throw std::runtime_error("No project is open");
        ecs::World check;SceneSettings settings;
        if(!SceneSerializer::LoadWorld(project->ResolveScene(values.startupScene),check,settings,error)){if(error)*error="Startup Scene: "+*error;return false;}
        auto next=ProjectManager::WithSettings(*project,values);ProjectManager::Activate(std::move(next));SetGpuInstancingEnabled(values.gpuInstancing);
        m_EditorLayer.ProjectSettingsSaved(*this);
        if(error)error->clear();return true;
    }catch(const std::exception& e){if(error)*error=e.what();return false;}
}

bool Application::SaveAuthoringSceneForTransition(std::string* error){
    if(!IsEditorPlayMode())return SaveCurrentScene(error);
    // Save the editor snapshot, never runtime entities or gameplay settings.
    auto settings=GetSceneSettings();settings.lighting=m_EditLighting;settings.environment=m_EditEnvironment;
    settings.shadows=m_EditShadows;settings.background=m_EditBackground;settings.antiAliasing=m_EditAntiAliasing;
    settings.primaryDirectional=SceneSerializer::PrimaryDirectionalId(m_World);
    try{
        const auto key=m_SceneKey.empty()?DefaultSceneKey():m_SceneKey;
        const auto path=GetProject()?GetProject()->ResolveScene(key):editor::AssetImport::SafeTarget(AssetPaths::ResolveAssetRoot(),key);
        if(!SceneSerializer::WriteAtomic(path,SceneSerializer::CaptureWorld(m_World,settings),error,!m_SceneKey.empty()))return false;
        m_SceneKey=key;m_EditorLayer.MarkSaved();return true;
    }catch(const std::exception& e){if(error)*error=e.what();return false;}
}
