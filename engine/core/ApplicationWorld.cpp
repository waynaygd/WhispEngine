#include "Application.h"
#include "../scene/SceneSerializer.h"
#include "../ecs/PlayerSpawn.h"
#include "../ecs/HierarchyPhysics.h"
#include "../ecs/components/MeshRendererComponent.h"
#include "../editor/SunSynchronization.h"
#include <fstream>
#include <type_traits>

namespace {
void Failure(std::string* error,const char* stage,const std::exception& e){if(error)*error=std::string(stage)+e.what();}
}
bool Application::CheckWorldPublication(std::string* error)const{
    try{
        if(m_WorldExecutionDepth||m_WorldTransitionDepth)throw std::runtime_error("World publication during system/lifecycle execution");
        GetEditorWorld().AssertOwnerThread();GetWorld().AssertOwnerThread();
        if(GetEditorWorld().IsIterating()||GetWorld().IsIterating()||GetEditorWorld().IsRetiringHierarchy()||GetWorld().IsRetiringHierarchy())
            throw std::runtime_error("World publication requires an ECS phase boundary");
        if(!GetEditorWorld().CanAttachHierarchyLifecycle(const_cast<scripting::LuaRuntime*>(&m_LuaRuntime)))
            throw std::runtime_error("Authoring World has a foreign Lua lifecycle owner");
        return true;
    }catch(const std::exception& e){Failure(error,"World preflight: ",e);return false;}
}
bool Application::ValidateAuthoringCandidate(AuthoringCandidate& candidate,std::string* error){
    try{
        if(!candidate.world)throw std::runtime_error("Missing candidate World");
        auto& world=*candidate.world;world.AssertOwnerThread();
        if(world.IsIterating()||world.IsRetiringHierarchy()||!world.CanAttachHierarchyLifecycle(nullptr))throw std::runtime_error("Candidate has live iteration/Lua ownership");
        for(auto entity:world.Entities()){
            auto* identity=world.GetComponent<ecs::SceneIdentityComponent>(entity);
            if(!identity||!ecs::ValidSceneIdentity(identity->value))throw std::runtime_error("Candidate requires stable SceneIdentity for every Entity");
            if(world.IsHierarchyData()&&world.HasComponent<ecs::MeshRendererComponent>(entity))
                if(auto* collider=world.GetComponent<ecs::ColliderComponent>(entity);collider&&collider->autoFitFromMesh)
                    throw std::runtime_error("Hierarchy Collider Entity "+identity->value+": requires explicit local geometry; legacy mesh auto-fit is unsupported");
        }
        (void)SceneSettingsFromJson(SceneSettingsToJson(candidate.settings));
        (void)SceneSerializer::CaptureWorld(world,candidate.settings);
        world.RequireRuntimeWorld();
        const auto& background=candidate.settings.background;
        if(background.sunSync==SunSyncMode::SkyDrivesLight||(background.background==EditorBackground::DayNight&&background.driveLightProperties)){
            auto sun=editor::PrimarySun(world);
            if(sun.IsValid()&&!ecs::ReadWorldTransform(world,sun))throw std::runtime_error("Sun synchronization requires a representable world TRS");
            editor::SynchronizeSun(world,background,background.useIblEnvironment?candidate.settings.environment.rotationDegrees:background.skyRotation);
            world.RequireRuntimeWorld();
        }
        candidate.entities=world.Entities();
        return true;
    }catch(const std::exception& e){Failure(error,"World preflight: ",e);return false;}
}
std::unique_ptr<Application::AuthoringCandidate> Application::PrepareAuthoringScene(const std::filesystem::path& path,std::string* error,bool requirePublicationBoundary){
    try{
        if(requirePublicationBoundary&&!CheckWorldPublication(error))return {};
        GetEditorWorld().AssertOwnerThread();
        std::ifstream file(path);if(!file)throw std::runtime_error("Cannot open scene: "+path.string());
        nlohmann::json document;file>>document;ecs::World::DataMode mode;
        std::string diagnostic;
        if(!SceneSerializer::DetermineWorldMode(document,mode,&diagnostic))throw std::runtime_error(diagnostic);
        auto candidate=std::make_unique<AuthoringCandidate>();candidate->world=std::make_unique<ecs::World>(mode);
        if(document.is_object()&&document.value("version",1)==4){
            if(!SceneSerializer::RestoreWorld(document,*candidate->world,candidate->settings,&diagnostic))throw std::runtime_error(diagnostic);
        }else if(!SceneSerializer::LoadWorld(path,*candidate->world,candidate->settings,&diagnostic))throw std::runtime_error(diagnostic);
        if(!ValidateAuthoringCandidate(*candidate,error))return {};
        return candidate;
    }catch(const std::exception& e){Failure(error,"Scene preflight: ",e);return {};}
}
std::unique_ptr<Application::AuthoringCandidate> Application::PrepareEmptyAuthoring(ecs::World::DataMode mode,const SceneSettings& settings,std::string* error){
    try{
        if(!CheckWorldPublication(error))return {};
        auto candidate=std::make_unique<AuthoringCandidate>();candidate->world=std::make_unique<ecs::World>(mode);candidate->settings=settings;
        if(!ValidateAuthoringCandidate(*candidate,error))return {};
        return candidate;
    }catch(const std::exception& e){Failure(error,"World preflight: ",e);return {};}
}
void Application::InstallAuthoringWorld(AuthoringCandidate& candidate)noexcept{
    static_assert(std::is_nothrow_move_assignable_v<EnvironmentSettings> && std::is_nothrow_move_assignable_v<SceneBackground>);
    m_EditorWorld.swap(candidate.world);
    m_ControlledHierarchySession=m_EditorWorld->IsHierarchyData()&&candidate.controlledHierarchy;
    m_EcsDebugEntities=std::move(candidate.entities);m_StressEntities.clear();m_EditDebugEntities.clear();m_EditStressEntities.clear();
    const bool lightingEnabled=m_LightingSettings.enabled;const int shadowDebug=m_ShadowSettings.debugMode;
    m_LightingSettings=candidate.settings.lighting;m_LightingSettings.enabled=lightingEnabled;
    m_EnvironmentSettings=std::move(candidate.settings.environment);m_ShadowSettings=candidate.settings.shadows;m_ShadowSettings.debugMode=shadowDebug;
    m_Background=std::move(candidate.settings.background);m_AntiAliasing=candidate.settings.antiAliasing;m_AntiAliasingOverride=candidate.settings.antiAliasingOverride;
    m_SceneKey=std::move(candidate.sceneKey);m_Config.ecsDemo.sceneFile=std::move(candidate.configSceneKey);
    m_PlayerEntity={};m_GameplayCamera={};m_CloneMapping.clear();m_RenderInterpolation.Clear();m_ActiveCollisionPairs.clear();
    m_HasSceneWatch=false;m_SceneWatchPath.clear();
    if(m_RenderSystem)m_RenderSystem->ResetWorldState();
    if(m_PhysicsSystem)m_PhysicsSystem->ResetState();ResetPhysicsClock();
}
bool Application::NewSceneInternal(ecs::World::DataMode mode,std::string* error){
    SceneSettings settings;settings.antiAliasingOverride=!GetProject();
    auto candidate=PrepareEmptyAuthoring(mode,settings,error);if(!candidate)return false;
    candidate->controlledHierarchy=mode==ecs::World::DataMode::HierarchyData;
    return PublishAuthoringWorld(candidate,error);
}
bool Application::PublishAuthoringWorld(std::unique_ptr<AuthoringCandidate>& candidate,std::string* error){
    if(!CheckWorldPublication(error))return false;
    if(!candidate){if(error)*error="World preflight: Missing candidate";return false;}
    if(!ValidateAuthoringCandidate(*candidate,error))return false;
    // Commit boundary: lifecycle callbacks/Editor cleanup may already have run
    // if a device/allocation exception escapes. No GPU/system owner is replaced.
    try{
        WorldOperationScope committing(m_WorldTransitionDepth);
        SetEditorPlayMode(false);m_LuaRuntime.Stop();m_StateMachine.Clear(*this);
        m_EditorLayer.ResetWorldState();m_Mutations.Clear();InstallAuthoringWorld(*candidate);candidate.reset();return true;
    }catch(const std::exception& e){Failure(error,"World commit failed (session may have changed): ",e);return false;}
}
void Application::UpdateWorldSystems(bool fixed,float dt){
    WorldOperationScope executing(m_WorldExecutionDepth);
    if(fixed)m_SystemWorld.UpdateFixedSystemsOn(GetWorld(),dt);
    else m_SystemWorld.UpdateFrameSystemsOn(GetWorld(),dt);
}
std::unique_ptr<Application::RuntimeCandidate> Application::PrepareRuntimeWorld(std::string* error){
    try{
        if(!CheckWorldPublication(error))return {};
        auto candidate=std::make_unique<RuntimeCandidate>();candidate->world=GetEditorWorld().CloneData(&candidate->mapping);
        if(candidate->world->IsHierarchyData()){
            (void)SceneSettingsFromJson(SceneSettingsToJson(GetSceneSettings()));
            editor::SynchronizeSun(*candidate->world,m_Background,m_Background.useIblEnvironment?m_EnvironmentSettings.rotationDegrees:m_Background.skyRotation);
        }
        candidate->world->RequireRuntimeWorld();
        candidate->player=ecs::SpawnPlayer(*candidate->world,&candidate->spawn);
        candidate->world->RequireRuntimeWorld();
        if(candidate->world->IsHierarchyData()&&!scripting::LuaRuntime::ValidateStartupScripts(*candidate->world,error))return {};
        return candidate;
    }catch(const std::exception& e){Failure(error,"Play preflight: ",e);return {};}
}
std::unique_ptr<Application::AuthoringCandidate> Application::PrepareHierarchyUpgrade(std::string* error){
    try{
        if(!CheckWorldPublication(error))return {};
        auto& source=GetEditorWorld();
        if(source.IsHierarchyData()||IsEditorPlayMode())throw std::runtime_error("Upgrade requires a RootOnly Edit session");
        for(auto e:source.Entities()){
            auto* id=source.GetComponent<ecs::SceneIdentityComponent>(e);
            if(!id||!ecs::ValidSceneIdentity(id->value))throw std::runtime_error("Upgrade requires original persisted SceneIdentity");
            // Diagnose incompatible authored geometry before even graph restore
            // (e.g. zero scale would otherwise fail as a generic graph error).
            if(auto* collider=source.GetComponent<ecs::ColliderComponent>(e)){
                const auto label="Upgrade Entity "+id->value+": Collider ";
                if(!source.HasComponent<ecs::RigidbodyComponent>(e))throw std::runtime_error(label+"without Rigidbody gains immovable solver participation; parity is not proven");
                auto* transform=source.GetComponent<ecs::TransformComponent>(e);
                if(!transform)throw std::runtime_error(label+"requires Transform");
                if(transform->scale.x!=1||transform->scale.y!=1||transform->scale.z!=1)throw std::runtime_error(label+"nonunit scale changes dimensions or violates dynamic/Sphere constraints");
                if(collider->offset.x!=0||collider->offset.y!=0||collider->offset.z!=0)throw std::runtime_error(label+"nonzero world offset is not admitted as a local offset");
            }
        }
        auto document=SceneSerializer::CaptureWorld(source,GetSceneSettings());
        auto original=document;document["worldMode"]="hierarchy";
        auto candidate=std::make_unique<AuthoringCandidate>();candidate->world=std::make_unique<ecs::World>(ecs::World::DataMode::HierarchyData);
        std::string diagnostic;
        if(!SceneSerializer::RestoreWorld(document,*candidate->world,candidate->settings,&diagnostic))throw std::runtime_error(diagnostic);
        const auto graph=ecs::HierarchyGraph::Build(*candidate->world,&diagnostic);if(!graph)throw std::runtime_error(diagnostic);
        auto near=[](double a,double b){return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=2e-5*std::max({1.,std::abs(a),std::abs(b)});};
        auto sameVector=[&](ecs::Vec3 a,ecs::Vec3 b){return near(a.x,b.x)&&near(a.y,b.y)&&near(a.z,b.z);};
        for(auto e:source.Entities()){
            const auto id=SceneSerializer::EntitySceneId(source,e);
            auto entry=std::find_if(graph->nodes.begin(),graph->nodes.end(),[&](const auto& node){return node.id==id;});
            if(entry==graph->nodes.end())throw std::runtime_error("Upgrade Entity "+id+": missing stable identity");
            try{
                if(auto matrix=ecs::ReadWorldMatrix(source,e)){
                    auto next=ecs::ReadWorldMatrix(*candidate->world,entry->entity);if(!next)throw std::runtime_error("world Transform is not representable");
                    for(std::size_t i=0;i<16;++i)if(!near((*matrix)[i],(*next)[i]))throw std::runtime_error("world Transform mismatch");
                }else if(source.HasComponent<ecs::TransformComponent>(e))throw std::runtime_error("invalid root Transform");
                auto* collider=source.GetComponent<ecs::ColliderComponent>(e);if(!collider)continue;
                auto* body=source.GetComponent<ecs::RigidbodyComponent>(e);
                if(!body)throw std::runtime_error("Collider without Rigidbody gains immovable solver participation; parity is not proven");
                auto* transform=source.GetComponent<ecs::TransformComponent>(e);
                if(!transform)throw std::runtime_error("Collider requires Transform");
                // Deliberately exact authored-unit admission. Geometry comparison
                // below allows only floating-point matrix/decomposition noise.
                if(transform->scale.x!=1||transform->scale.y!=1||transform->scale.z!=1)
                    throw std::runtime_error("Collider nonunit scale changes dimensions or violates dynamic/Sphere constraints");
                if(collider->offset.x!=0||collider->offset.y!=0||collider->offset.z!=0)
                    throw std::runtime_error("Collider nonzero world offset is not admitted as a local offset");
                const auto resolved=ecs::ResolveHierarchyCollider(*candidate->world,*graph,entry->entity);
                if(collider->type!=resolved.collider.type||!sameVector(collider->halfExtents,resolved.collider.halfExtents)||
                   !sameVector(transform->position,resolved.transform.position)||!sameVector(collider->offset,resolved.collider.offset))
                    throw std::runtime_error("Collider shape/center/dimensions mismatch");
                auto oldOrientation=*transform,newOrientation=resolved.transform;
                oldOrientation.position=newOrientation.position={};oldOrientation.scale=newOrientation.scale={1,1,1};
                const auto oldMatrix=ecs::ComposeTRS(oldOrientation),newMatrix=ecs::ComposeTRS(newOrientation);
                for(std::size_t i=0;i<16;++i)if(!near(oldMatrix[i],newMatrix[i]))throw std::runtime_error("Collider orientation mismatch");
            }catch(const std::exception& e){throw std::runtime_error("Upgrade Entity "+id+": "+e.what());}
        }
        if(!ValidateAuthoringCandidate(*candidate,&diagnostic))throw std::runtime_error(diagnostic);
        auto captured=SceneSerializer::CaptureWorld(*candidate->world,candidate->settings);captured["worldMode"]="rootOnly";
        // Includes every serialized Rigidbody/Trigger/material coefficient,
        // Collider property, PlayerStart, script, light and authored Transform.
        if(captured!=original)throw std::runtime_error("Upgrade would change serialized components/settings or primary light selection");
        candidate->sceneKey=m_SceneKey;candidate->configSceneKey=m_Config.ecsDemo.sceneFile;candidate->controlledHierarchy=true;
        return candidate;
    }catch(const std::exception& e){Failure(error,"Upgrade preflight: ",e);return {};}
}
