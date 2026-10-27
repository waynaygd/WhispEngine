#include "../FogGather.h"
#include "RenderSystem.h"
#include "../components/UICanvasComponent.h"
#include "../LightGather.h"
#include "../CameraAttachments.h"

#include "../World.h"
#include "../components/MaterialComponent.h"
#include "../components/ColliderComponent.h"
#include "../components/MeshRendererComponent.h"
#include "../components/TransformComponent.h"
#include "../../core/AssetPaths.h"
#include "../../core/Logger.h"
#include "../../render/IRenderAdapter.h"
#include "../../resources/MaterialResource.h"
#include "../../resources/MeshResource.h"
#include "../../resources/ResourceManager.h"
#include "../../resources/ShaderResource.h"
#include "../../resources/TextureResource.h"

#include <cmath>
#include <tuple>
#include <chrono>
#include <tracy/Tracy.hpp>

namespace
{
constexpr float kWhiteTint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

void MultiplyMatrix(const float* lhs, const float* rhs, float* out16)
{
    float result[16]{};

    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            for (int k = 0; k < 4; ++k)
                result[col * 4 + row] += lhs[k * 4 + row] * rhs[col * 4 + k];
        }
    }

    for (int i = 0; i < 16; ++i)
        out16[i] = result[i];
}

void BuildMvp(float* out16,const ecs::RenderPose& pose,const ecs::CameraState& camera,float* outModel=nullptr)
{
    float model[16];ecs::BuildRenderModelMatrix(model,pose);
    if(outModel)std::copy(model,model+16,outModel);
    MultiplyMatrix(camera.viewProjection.data(),model,out16);
}

}

namespace ecs {
    RenderSystem::~RenderSystem() {
        ReleaseGpuResources();
    }

    void RenderSystem::SetRenderAdapter(IRenderAdapter* renderer)
    {
        if (renderer != nullptr && m_ResourceOwnerRenderer != nullptr && renderer != m_ResourceOwnerRenderer)
            ReleaseGpuResources();

        m_Renderer = renderer;
        if (renderer != nullptr && m_ResourceOwnerRenderer == nullptr)
            m_ResourceOwnerRenderer = renderer;
    }

    void RenderSystem::SetCameraState(const CameraState& state)
    {
        m_RenderCamera=state;m_CameraPosition=state.pose.position;
        m_CameraYaw=std::atan2(state.forward.x,state.forward.z);m_CameraPitch=std::asin(std::clamp(state.forward.y,-1.f,1.f));
        m_CameraVerticalFovRadians=state.settings.fieldOfView;m_CameraAspectRatio=state.aspect;
        m_CameraNearPlane=state.settings.nearClip;m_CameraFarPlane=state.settings.farClip;
    }
    void RenderSystem::RebuildLegacyCamera()
    {
        CameraComponent c;c.fieldOfView=m_CameraVerticalFovRadians;c.nearClip=m_CameraNearPlane;c.farClip=m_CameraFarPlane;
        SetCameraState(BuildCameraState({m_CameraPosition,{1,1,1},RenderRotation({-m_CameraPitch,m_CameraYaw,0})},c,m_CameraAspectRatio));
    }
    void RenderSystem::SetCameraTransform(const Vec3& position, float yawRadians, float pitchRadians)
    {
        m_CameraPosition = position;
        m_CameraYaw = yawRadians;
        m_CameraPitch = pitchRadians;
        RebuildLegacyCamera();
    }

    void RenderSystem::SetCameraProjection(
        float verticalFovRadians,
        float aspectRatio,
        float nearPlane,
        float farPlane)
    {
        m_CameraVerticalFovRadians = std::isfinite(verticalFovRadians)?std::clamp(verticalFovRadians,.017453293f,3.1241394f):1.04719755f;
        m_CameraAspectRatio = aspectRatio > 0.001f ? aspectRatio : 16.0f / 9.0f;
        m_CameraNearPlane = std::isfinite(nearPlane)?std::clamp(nearPlane,.001f,999999.f):.01f;
        m_CameraFarPlane =
            farPlane > m_CameraNearPlane + 0.001f
            ? farPlane
            : m_CameraNearPlane + 0.001f;
        m_CameraFarPlane=std::min(m_CameraFarPlane,1000000.f);
        RebuildLegacyCamera();
    }

    void RenderSystem::ReleaseGpuResources()
    {
        IRenderAdapter* renderer = m_ResourceOwnerRenderer != nullptr ? m_ResourceOwnerRenderer : m_Renderer;
        if (renderer != nullptr)
        {
            m_UIFonts.Clear(renderer);
            if(m_UIFontTexture.IsValid())renderer->DestroyTexture(m_UIFontTexture);m_UIFontTexture={};m_RuntimeUI.Clear();m_UIDrawList={};
            for (auto& [key, resource] : m_ShaderResources)
            {
                (void)key;
                if (resource != nullptr && resource->GetData().gpuHandle.IsValid())
                {
                    renderer->DestroyShader(resource->GetData().gpuHandle);
                    resource->GetData().gpuHandle = RenderShaderHandle::Invalid();
                    resource->GetData().gpuHandleVersion = 0;
                }
            }

            for (auto& [key, resource] : m_TextureResources)
            {
                (void)key;
                if (resource != nullptr && resource->GetData().gpuHandle.IsValid())
                {
                    renderer->DestroyTexture(resource->GetData().gpuHandle);
                    resource->GetData().gpuHandle = RenderTextureHandle::Invalid();
                    resource->GetData().gpuHandleVersion = 0;
                }
            }

            for (auto& [key, resource] : m_MeshResources)
            {
                (void)key;
                if (resource != nullptr && resource->GetData().gpuHandle.IsValid())
                {
                    renderer->DestroyMesh(resource->GetData().gpuHandle);
                    resource->GetData().gpuHandle = RenderMeshHandle::Invalid();
                    resource->GetData().gpuHandleVersion = 0;
                }
            }
        }

        m_MeshResources.clear();m_ShadowLocalBounds.clear();m_ShadowWorldBounds.clear();
        m_TextureResources.clear();
        m_ShaderResources.clear();
        m_EnvironmentAsset.reset();m_RequestedEnvironment.clear();m_EnvironmentKey.clear();m_EnvironmentVersion=0;m_FailedEnvironmentKey.clear();m_FailedEnvironmentVersion=0;
        if(m_Renderer)m_Renderer->ReleaseEnvironment();
        m_MaterialResources.clear();
        m_FailedMeshKeys.clear();
        m_FailedTextureKeys.clear();
        m_FailedShaderKeys.clear();
        m_FailedMaterialKeys.clear();
        m_FailedMeshGpuVersions.clear();
        m_FailedTextureGpuVersions.clear();
        m_FailedShaderGpuVersions.clear();
        m_LoggedMeshReuseKeys.clear();
        m_LoggedTextureReuseKeys.clear();
        m_LoggedShaderReuseKeys.clear();
        m_GpuFinalizeQueue.clear();m_PendingGpuFinalizations.clear();
        m_RenderSnapshots.clear();m_RenderPackets.clear();m_Batches.clear();m_ShadowBatches.clear();m_InstanceData.clear();
        m_ResourceOwnerRenderer = nullptr;
    }

