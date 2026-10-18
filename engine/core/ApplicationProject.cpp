#include "Application.h"
#include "AssetPaths.h"
#include "../scene/SceneSerializer.h"
#include "../resources/ResourceManager.h"
#include "../platform/GlfwWindow.h"
#include <GLFW/glfw3.h>

bool Application::OpenProject(const std::filesystem::path& path,ProjectDecision decision,std::string* error){
    return OpenProjectInternal(path,decision,false,error);
}
bool Application::OpenProjectInternal(const std::filesystem::path& path,ProjectDecision decision,bool allowHierarchy,std::string* error){
    if(decision==ProjectDecision::Cancel)return false;
    try {
        auto next=ProjectManager::Prepare(path);
        auto candidate=PrepareAuthoringScene(next->ResolveScene(next->startupScene),error);if(!candidate)return false;
        if(candidate->world->IsHierarchyData()&&!allowHierarchy){if(error)*error="Project preflight: worldMode mismatch: hierarchy Editor is not enabled";return false;}
        candidate->controlledHierarchy=allowHierarchy;
        candidate->sceneKey=next->startupScene;candidate->configSceneKey=candidate->sceneKey;
        return CommitProject(std::move(next),candidate,decision,error);
    }catch(const std::exception& e){if(error)*error=std::string("Open project: ")+e.what();return false;}
}
bool Application::CloseProject(ProjectDecision decision,std::string* error){
    if(decision==ProjectDecision::Cancel)return false;
    auto candidate=PrepareEmptyAuthoring(ecs::World::DataMode::RootOnly,SceneSettings{},error);
    if(!candidate)return false;return CommitProject({},candidate,decision,error);
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
bool Application::CommitProject(std::shared_ptr<const ProjectContext> next,std::unique_ptr<AuthoringCandidate>& candidate,ProjectDecision decision,std::string* error){
    if(!CheckWorldPublication(error))return false;
    if(!candidate){if(error)*error="Project preflight: Missing candidate";return false;}
    if(!ValidateAuthoringCandidate(*candidate,error))return false;
    // Commit begins with explicit edit resolution (Save may write files), then
    // lifecycle/resource cleanup. Failures here are not advertised as rollback.
    try{
    WorldOperationScope committing(m_WorldTransitionDepth);
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
    InstallAuthoringWorld(*candidate);candidate.reset();auto project=GetProject();
    m_ProjectBrowserActive=!project;
    m_HasConfigWatch=m_HasSceneWatch=false;m_SceneWatchPath.clear();
    m_EditorLayer.ProjectOpened(*this);
    if(error)error->clear();return true;
    }catch(const std::exception& e){if(error)*error=std::string("Project commit failed (session may have changed): ")+e.what();return false;}
}
bool Application::SetStartupScene(const std::string& key,std::string* error){
    auto project=GetProject();
    return SaveProjectSettings(key,project?project->settings:ProjectSettings{},error);
}
bool Application::SaveProjectSettings(const std::string& key,const ProjectSettings& settings,std::string* error){
    return SaveProjectSettingsInternal(key,settings,false,error);
}
bool Application::SaveProjectSettingsInternal(const std::string& key,const ProjectSettings& settings,bool allowHierarchy,std::string* error){
    try{
        GetEditorWorld().AssertOwnerThread();
        auto project=GetProject();if(!project)throw std::runtime_error("No project is open");
        // Descriptor validation only: no World publication. This is also called
        // by Save-and-continue inside the already guarded project commit.
        auto check=PrepareAuthoringScene(project->ResolveScene(key),error,false);if(!check)return false;
        if(check->world->IsHierarchyData()&&!allowHierarchy){if(error)*error="Project settings preflight: worldMode mismatch: hierarchy Editor is not enabled";return false;}
        auto next=ProjectManager::WithSettings(*project,key,settings);
        ProjectManager::Activate(std::move(next));
        if(error)error->clear();return true;
    }catch(const std::exception& e){if(error)*error=e.what();return false;}
}
AntiAliasingMode Application::GetEffectiveAntiAliasing()const{
    const auto project=GetProject();
    return project&&!m_AntiAliasingOverride?project->settings.antiAliasing:m_AntiAliasing;
}

bool Application::SaveAuthoringSceneForTransition(std::string* error){
    if(!IsEditorPlayMode())return SaveCurrentScene(error);
    // Save the editor snapshot, never runtime entities or gameplay settings.
    auto settings=GetSceneSettings();settings.lighting=m_EditLighting;settings.environment=m_EditEnvironment;
    settings.shadows=m_EditShadows;settings.background=m_EditBackground;settings.antiAliasing=m_EditAntiAliasing;settings.antiAliasingOverride=m_EditAntiAliasingOverride;
    settings.primaryDirectional=SceneSerializer::PrimaryDirectionalId(GetEditorWorld());
    try{
        const auto key=m_SceneKey.empty()?DefaultSceneKey():m_SceneKey;
        const auto path=GetProject()?GetProject()->ResolveScene(key):editor::AssetImport::SafeTarget(AssetPaths::ResolveAssetRoot(),key);
        if(!SceneSerializer::WriteAtomic(path,SceneSerializer::CaptureWorld(GetEditorWorld(),settings),error,!m_SceneKey.empty()))return false;
        m_SceneKey=key;m_EditorLayer.MarkSaved();return true;
    }catch(const std::exception& e){if(error)*error=e.what();return false;}
}