    void RenderSystem::Update(World& world, float dt)
    {
        ZoneScopedN("RenderSystem");

        (void)dt;
        m_Statistics = {};m_MaterialDiagnostics.clear();
        LightCamera lightCamera;
        lightCamera.view=m_RenderCamera.view;lightCamera.projection=m_RenderCamera.projection;
        lightCamera.nearPlane=m_CameraNearPlane;lightCamera.farPlane=m_CameraFarPlane;
        auto lights=GatherSceneLights(world,m_LightingSettings?*m_LightingSettings:m_DefaultLighting,lightCamera,m_Interpolation);
        if(m_ViewportUnlit)lights.lighting.enabled=false;
        auto fog=GatherFog(world,lightCamera);
        PostProcessSettings postBase;postBase.fog=fog.settings;postBase.fog.enabled=fog.active;
        if(m_EnvironmentSettings)postBase.environment=*m_EnvironmentSettings;
        if(m_AtmosphereSettings)postBase.atmosphere=*m_AtmosphereSettings;
        m_EvaluatedPostProcess=EvaluatePostProcess(m_CameraPosition,world,postBase);
        fog.settings=m_EvaluatedPostProcess.fog;fog.active=fog.settings.enabled;
        if(m_Renderer){
            std::string warning;
            if(fog.activeCount>1)warning="Multiple enabled global Fog components: lowest Entity index wins";
            if(fog.invalidCount)warning="Invalid Fog component ignored at render boundary";
            if(fog.active&&!m_Renderer->GetFogDiagnostics().supported)warning="Fog is not supported by this render backend";
            if(warning!=m_FogWarning){m_FogWarning=warning;if(!warning.empty())Logger::Get().Warn(warning);}
            m_Renderer->SetFog(fog);
            m_Renderer->SetScreenEffects(m_EvaluatedPostProcess.screenEffects);
            m_PostFxTime+=std::isfinite(dt)?std::max(dt,0.f):0;
            if(m_ResourceManager)m_Renderer->SetCustomPostFx(ResolvePostFx(m_EvaluatedPostProcess.customEffects,*m_ResourceManager,m_PostFxOverrides,m_PostFxTime,dt));
        }
        if(m_Renderer){m_Renderer->SetCameraPosition({m_CameraPosition.x,m_CameraPosition.y,m_CameraPosition.z,0});
            m_Renderer->SetLighting(BuildLightingConstants(lights.lighting));
            m_Renderer->SetSceneLights(lights.lights,m_LightRenderingMode,lights.diagnostics);}

        if (m_Renderer == nullptr)
            return;
        if(m_EnvironmentSettings){
            auto environment=m_EvaluatedPostProcess.environment;if(m_ViewportUnlit){environment.diffuse=environment.specular=false;environment.ambientOcclusion.enabled=false;}
            m_Renderer->SetEnvironmentSettings(environment);
            if(m_ResourceManager&&m_EnvironmentSettings->enabled){
                const auto& key=m_EnvironmentSettings->assetPath;
                if(!m_EnvironmentAsset||m_RequestedEnvironment!=key){
                    m_RequestedEnvironment=key;m_EnvironmentAsset=m_ResourceManager->LoadAsync<EnvironmentResource>(key).get();
                    m_ResourceManager->WatchForHotReload<EnvironmentResource>(key);
                }
                auto asset=m_EnvironmentAsset;
                if(asset&&asset->IsLoaded()&&!asset->GetData().placeholder&&(m_EnvironmentKey!=asset->GetKey()||m_EnvironmentVersion!=asset->GetVersion())&&(m_FailedEnvironmentKey!=asset->GetKey()||m_FailedEnvironmentVersion!=asset->GetVersion())){
                    if(m_Renderer->UploadEnvironment(asset->GetData(),asset->GetVersion())){m_EnvironmentKey=asset->GetKey();m_EnvironmentVersion=asset->GetVersion();m_FailedEnvironmentKey.clear();}else{m_FailedEnvironmentKey=asset->GetKey();m_FailedEnvironmentVersion=asset->GetVersion();}
                }
            }
        }

        constexpr std::size_t
            maxGpuFinalizationsPerFrame = 1;

        if(m_ResourceManager&&m_ResourceCacheEpoch!=m_ResourceManager->GetCacheEpoch()){
            m_ResourceCacheEpoch=m_ResourceManager->GetCacheEpoch();
            auto refresh=[&](auto& cache,auto release,auto& failedVersions){for(auto it=cache.begin();it!=cache.end();){
                using T=std::remove_cvref_t<decltype(it->second->GetData())>;
                auto current=m_ResourceManager->Get<T>(it->first);
                if constexpr(std::is_same_v<T,TextureResource>)if(it->first=="defaults/texture")current=m_ResourceManager->GetDefault<T>();
                if(current!=it->second){auto& data=it->second->GetData();if(data.gpuHandle.IsValid()){release(data.gpuHandle);data.gpuHandle={};data.gpuHandleVersion=0;}failedVersions.erase(it->first);it=cache.erase(it);}else ++it;
            }};
            refresh(m_TextureResources,[&](auto handle){m_Renderer->DestroyTexture(handle);},m_FailedTextureGpuVersions);
            refresh(m_MeshResources,[&](auto handle){m_Renderer->DestroyMesh(handle);},m_FailedMeshGpuVersions);
            refresh(m_ShaderResources,[&](auto handle){m_Renderer->DestroyShader(handle);},m_FailedShaderGpuVersions);
            for(auto it=m_MaterialResources.begin();it!=m_MaterialResources.end();)if(m_ResourceManager->Get<MaterialResource>(it->first)!=it->second)it=m_MaterialResources.erase(it);else ++it;
        }
        const auto uploadStart=std::chrono::steady_clock::now();
        PumpGpuFinalization(maxGpuFinalizationsPerFrame);
        m_Statistics.gpuFinalizationMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-uploadStart).count();

        const auto submittedBefore = m_Renderer->GetSubmissionStatistics();
        const auto elapsed = [](auto start) { return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(); };
        {
            ZoneScopedN("RenderEntities");

            m_RenderSnapshots.clear();m_ShadowWorldBounds.clear();
            m_RenderSnapshots.reserve(world.GetAliveCount());

            {
                ZoneScopedN("RenderGather");
                const auto start = std::chrono::steady_clock::now();

                world.ForEach<TransformComponent, MeshRendererComponent>(
                    [&](Entity entity,
                        TransformComponent& transform,
                        MeshRendererComponent& meshRenderer)
                    {
                        if (!meshRenderer.visible||(!m_EditorIsolated.empty()&&!m_EditorIsolated.contains(entity)))
                            return;

                        const auto key=m_MeshResources.contains(meshRenderer.meshPath)?meshRenderer.meshPath:AssetPaths::NormalizeAssetKey(meshRenderer.meshPath);
                        const auto meshHandle=GetOrUploadMesh(key);if(!meshHandle.IsValid())return;
                        const auto resource=m_MeshResources.find(key);
                        if(resource==m_MeshResources.end())return;
                        const auto& mesh=resource->second->GetData().meshData;
                        const auto count=std::max(std::size_t(1),mesh.submeshes.size());
                        const auto attachment=m_CameraAttachmentsEnabled?CameraAttachmentPose(world,entity,m_RenderCamera,m_Interpolation):std::optional<RenderPose>{};
                        const auto pose=attachment?*attachment:m_Interpolation?m_Interpolation->ResolveWorld(world,entity):RenderPose::From(world.WorldTransform(entity));
                        bool visible=false,casts=false;
                        for(std::size_t slot=0;slot<count;++slot) {
                            RenderSnapshot snapshot{};
                            if(TryBuildRenderSnapshot(meshRenderer,world.GetComponent<MaterialComponent>(entity),snapshot,mesh,meshHandle,slot)) {
                                snapshot.renderMode=meshRenderer.renderMode;if(snapshot.renderMode==MeshRenderMode::FirstPerson){snapshot.castShadows=false;snapshot.materialState.receiveShadows=false;}casts|=snapshot.castShadows;snapshot.pose=pose;if(m_TraceMaterials&&!m_MaterialDiagnostics.empty()){m_MaterialDiagnostics.back()["entity"]=entity.index;m_MaterialDiagnostics.back()["generation"]=entity.generation;}m_RenderSnapshots.push_back(std::move(snapshot));visible=true;
                            }
                        }
                        if(visible)++m_Statistics.visibleObjects;
                        if(casts){auto it=m_ShadowLocalBounds.find(meshHandle.value);
                            if(it==m_ShadowLocalBounds.end()){ShadowCasterBounds b{{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};for(auto& v:mesh.vertices)for(int k=0;k<3;++k){b.minimum[k]=std::min(b.minimum[k],v.position[k]);b.maximum[k]=std::max(b.maximum[k],v.position[k]);}it=m_ShadowLocalBounds.emplace(meshHandle.value,b).first;}
                            ShadowCasterBounds b{{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};float model[16];BuildRenderModelMatrix(model,pose);
                            for(int c=0;c<8;++c)for(int row=0;row<3;++row){float value=model[12+row];for(int k=0;k<3;++k)value+=model[k*4+row]*(c&(1<<k)?it->second.maximum[k]:it->second.minimum[k]);b.minimum[row]=std::min(b.minimum[row],value);b.maximum[row]=std::max(b.maximum[row],value);}m_ShadowWorldBounds.push_back(b);
                        }
                    });
                for(const auto& item:m_PlacementItems){
                    const auto resource=m_ResourceManager->Get<MeshResource>(item.mesh);
                    if(resource&&resource->IsLoaded()&&!resource->GetData().placeholder){
                        RenderSnapshot preview;preview.mesh=GetOrUploadMesh(item.mesh);
                        preview.shader=GetOrCreateShader("dx12/textured.hlsl");preview.texture=GetOrCreateTexture("defaults/texture");
                        preview.pose=RenderPose::From(item.transform);preview.castShadows=false;preview.materialState={true,false,false};
                        preview.baseColor={.08f,.8f,1,1};preview.tint={.5f,1,1,1};
                        if(preview.mesh.IsValid()&&preview.shader.IsValid()&&preview.texture.IsValid())m_RenderSnapshots.push_back(preview);
                    }
                }
                m_Statistics.gatherMs = elapsed(start);
                m_Statistics.visibleSubmeshes = m_RenderSnapshots.size();
                m_Statistics.viewmodelSubmeshes=std::count_if(m_RenderSnapshots.begin(),m_RenderSnapshots.end(),[](const auto& s){return s.renderMode==MeshRenderMode::FirstPerson;});m_Statistics.worldSubmeshes=m_RenderSnapshots.size()-m_Statistics.viewmodelSubmeshes;
            }

            {
                ZoneScopedN("RenderPrepare");
                const auto start = std::chrono::steady_clock::now();

                m_RenderPackets.resize(m_RenderSnapshots.size());

                for (std::size_t i = 0; i < m_RenderSnapshots.size(); ++i)
                {
                    PrepareRenderPacket(
                        m_RenderSnapshots[i],
                        m_RenderPackets[i]);
                }
                m_Statistics.prepareMs = elapsed(start);
            }

            {
                ZoneScopedN("RenderBatchBuild");
                const auto start = std::chrono::steady_clock::now();
                m_Batches.clear();
                m_Batches.reserve(m_RenderPackets.size());
                m_InstanceData.resize(m_RenderPackets.size());
                // All supported surfaces are opaque: order by compatible state/range.
                const auto batchKey=[](const RenderPacket& p) {return std::tuple(int(p.renderMode),p.mesh.value,p.shader.value,p.texture.value,p.maps[0].value,p.maps[1].value,p.maps[2].value,
                    p.range.firstIndex,p.range.indexCount,p.materialState.doubleSided,p.materialState.receiveShadows,p.materialState.reverseWinding);};
                std::stable_sort(m_RenderPackets.begin(),m_RenderPackets.end(),[&](const auto& a,const auto& b){return batchKey(a)<batchKey(b);});
                for (std::size_t i=0;i<m_RenderPackets.size();++i) {
                    m_InstanceData[i]=m_RenderPackets[i].instance;
                    const auto& packet=m_RenderPackets[i];
                    if(i>0 && packet.renderMode==m_RenderPackets[i-1].renderMode && packet.mesh==m_RenderPackets[i-1].mesh &&
                        packet.maps==m_RenderPackets[i-1].maps && packet.texture==m_RenderPackets[i-1].texture && packet.shader==m_RenderPackets[i-1].shader &&
                        packet.range==m_RenderPackets[i-1].range && packet.materialState==m_RenderPackets[i-1].materialState)
                        ++m_Batches.back().count;
                    else m_Batches.push_back({i,1});
                }
                m_Statistics.batchMs=elapsed(start);
            }
            {
                ZoneScopedN("ShadowPrepare");
                const auto start=std::chrono::steady_clock::now();
                m_ShadowBatches.clear();
                for(std::size_t i=0;i<m_RenderPackets.size();++i) {
                    if(!m_RenderPackets[i].castShadows)continue;
                    if(!m_ShadowBatches.empty()&&m_ShadowBatches.back().begin+m_ShadowBatches.back().count==i &&
                        m_RenderPackets[i].mesh==m_RenderPackets[i-1].mesh && m_RenderPackets[i].range==m_RenderPackets[i-1].range &&
                        m_RenderPackets[i].materialState.doubleSided==m_RenderPackets[i-1].materialState.doubleSided &&
                        m_RenderPackets[i].materialState.reverseWinding==m_RenderPackets[i-1].materialState.reverseWinding)++m_ShadowBatches.back().count;
                    else m_ShadowBatches.push_back({i,1});
                }
                m_Statistics.shadowPrepareMs=elapsed(start);
            }
            {
                ZoneScopedN("ShadowPass");
                const auto start=std::chrono::steady_clock::now();
                auto settings=SanitizeShadowSettings(m_ShadowSettings?*m_ShadowSettings:m_DefaultShadows);
                settings.enabled=settings.enabled&&lights.primaryCastShadows&&!m_ViewportUnlit;
                const auto light=BuildLightingConstants(lights.lighting);
                ShadowFrame frame;
                if(m_RenderCamera.settings.projection==CameraProjection::Orthographic){
                    float far=std::min(m_CameraFarPlane,std::max(m_CameraNearPlane,settings.focusDistance));
                    float distance=(m_CameraNearPlane+far)*.5f;
                    float half=m_RenderCamera.settings.orthographicSize*.5f;
                    if(settings.cameraFitted){
                        float radius=std::ceil(std::sqrt(half*half*(1+m_CameraAspectRatio*m_CameraAspectRatio)+(far-distance)*(far-distance)));
                        settings.coverage=std::max(settings.coverage,2*radius);
                        settings.coverage+=2*settings.coverage/settings.resolution;
                        settings.depthRange=std::max(settings.depthRange,settings.coverage+20.f);
                    }else distance=settings.focusDistance;
                    auto p=m_RenderCamera.pose.position,f=m_RenderCamera.forward;
                    frame=BuildDirectionalShadowFrame(light,settings,{p.x+f.x*distance,p.y+f.y*distance,p.z+f.z*distance});
                }else frame=BuildCameraDirectionalShadowFrame(light,settings,
                    {m_CameraPosition.x,m_CameraPosition.y,m_CameraPosition.z},m_CameraYaw,m_CameraPitch,
                    m_CameraVerticalFovRadians,m_CameraAspectRatio,m_CameraNearPlane,m_CameraFarPlane);
                ExtendDirectionalCasterDepth(frame,m_ShadowWorldBounds);
                if(m_Renderer->BeginShadowPass(frame)) {
                    for(const auto& batch:m_ShadowBatches) {
                        const auto& packet=m_RenderPackets[batch.begin];
                        m_Renderer->SetMeshRange(packet.range);m_Renderer->SetMaterialState(packet.materialState);
                        const auto mesh=packet.mesh;
                        if(m_InstancingEnabled&&batch.count>1&&m_Renderer->DrawShadowMeshInstanced(mesh,
                            std::span<const RenderInstanceData>(m_InstanceData.data()+batch.begin,batch.count)))continue;
                        for(std::size_t i=batch.begin;i<batch.begin+batch.count;++i)m_Renderer->DrawShadowMesh(mesh,m_InstanceData[i]);
                    }
                    m_Renderer->EndShadowPass();
                }
                // Spot maps share caster submissions; their light-pose basis is independent of the camera.
                const bool spotEnabled=(m_ShadowSettings?m_ShadowSettings->enabled:m_DefaultShadows.enabled)&&!m_ViewportUnlit;
                m_Renderer->SetSpotShadowFrames(spotEnabled?std::span<const SpotShadowFrame>(lights.spotShadows):std::span<const SpotShadowFrame>{},spotEnabled?lights.spotShadowRequests:0);
                if(spotEnabled)for(size_t slot=0;slot<lights.spotShadows.size();++slot)if(m_Renderer->BeginSpotShadowPass(unsigned(slot))){
                    const auto& spot=lights.spotShadows[slot];
                    auto intersects=[&](size_t i){const auto& packet=m_RenderPackets[i];auto it=m_ShadowLocalBounds.find(packet.mesh.value);if(it==m_ShadowLocalBounds.end())return true;
                        ShadowCasterBounds bounds{{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};const auto& model=m_InstanceData[i].model;
                        for(int c=0;c<8;++c)for(int row=0;row<3;++row){float value=model[12+row];for(int k=0;k<3;++k)value+=model[k*4+row]*(c&(1<<k)?it->second.maximum[k]:it->second.minimum[k]);bounds.minimum[row]=std::min(bounds.minimum[row],value);bounds.maximum[row]=std::max(bounds.maximum[row],value);}
                        return SpotCasterIntersects(spot,bounds);
                    };
                    for(const auto& batch:m_ShadowBatches){const auto& packet=m_RenderPackets[batch.begin];m_Renderer->SetMeshRange(packet.range);m_Renderer->SetMaterialState(packet.materialState);
                        size_t cursor=batch.begin,end=batch.begin+batch.count;
                        while(cursor<end){while(cursor<end&&!intersects(cursor))++cursor;const size_t begin=cursor;while(cursor<end&&intersects(cursor))++cursor;const size_t count=cursor-begin;if(!count)continue;
                            if(m_InstancingEnabled&&count>1&&m_Renderer->DrawShadowMeshInstanced(packet.mesh,std::span<const RenderInstanceData>(m_InstanceData.data()+begin,count)))continue;
                            for(size_t i=begin;i<cursor;++i)m_Renderer->DrawShadowMesh(packet.mesh,m_InstanceData[i]);
                        }
                    }
                    m_Renderer->EndShadowPass();
                }
                m_Statistics.shadowMs=elapsed(start);
            }
            if(m_Particles)m_Particles->Build(world,m_RenderCamera,m_Interpolation,[this](const std::string& key){return GetOrCreateTexture(key);},m_WorldParticles,m_FirstPersonParticles);
            else {m_WorldParticles.vertices.clear();m_WorldParticles.commands.clear();m_FirstPersonParticles.vertices.clear();m_FirstPersonParticles.commands.clear();}
            // Reuse opaque depth submission (including non-shadow-casting meshes).
            // First-person geometry/particles are excluded from world occlusion.
            if(!m_ViewportUnlit&&m_Renderer->BeginAmbientOcclusion(m_RenderCamera.viewProjection,m_RenderCamera.projection)){
                for(const auto& batch:m_Batches){const auto& packet=m_RenderPackets[batch.begin];
                    if(packet.renderMode!=MeshRenderMode::World)continue;
                    m_Renderer->SetMeshRange(packet.range);m_Renderer->SetMaterialState(packet.materialState);
                    if(m_InstancingEnabled&&batch.count>1&&m_Renderer->DrawShadowMeshInstanced(packet.mesh,std::span<const RenderInstanceData>(m_InstanceData.data()+batch.begin,batch.count)))continue;
                    for(size_t i=batch.begin;i<batch.begin+batch.count;++i)m_Renderer->DrawShadowMesh(packet.mesh,m_InstanceData[i]);
                }
                m_Renderer->EndAmbientOcclusion();
            }
            m_Renderer->BeginMainPass();
            {
                ZoneScopedN("RenderSubmit");
                const auto start = std::chrono::steady_clock::now();
                auto submitBatch=[&](const auto& batch) {
                    const auto& packet=m_RenderPackets[batch.begin];
                    m_Renderer->SetMeshRange(packet.range);m_Renderer->SetMaterialState(packet.materialState);
                    if(m_InstancingEnabled && batch.count>1 && m_Renderer->SupportsInstancing(packet.shader)) {
                        m_Renderer->BindShader(packet.shader);
                        m_Renderer->BindTexture(0,packet.texture);
                        for(unsigned i=0;i<3;++i)m_Renderer->BindTexture(i+1,packet.maps[i]);
                        if(m_Renderer->DrawMeshInstanced(packet.mesh,std::span<const RenderInstanceData>(m_InstanceData.data()+batch.begin,batch.count)))return;
                    }
                    ZoneScopedN("RenderFallbackDraw");
                    for(std::size_t i=batch.begin;i<batch.begin+batch.count;++i)SubmitRenderPacket(m_RenderPackets[i]);
                };
                for(const auto& batch:m_Batches)if(m_RenderPackets[batch.begin].renderMode==MeshRenderMode::World)submitBatch(batch);
                m_Renderer->BeginWorldTransparency();
                m_Renderer->DrawParticles(m_WorldParticles);
                if((m_Statistics.viewmodelSubmeshes||!m_FirstPersonParticles.vertices.empty())&&m_Renderer->BeginViewmodelPass()){
                    for(const auto& batch:m_Batches)if(m_RenderPackets[batch.begin].renderMode==MeshRenderMode::FirstPerson)submitBatch(batch);
                    m_Renderer->DrawParticles(m_FirstPersonParticles);
                    m_Renderer->EndViewmodelPass();
                }
                m_Statistics.submitMs=elapsed(start);
            }
            m_Renderer->EndMainPass();

        }

        const auto finishStatistics = [&] {
        const auto submitted = m_Renderer->GetSubmissionStatistics();
        m_Statistics.submitted=submitted;
        m_Statistics.submitted.drawCalls-=submittedBefore.drawCalls;
        m_Statistics.submitted.outputDrawCalls-=submittedBefore.outputDrawCalls;
        m_Statistics.submitted.instancedDrawCalls-=submittedBefore.instancedDrawCalls;
        m_Statistics.submitted.renderedInstances-=submittedBefore.renderedInstances;
        m_Statistics.submitted.uploadBytes-=submittedBefore.uploadBytes;
        m_Statistics.submitted.frameConstantUploadBytes-=submittedBefore.frameConstantUploadBytes;
        m_Statistics.submitted.shadowDrawCalls-=submittedBefore.shadowDrawCalls;
        m_Statistics.submitted.shadowInstancedDrawCalls-=submittedBefore.shadowInstancedDrawCalls;
        m_Statistics.submitted.shadowCasters-=submittedBefore.shadowCasters;
        m_Statistics.submitted.instanceBufferGrowths-=submittedBefore.instanceBufferGrowths;
        };
        auto renderUI=[&](){
            m_UIDrawList={};bool hasUI=false;if(m_UIPreview)for(auto e:world.Entities())if(auto* c=world.GetComponent<UICanvasComponent>(e);c&&c->enabled&&!c->document.empty()){hasUI=true;break;}
            if(hasUI&&m_ResourceManager){
                if(!m_UIFont)m_UIFont=ui::RasterizeFont();
                if(!m_UIFontTexture.IsValid())m_UIFontTexture=m_Renderer->CreateTexture2D(m_UIFont->texture);
                auto area=m_Renderer->RenderAreaSize();
                const auto layoutStart=std::chrono::steady_clock::now();
                m_UIDrawList=m_RuntimeUI.Build(world,*m_ResourceManager,float(area[0]),float(area[1]),*m_UIFont,m_UIFontTexture,[&](const std::string& key){return key.empty()?RenderTextureHandle{}:GetOrCreateTexture(key,true);},[&](const std::string& key){return m_UIFonts.Get(key,*m_ResourceManager,m_Renderer);});
                m_Statistics.runtimeUILayoutMs=elapsed(layoutStart);
                const auto uiSubmitStart=std::chrono::steady_clock::now();
                m_Renderer->DrawRuntimeUI(m_UIDrawList);
                m_Statistics.runtimeUISubmitMs=elapsed(uiSubmitStart);
            }else if(m_ResourceManager)m_RuntimeUI.Sync(world,*m_ResourceManager);
        };
        if (!m_DebugCollidersEnabled) { renderUI();finishStatistics(); return; }
        {
            ZoneScopedN("DebugColliders");

            world.ForEach<TransformComponent, ColliderComponent>(
                [&](Entity entity, TransformComponent& transform, ColliderComponent& collider)
                {
                    if(!m_EditorIsolated.empty()&&!m_EditorIsolated.contains(entity))return;
                    float mvp[16];
                    RenderPose debugTransform = m_Interpolation && !m_PhysicsDebugPose ? m_Interpolation->ResolveWorld(world,entity) : RenderPose::From(world.WorldTransform(entity));
                    debugTransform.position.x += collider.offset.x;
                    debugTransform.position.y += collider.offset.y;
                    debugTransform.position.z += collider.offset.z;
                    // Box colliders can be oriented; keep rotation for box debug draw.
                    if (collider.type == ColliderType::Sphere)
                    {
                        const float radius = std::max(collider.halfExtents.x, std::max(collider.halfExtents.y, collider.halfExtents.z));
                        debugTransform.scale = ecs::Vec3{ radius * 2.0f, radius * 2.0f, radius * 2.0f };
                    }
                    else
                    {
                        // Box colliders can be oriented; keep rotation for box debug draw.
                        debugTransform.scale = ecs::Vec3{
                            collider.halfExtents.x * 2.0f,
                            collider.halfExtents.y * 2.0f,
                            collider.halfExtents.z * 2.0f
                        };
                    }
                    BuildMvp(mvp,debugTransform,m_RenderCamera);
                    m_Renderer->SetTestTransform(mvp);
                    m_Renderer->SetTestColor(0.1f, 1.0f, 0.1f, 1.0f);
                    m_Renderer->DrawTestCube();
                });
        }
        renderUI();finishStatistics();
    }

    bool RenderSystem::TryBuildRenderSnapshot(const MeshRendererComponent& meshRenderer,
        const MaterialComponent* component,RenderSnapshot& snapshot,const MeshData& mesh,RenderMeshHandle meshHandle,std::size_t submeshIndex)
    {
        ZoneScopedN("MaterialResolve");
        ++m_Statistics.materialResolves;
        const auto start=std::chrono::steady_clock::now();
        const auto finish=[&](bool result) {m_Statistics.materialResolveMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return result;};
        if(!m_Renderer||!m_ResourceManager||meshRenderer.meshPath.empty())return finish(false);
        snapshot.mesh=meshHandle;
        MaterialResource material;std::size_t slot=0;
        if(submeshIndex<mesh.submeshes.size()) {
            const auto& submesh=mesh.submeshes[submeshIndex];slot=submesh.materialSlot;
            snapshot.range={submesh.firstIndex,submesh.indexCount};
            if(!submesh.indexCount)return finish(false);
            if(slot<mesh.materials.size())material=mesh.materials[slot];
        }
        std::string asset;
        if(component) {
            asset=component->materialPath;
            if(slot<component->slotMaterials.size()&&!component->slotMaterials[slot].empty())asset=component->slotMaterials[slot];
        }
        if(!asset.empty()) {
            const auto pathStart=std::chrono::steady_clock::now();
            std::string key=asset;
            if(!m_MaterialResources.contains(asset)){++m_Statistics.materialPathChecks;key=AssetPaths::NormalizeAssetKey(asset);}
            m_Statistics.materialPathMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-pathStart).count();
            if(m_MaterialResources.count(key))++m_Statistics.materialCacheHits;else ++m_Statistics.materialCacheMisses;
            const auto resource=key.empty()?nullptr:GetOrLoadMaterial(key);
            if(!resource||resource->IsLoading())return finish(false);
            if(resource->IsUsable())material=resource->GetData();
        }
        if(component)ApplyMaterialOverrides(material,component->overrides);
        // Legacy scene paths are explicit overrides; preserve their existing priority.
        if(!meshRenderer.shaderPath.empty())material.shaderPath=meshRenderer.shaderPath;
        if(!meshRenderer.texturePath.empty())material.texturePath=meshRenderer.texturePath;
        if(component) {
            if(!component->shaderPath.empty())material.shaderPath=component->shaderPath;
            if(!component->texturePath.empty())material.texturePath=component->texturePath;
        }
        if(material.surfaceMode!=MaterialSurfaceMode::Opaque) {
            const auto diagnostic=material.sourcePath+"#unsupported-surface";
            if(m_FailedMaterialKeys.insert(diagnostic).second)Logger::Get().Warn("Material surface mode unsupported; omitted: "+material.sourcePath);
            return finish(false);
        }
        const bool hasUV0=submeshIndex>=mesh.submeshes.size()||mesh.submeshes[submeshIndex].hasUV0;
        auto textureStart=std::chrono::steady_clock::now();
        std::string textureKey="defaults/texture";
        if(!material.texturePath.empty()&&material.textureTexCoords[0]==0&&hasUV0) {
            auto pathStart=std::chrono::steady_clock::now();
            // Imported texture paths are already canonical resource keys. Reuse
            // the renderer-owned resource, just as the optional maps do below.
            // GetOrCreateTexture still checks usability and GPU resource version;
            // cache-epoch refresh and ReleaseGpuResources retire stale owners.
            if(m_TextureResources.contains(material.texturePath))textureKey=material.texturePath;
            else {++m_Statistics.materialPathChecks;textureKey=AssetPaths::NormalizeAssetKey(material.texturePath);}
            m_Statistics.materialPathMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-pathStart).count();
        }
        snapshot.texture=GetOrCreateTexture(textureKey);
        snapshot.textureParameters={material.normalScale,material.occlusionStrength,0,1};
        const std::string* paths[3]={&material.normalTexturePath,&material.metallicRoughnessTexturePath,&material.occlusionTexturePath};
        for(int i=0;i<3;++i)if(hasUV0 && !paths[i]->empty() && material.textureTexCoords[i+1]==0) {
            snapshot.maps[i]=GetOrCreateTexture(m_TextureResources.count(*paths[i])?*paths[i]:AssetPaths::NormalizeAssetKey(*paths[i]),true);
            if(snapshot.maps[i].IsValid()&&(m_TextureMapMask&(1<<i)))snapshot.textureParameters[2]+=float(1<<i);
        }
        m_Statistics.materialTextureMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-textureStart).count();
        const auto shaderStart=std::chrono::steady_clock::now();
        const auto shaderKey=AssetPaths::NormalizeShaderKey(material.shaderPath);
        snapshot.shader=GetOrCreateShader(shaderKey);
        const auto shaderSource=m_ResourceManager->Get<ShaderResource>(shaderKey);
        m_Statistics.materialShaderMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-shaderStart).count();
        const bool colorPipeline=shaderSource && shaderSource->GetData().fragmentSource.find("PBR_COLOR_PIPELINE_V1")!=std::string::npos;
        for(int i=0;i<4;++i) {
            snapshot.tint[i]=(colorPipeline?1.f:material.baseColor[i])*(component?component->tint[i]:1.f);
            snapshot.baseColor[i]=colorPipeline?material.baseColor[i]:1.f;
        }
        snapshot.surface={material.metallic,material.roughness,float(m_ShadingModelOverride>=0?m_ShadingModelOverride:int(material.shadingModel)),0};
        snapshot.materialState={material.doubleSided,material.receiveShadows};snapshot.castShadows=material.castShadows;
        if(m_TraceMaterials){nlohmann::json record={{"mesh",meshRenderer.meshPath},{"submesh",submeshIndex},{"slot",slot},{"materialAsset",asset},{"resolvedSource",material.sourcePath},{"baseTexture",material.texturePath},{"gpuTexture",snapshot.texture.value},{"shader",snapshot.shader.value},{"surface",snapshot.surface},{"tint",snapshot.tint},{"baseColor",snapshot.baseColor}};
            record["maps"]={snapshot.maps[0].value,snapshot.maps[1].value,snapshot.maps[2].value};
            auto key=AssetPaths::NormalizeAssetKey(material.texturePath);auto r=m_ResourceManager->Get<TextureResource>(key);if(r){record["textureIdentity"]=std::uint64_t(reinterpret_cast<std::uintptr_t>(r.get()));record["textureVersion"]=r->GetVersion();record["loadState"]=int(r->GetLoadState());}m_MaterialDiagnostics.push_back(std::move(record));}
        return finish(snapshot.texture.IsValid()&&snapshot.shader.IsValid());
    }

    void RenderSystem::PrepareRenderPacket(
        const RenderSnapshot& snapshot,
        RenderPacket& outPacket) const
    {
        BuildMvp(outPacket.instance.mvp.data(),snapshot.pose,m_RenderCamera,outPacket.instance.model.data());

        outPacket.renderMode=snapshot.renderMode;outPacket.range=snapshot.range;outPacket.materialState=snapshot.materialState;outPacket.castShadows=snapshot.castShadows;
        outPacket.mesh = snapshot.mesh;
        outPacket.texture = snapshot.texture;outPacket.maps=snapshot.maps;
        outPacket.instance.textureParameters=snapshot.textureParameters;
        outPacket.shader = snapshot.shader;
        outPacket.instance.tint = snapshot.tint;
        outPacket.instance.surface=snapshot.surface;outPacket.instance.baseColor=snapshot.baseColor;
        outPacket.instance.normal = BuildNormalMatrix(outPacket.instance.model);
        const auto& m=outPacket.instance.model;
        const double determinant=double(m[0])*(double(m[5])*m[10]-double(m[9])*m[6])
            -double(m[4])*(double(m[1])*m[10]-double(m[9])*m[2])
            +double(m[8])*(double(m[1])*m[6]-double(m[5])*m[2]);
        outPacket.instance.textureParameters[3]=determinant<0?-1.f:1.f;
        outPacket.materialState.reverseWinding=!outPacket.materialState.doubleSided && determinant<0;

    }

    void RenderSystem::SubmitRenderPacket(
        const RenderPacket& packet)
    {
        if (m_Renderer == nullptr)
            return;
        if(m_EnvironmentSettings){
            auto environment=m_EvaluatedPostProcess.environment;if(m_ViewportUnlit)environment.diffuse=environment.specular=false;
            m_Renderer->SetEnvironmentSettings(environment);
            if(m_ResourceManager&&m_EnvironmentSettings->enabled){
                const auto& key=m_EnvironmentSettings->assetPath;
                if(!m_EnvironmentAsset||m_RequestedEnvironment!=key){
                    m_RequestedEnvironment=key;m_EnvironmentAsset=m_ResourceManager->LoadAsync<EnvironmentResource>(key).get();
                    m_ResourceManager->WatchForHotReload<EnvironmentResource>(key);
                }
                auto asset=m_EnvironmentAsset;
                if(asset&&asset->IsLoaded()&&!asset->GetData().placeholder&&(m_EnvironmentKey!=asset->GetKey()||m_EnvironmentVersion!=asset->GetVersion())&&(m_FailedEnvironmentKey!=asset->GetKey()||m_FailedEnvironmentVersion!=asset->GetVersion())){
                    if(m_Renderer->UploadEnvironment(asset->GetData(),asset->GetVersion())){m_EnvironmentKey=asset->GetKey();m_EnvironmentVersion=asset->GetVersion();m_FailedEnvironmentKey.clear();}else{m_FailedEnvironmentKey=asset->GetKey();m_FailedEnvironmentVersion=asset->GetVersion();}
                }
            }
        }

        if (!packet.mesh.IsValid() ||
            !packet.texture.IsValid() ||
            !packet.shader.IsValid())
        {
            return;
        }

        m_Renderer->SetMeshRange(packet.range);m_Renderer->SetMaterialState(packet.materialState);
        m_Renderer->SetTestTransform(packet.instance.mvp.data());
        m_Renderer->SetNormalMatrix(packet.instance.normal);
        m_Renderer->SetModelMatrix(packet.instance.model);
        m_Renderer->SetSurfaceParameters(packet.instance.surface);
        m_Renderer->SetBaseColor(packet.instance.baseColor);
        m_Renderer->SetTextureParameters(packet.instance.textureParameters);

        m_Renderer->SetTestColor(
            packet.instance.tint[0],
            packet.instance.tint[1],
            packet.instance.tint[2],
            packet.instance.tint[3]);

        m_Renderer->BindShader(packet.shader);
        m_Renderer->BindTexture(0, packet.texture);
        for(unsigned i=0;i<3;++i)m_Renderer->BindTexture(i+1,packet.maps[i]);
        m_Renderer->DrawMesh(packet.mesh);
    }

    RenderMeshHandle RenderSystem::GetOrUploadMesh(const std::string& key)
    {
        auto resourceIt = m_MeshResources.find(key);
        auto resource =
            resourceIt != m_MeshResources.end()
            ? resourceIt->second
            : m_ResourceManager->Get<MeshResource>(key);

        if (resource == nullptr)
        {
            (void)m_ResourceManager->LoadAsync<MeshResource>(key);
            resource =
                m_ResourceManager->Get<MeshResource>(key);
        }
        if (resource == nullptr || resource->IsLoading() || !resource->IsUsable())
            return RenderMeshHandle::Invalid();
        auto& mesh = resource->GetData();
        const std::uint64_t version =
            resource->GetVersion();

        if (mesh.gpuHandle.IsValid() &&
            mesh.gpuHandleVersion == version)
        {
            m_MeshResources[key] = resource;
            return mesh.gpuHandle;
        }

        QueueGpuFinalization(
            GpuFinalizeKind::Mesh,
            key,
            version);

        return RenderMeshHandle::Invalid();
    }

    RenderTextureHandle RenderSystem::GetOrCreateTexture(
        const std::string& key,bool optionalMap)
    {
        ResourceHandle<TextureResource> resource;

        const auto resourceIt = m_TextureResources.find(key);

        if (resourceIt != m_TextureResources.end())
        {
            resource = resourceIt->second;
        }
        else if (key == "defaults/texture")
        {
            resource =
                m_ResourceManager->GetDefault<TextureResource>();
        }
        else
        {
            resource =
                m_ResourceManager->Get<TextureResource>(key);

            if (resource == nullptr)
            {
                (void)m_ResourceManager
                    ->LoadAsync<TextureResource>(key);

                resource =
                    m_ResourceManager->Get<TextureResource>(key);
            }
        }

        // Keep loading/failed owners too: an Image must not revalidate its path
        // every frame while loading or displaying a missing-texture diagnostic.
        if(resource)m_TextureResources[key]=resource;
        if (resource == nullptr || !resource->IsUsable() || (optionalMap && resource->IsFailed()))
            return RenderTextureHandle::Invalid();

        auto& texture = resource->GetData();
        const std::uint64_t version = resource->GetVersion();

        if (texture.gpuHandle.IsValid() &&
            texture.gpuHandleVersion == version)
        {
            m_TextureResources[key] = resource;
            return texture.gpuHandle;
        }

        QueueGpuFinalization(
            GpuFinalizeKind::Texture,
            key,
            version);

        return RenderTextureHandle::Invalid();
    }

    RenderShaderHandle RenderSystem::GetOrCreateShader(const std::string& key)
    {
        auto resourceIt = m_ShaderResources.find(key);
        auto resource =
            resourceIt != m_ShaderResources.end()
            ? resourceIt->second
            : m_ResourceManager->Get<ShaderResource>(key);

        if (resource == nullptr)
        {
            (void)m_ResourceManager->LoadAsync<ShaderResource>(key);
            resource =
                m_ResourceManager->Get<ShaderResource>(key);
        }

        if (resource == nullptr || resource->IsLoading() || !resource->IsUsable())
            return RenderShaderHandle::Invalid();
        auto& shader = resource->GetData();
        const std::uint64_t version =
            resource->GetVersion();

        if (shader.gpuHandle.IsValid() &&
            shader.gpuHandleVersion == version)
        {
            m_ShaderResources[key] = resource;
            return shader.gpuHandle;
        }

        QueueGpuFinalization(
            GpuFinalizeKind::Shader,
            key,
            version);

        return RenderShaderHandle::Invalid();
    }

    ResourceHandle<MaterialResource> RenderSystem::GetOrLoadMaterial(const std::string& key)
    {
        const auto cached = m_MaterialResources.find(key);
        if (cached != m_MaterialResources.end())
            return cached->second;

        auto resource =
            m_ResourceManager->Get<MaterialResource>(key);

        if (resource == nullptr)
        {
            (void)m_ResourceManager->LoadAsync<MaterialResource>(key);
            resource =
                m_ResourceManager->Get<MaterialResource>(key);
        }

        m_FailedMaterialKeys.erase(key);

        m_MaterialResources[key] = resource;
        Logger::Get().Info("RenderSystem: loaded material resource key=" + key);
        return resource;
    }

    std::string MakeGpuFinalizeToken(
        int kind,
        const std::string& key,
        std::uint64_t version)
    {
        return std::to_string(kind) +
            "#" + key +
            "#" + std::to_string(version);
    }

    void RenderSystem::QueueGpuFinalization(
        GpuFinalizeKind kind,
        const std::string& key,
        std::uint64_t version)
    {
        const std::string token =
            MakeGpuFinalizeToken(
                static_cast<int>(kind),
                key,
                version);

        if (!m_PendingGpuFinalizations.insert(token).second)
            return;

        m_GpuFinalizeQueue.push_back(
            GpuFinalizeRequest{
                kind,
                key,
                version
            });
    }

    std::size_t RenderSystem::PumpGpuFinalization(
        std::size_t maxItems)
    {
        ZoneScopedN("GpuFinalizePump");

        std::size_t processed = 0;

        while (processed < maxItems &&
            !m_GpuFinalizeQueue.empty())
        {
            GpuFinalizeRequest request =
                std::move(m_GpuFinalizeQueue.front());

            m_GpuFinalizeQueue.pop_front();

            const std::string token =
                MakeGpuFinalizeToken(
                    static_cast<int>(request.kind),
                    request.key,
                    request.version);

            m_PendingGpuFinalizations.erase(token);

            switch (request.kind)
            {
            case GpuFinalizeKind::Mesh:
                FinalizeMeshGpu(
                    request.key,
                    request.version);
                break;

            case GpuFinalizeKind::Texture:
                FinalizeTextureGpu(
                    request.key,
                    request.version);
                break;

            case GpuFinalizeKind::Shader:
                FinalizeShaderGpu(
                    request.key,
                    request.version);
                break;
            }

            ++processed;
        }

        return processed;
    }

    void RenderSystem::FinalizeMeshGpu(
        const std::string& key,
        std::uint64_t requestedVersion)
    {
        ZoneScopedN("GpuFinalizeMesh");

        if (m_Renderer == nullptr ||
            m_ResourceManager == nullptr)
        {
            return;
        }

        auto resource =
            m_ResourceManager->Get<MeshResource>(key);

        if (resource == nullptr ||
            !resource->IsUsable())
        {
            return;
        }

        const std::uint64_t currentVersion =
            resource->GetVersion();

        // ���� ������ ���� � �������, CPU resource ��� ����������.
        if (currentVersion != requestedVersion)
        {
            QueueGpuFinalization(
                GpuFinalizeKind::Mesh,
                key,
                currentVersion);

            return;
        }

        if (resource == nullptr || resource->IsLoading() || !resource->IsUsable())
            return;
        auto& mesh = resource->GetData();

        if (mesh.gpuHandle.IsValid() &&
            mesh.gpuHandleVersion == currentVersion)
        {
            return;
        }

        if (mesh.gpuHandle.IsValid())
        {
            m_Renderer->DestroyMesh(mesh.gpuHandle);
            mesh.gpuHandle = RenderMeshHandle::Invalid();
            mesh.gpuHandleVersion = 0;
        }

        const RenderMeshHandle handle =
            m_Renderer->UploadMesh(mesh.meshData);

        if (!handle.IsValid())
        {
            m_FailedMeshGpuVersions[key] =
                currentVersion;

            return;
        }

        mesh.gpuHandle = handle;
        mesh.gpuHandleVersion = currentVersion;

        m_MeshResources[key] = resource;

        m_FailedMeshKeys.erase(key);
        m_FailedMeshGpuVersions.erase(key);

        Logger::Get().Info(
            "RenderSystem: GPU finalized mesh key=" +
            key);
    }

    void RenderSystem::FinalizeTextureGpu(
        const std::string& key,
        std::uint64_t requestedVersion)
    {
        ZoneScopedN("GpuFinalizeTexture");

        if (m_Renderer == nullptr ||
            m_ResourceManager == nullptr)
        {
            return;
        }

        ResourceHandle<TextureResource> resource;

        if (key == "defaults/texture")
        {
            resource =
                m_ResourceManager->GetDefault<TextureResource>();
        }
        else
        {
            resource =
                m_ResourceManager->Get<TextureResource>(key);
        }

        if (resource == nullptr ||
            !resource->IsUsable())
        {
            return;
        }

        const std::uint64_t currentVersion =
            resource->GetVersion();

        if (currentVersion != requestedVersion)
        {
            QueueGpuFinalization(
                GpuFinalizeKind::Texture,
                key,
                currentVersion);

            return;
        }

        auto& texture = resource->GetData();

        if (texture.gpuHandle.IsValid() &&
            texture.gpuHandleVersion == currentVersion)
        {
            return;
        }

        if (texture.gpuHandle.IsValid())
        {
            m_Renderer->DestroyTexture(texture.gpuHandle);
            texture.gpuHandle =
                RenderTextureHandle::Invalid();

            texture.gpuHandleVersion = 0;
        }

        const RenderTextureHandle handle =
            m_Renderer->CreateTexture2D(
                texture.textureData);

        if (!handle.IsValid())
        {
            m_FailedTextureGpuVersions[key] =
                currentVersion;

            return;
        }

        texture.gpuHandle = handle;
        texture.gpuHandleVersion = currentVersion;

        m_TextureResources[key] = resource;

        m_FailedTextureKeys.erase(key);
        m_FailedTextureGpuVersions.erase(key);

        Logger::Get().Info(
            "RenderSystem: GPU finalized texture key=" +
            key);
    }

    void RenderSystem::FinalizeShaderGpu(
        const std::string& key,
        std::uint64_t requestedVersion)
    {
        ZoneScopedN("GpuFinalizeShader");

        if (m_Renderer == nullptr ||
            m_ResourceManager == nullptr)
        {
            return;
        }

        auto resource =
            m_ResourceManager->Get<ShaderResource>(key);

        if (resource == nullptr ||
            !resource->IsUsable())
        {
            return;
        }

        const std::uint64_t currentVersion =
            resource->GetVersion();

        if (currentVersion != requestedVersion)
        {
            QueueGpuFinalization(
                GpuFinalizeKind::Shader,
                key,
                currentVersion);

            return;
        }

        if (resource == nullptr || resource->IsLoading() || !resource->IsUsable())
            return;
        auto& shader = resource->GetData();

        if (shader.gpuHandle.IsValid() &&
            shader.gpuHandleVersion == currentVersion)
        {
            return;
        }

        if (shader.gpuHandle.IsValid())
        {
            m_Renderer->DestroyShader(
                shader.gpuHandle);

            shader.gpuHandle =
                RenderShaderHandle::Invalid();

            shader.gpuHandleVersion = 0;
        }

        const RenderShaderHandle handle =
            m_Renderer->CreateShaderProgram(shader);

        if (!handle.IsValid())
        {
            m_FailedShaderGpuVersions[key] =
                currentVersion;

            return;
        }

        shader.gpuHandle = handle;
        shader.gpuHandleVersion = currentVersion;

        m_ShaderResources[key] = resource;

        m_FailedShaderKeys.erase(key);
        m_FailedShaderGpuVersions.erase(key);

        Logger::Get().Info(
            "RenderSystem: GPU finalized shader key=" +
            key);
    }
}

ui::DrawList ecs::RenderSystem::BuildUIDesignerPreview(const ui::Document& document,ui::Pair preview,IRenderAdapter* renderer){
 struct RestoreAdapter {IRenderAdapter*& field;IRenderAdapter* previous;~RestoreAdapter(){field=previous;}} restore{m_Renderer,m_Renderer};
 SetRenderAdapter(renderer);
 if(!m_Renderer||!m_ResourceManager)return {};
 if(!m_UIFont)m_UIFont=ui::RasterizeFont();
 if(!m_UIFontTexture.IsValid())m_UIFontTexture=m_Renderer->CreateTexture2D(m_UIFont->texture);
 PumpGpuFinalization(1);
 return ui::LayoutDocument(m_RuntimeUI.Preview(document,*m_ResourceManager),preview[0],preview[1],*m_UIFont,m_UIFontTexture,[&](const std::string& key){
  if(key.empty())return RenderTextureHandle{};
  auto cached=m_TextureResources.find(key);if(cached!=m_TextureResources.end()&&cached->second!=m_ResourceManager->Get<TextureResource>(key)){auto& data=cached->second->GetData();if(data.gpuHandle.IsValid()){m_Renderer->DestroyTexture(data.gpuHandle);data.gpuHandle={};data.gpuHandleVersion=0;}m_TextureResources.erase(cached);m_FailedTextureGpuVersions.erase(key);}
  return GetOrCreateTexture(key,true);
 },[&](const std::string& key){return m_UIFonts.Get(key,*m_ResourceManager,m_Renderer);});
}
