#include "RenderSystem.h"

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
#include <chrono>
#include <tracy/Tracy.hpp>

namespace
{
constexpr float kWhiteTint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

float Dot(const ecs::Vec3& lhs, const ecs::Vec3& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

ecs::Vec3 Cross(const ecs::Vec3& lhs, const ecs::Vec3& rhs)
{
    return ecs::Vec3{
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x
    };
}

float Length(const ecs::Vec3& vector)
{
    return std::sqrt(Dot(vector, vector));
}

ecs::Vec3 Normalize(const ecs::Vec3& vector)
{
    const float length = Length(vector);
    if (length <= 0.0001f)
        return ecs::Vec3{};

    const float invLength = 1.0f / length;
    return ecs::Vec3{ vector.x * invLength, vector.y * invLength, vector.z * invLength };
}

ecs::Vec3 BuildCameraForward(float yaw, float pitch)
{
    const float cosPitch = std::cos(pitch);
    return Normalize(ecs::Vec3{
        std::sin(yaw) * cosPitch,
        std::sin(pitch),
        std::cos(yaw) * cosPitch
    });
}

void SetIdentity(float* out16)
{
    for (int i = 0; i < 16; ++i)
        out16[i] = 0.0f;

    out16[0] = 1.0f;
    out16[5] = 1.0f;
    out16[10] = 1.0f;
    out16[15] = 1.0f;
}

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

void BuildViewMatrix(
    float* out16,
    const ecs::Vec3& cameraPosition,
    float yaw,
    float pitch)
{
    const ecs::Vec3 worldUp{ 0.0f, 1.0f, 0.0f };
    const ecs::Vec3 forward = BuildCameraForward(yaw, pitch);
    const ecs::Vec3 right = Normalize(Cross(worldUp, forward));
    const ecs::Vec3 up = Cross(forward, right);

    // View matrix is the inverse of the camera transform, so the camera basis
    // must be transposed relative to the world-space camera axes.
    SetIdentity(out16);
    out16[0] = right.x;
    out16[1] = up.x;
    out16[2] = forward.x;
    out16[4] = right.y;
    out16[5] = up.y;
    out16[6] = forward.y;
    out16[8] = right.z;
    out16[9] = up.z;
    out16[10] = forward.z;
    out16[12] = -Dot(right, cameraPosition);
    out16[13] = -Dot(up, cameraPosition);
    out16[14] = -Dot(forward, cameraPosition);
}

void BuildPerspectiveMatrix(
    float* out16,
    float verticalFovRadians,
    float aspectRatio,
    float nearPlane,
    float farPlane)
{
    const float yScale = 1.0f / std::tan(verticalFovRadians * 0.5f);
    const float xScale = yScale / aspectRatio;
    const float depthRange = farPlane - nearPlane;

    for (int i = 0; i < 16; ++i)
        out16[i] = 0.0f;

    out16[0] = xScale;
    out16[5] = yScale;
    out16[10] = farPlane / depthRange;
    out16[11] = 1.0f;
    out16[14] = -(nearPlane * farPlane) / depthRange;
}

void BuildMvp(
    float* out16,
    const ecs::RenderPose& pose,
    const ecs::Vec3& cameraPosition,
    float cameraYaw,
    float cameraPitch,
    float verticalFovRadians,
    float aspectRatio,
    float nearPlane,
    float farPlane, float* outModel = nullptr)
{
    float modelMatrix[16];
    float viewMatrix[16];
    float projectionMatrix[16];
    float viewModel[16];

    ecs::BuildRenderModelMatrix(modelMatrix, pose);
    if(outModel) std::copy(modelMatrix, modelMatrix+16, outModel);
    BuildViewMatrix(viewMatrix, cameraPosition, cameraYaw, cameraPitch);
    BuildPerspectiveMatrix(projectionMatrix, verticalFovRadians, aspectRatio, nearPlane, farPlane);
    MultiplyMatrix(viewMatrix, modelMatrix, viewModel);
    MultiplyMatrix(projectionMatrix, viewModel, out16);
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

    void RenderSystem::SetCameraTransform(const Vec3& position, float yawRadians, float pitchRadians)
    {
        m_CameraPosition = position;
        m_CameraYaw = yawRadians;
        m_CameraPitch = pitchRadians;
    }

    void RenderSystem::SetCameraProjection(
        float verticalFovRadians,
        float aspectRatio,
        float nearPlane,
        float farPlane)
    {
        m_CameraVerticalFovRadians = verticalFovRadians > 0.001f ? verticalFovRadians : 1.04719755f;
        m_CameraAspectRatio = aspectRatio > 0.001f ? aspectRatio : 16.0f / 9.0f;
        m_CameraNearPlane = nearPlane > 0.0001f ? nearPlane : 0.01f;
        m_CameraFarPlane =
            farPlane > m_CameraNearPlane + 0.001f
            ? farPlane
            : m_CameraNearPlane + 0.001f;
    }

    void RenderSystem::ReleaseGpuResources()
    {
        IRenderAdapter* renderer = m_ResourceOwnerRenderer != nullptr ? m_ResourceOwnerRenderer : m_Renderer;
        if (renderer != nullptr)
        {
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

        m_MeshResources.clear();
        m_TextureResources.clear();
        m_ShaderResources.clear();
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
        m_ResourceOwnerRenderer = nullptr;
    }

    void RenderSystem::Update(World& world, float dt)
    {
        ZoneScopedN("RenderSystem");

        (void)dt;
        m_Statistics = {};

        if (m_Renderer == nullptr)
            return;

        constexpr std::size_t
            maxGpuFinalizationsPerFrame = 1;

        PumpGpuFinalization(
            maxGpuFinalizationsPerFrame);

        const auto submittedBefore = m_Renderer->GetSubmissionStatistics();
        const auto elapsed = [](auto start) { return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(); };
        {
            ZoneScopedN("RenderEntities");

            m_RenderSnapshots.clear();
            m_RenderSnapshots.reserve(world.GetAliveCount());

            {
                ZoneScopedN("RenderGather");
                const auto start = std::chrono::steady_clock::now();

                world.ForEach<TransformComponent, MeshRendererComponent>(
                    [&](Entity entity,
                        TransformComponent& transform,
                        MeshRendererComponent& meshRenderer)
                    {
                        if (!meshRenderer.visible)
                            return;

                        RenderSnapshot snapshot{};

                        if (TryBuildRenderSnapshot(
                            meshRenderer,
                            world.GetComponent<MaterialComponent>(entity),
                            snapshot))
                        {
                            {
                                ZoneScopedN("RenderInterpolationResolve");
                                snapshot.pose = m_Interpolation ? m_Interpolation->Resolve(entity, transform) : RenderPose::From(transform);
                            }
                            m_RenderSnapshots.push_back(std::move(snapshot));
                        }
                    });
                m_Statistics.gatherMs = elapsed(start);
                m_Statistics.visibleObjects = m_RenderSnapshots.size();
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
                // Consecutive compatible packets only: retain all draw/pass ordering.
                for (std::size_t i=0;i<m_RenderPackets.size();++i) {
                    m_InstanceData[i]=m_RenderPackets[i].instance;
                    const auto& packet=m_RenderPackets[i];
                    if(i>0 && packet.mesh==m_RenderPackets[i-1].mesh &&
                        packet.texture==m_RenderPackets[i-1].texture && packet.shader==m_RenderPackets[i-1].shader)
                        ++m_Batches.back().count;
                    else m_Batches.push_back({i,1});
                }
                m_Statistics.batchMs=elapsed(start);
            }
            {
                ZoneScopedN("RenderSubmit");
                const auto start = std::chrono::steady_clock::now();
                for(const auto& batch:m_Batches) {
                    const auto& packet=m_RenderPackets[batch.begin];
                    if(m_InstancingEnabled && batch.count>1 && m_Renderer->SupportsInstancing(packet.shader)) {
                        m_Renderer->BindShader(packet.shader);
                        m_Renderer->BindTexture(0,packet.texture);
                        if(m_Renderer->DrawMeshInstanced(packet.mesh,std::span<const RenderInstanceData>(m_InstanceData.data()+batch.begin,batch.count)))continue;
                    }
                    ZoneScopedN("RenderFallbackDraw");
                    for(std::size_t i=batch.begin;i<batch.begin+batch.count;++i)SubmitRenderPacket(m_RenderPackets[i]);
                }
                m_Statistics.submitMs=elapsed(start);
            }
        }

        const auto finishStatistics = [&] {
        const auto submitted = m_Renderer->GetSubmissionStatistics();
        m_Statistics.submitted=submitted;
        m_Statistics.submitted.drawCalls-=submittedBefore.drawCalls;
        m_Statistics.submitted.instancedDrawCalls-=submittedBefore.instancedDrawCalls;
        m_Statistics.submitted.renderedInstances-=submittedBefore.renderedInstances;
        m_Statistics.submitted.uploadBytes-=submittedBefore.uploadBytes;
        m_Statistics.submitted.instanceBufferGrowths-=submittedBefore.instanceBufferGrowths;
        };
        if (!m_DebugCollidersEnabled) { finishStatistics(); return; }
        {
            ZoneScopedN("DebugColliders");

            world.ForEach<TransformComponent, ColliderComponent>(
                [&](Entity entity, TransformComponent& transform, ColliderComponent& collider)
                {
                    float mvp[16];
                    RenderPose debugTransform = m_Interpolation && !m_PhysicsDebugPose ? m_Interpolation->Resolve(entity, transform) : RenderPose::From(transform);
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
                    BuildMvp(
                        mvp,
                        debugTransform,
                        m_CameraPosition,
                        m_CameraYaw,
                        m_CameraPitch,
                        m_CameraVerticalFovRadians,
                        m_CameraAspectRatio,
                        m_CameraNearPlane,
                        m_CameraFarPlane);
                    m_Renderer->SetTestTransform(mvp);
                    m_Renderer->SetTestColor(0.1f, 1.0f, 0.1f, 1.0f);
                    m_Renderer->DrawTestCube();
                });
        }
        finishStatistics();
    }

    bool RenderSystem::TryBuildRenderSnapshot(
        const MeshRendererComponent& meshRenderer,
        const MaterialComponent* materialComponent,
        RenderSnapshot& outSnapshot)
    {
        if (m_Renderer == nullptr || m_ResourceManager == nullptr)
            return false;

        if (meshRenderer.meshPath.empty())
            return false;

        std::string texturePath = meshRenderer.texturePath;
        std::string shaderPath = meshRenderer.shaderPath;
        float tint[4] = { kWhiteTint[0], kWhiteTint[1], kWhiteTint[2], kWhiteTint[3] };

        if (materialComponent != nullptr)
        {
            if (!materialComponent->texturePath.empty())
                texturePath = materialComponent->texturePath;
            if (!materialComponent->shaderPath.empty())
                shaderPath = materialComponent->shaderPath;
            for (std::size_t i = 0; i < 4; ++i)
                tint[i] *= materialComponent->tint[i];

            if (!materialComponent->materialPath.empty())
            {
                const std::string materialKey = AssetPaths::NormalizeAssetKey(materialComponent->materialPath);
                const auto material = materialKey.empty() ? nullptr : GetOrLoadMaterial(materialKey);
                if (material != nullptr && material->IsUsable())
                {
                    const auto& data = material->GetData();
                    if (texturePath.empty())
                        texturePath = data.texturePath;
                    if (shaderPath.empty())
                        shaderPath = data.shaderPath;
                    tint[0] *= data.baseColor[0];
                    tint[1] *= data.baseColor[1];
                    tint[2] *= data.baseColor[2];
                    tint[3] *= data.baseColor[3];
                }
            }
        }

        if (shaderPath.empty())
            return false;

        const std::string meshKey = AssetPaths::NormalizeAssetKey(meshRenderer.meshPath);
        const std::string shaderKey = AssetPaths::NormalizeShaderKey(shaderPath);
        if (meshKey.empty() || shaderKey.empty())
            return false;

        const RenderMeshHandle meshHandle = GetOrUploadMesh(meshKey);
        RenderTextureHandle textureHandle = RenderTextureHandle::Invalid();
        if (!texturePath.empty())
        {
            const std::string textureKey = AssetPaths::NormalizeAssetKey(texturePath);
            if (textureKey.empty())
                return false;
            textureHandle = GetOrCreateTexture(textureKey);
        }
        else
        {
            textureHandle = GetOrCreateTexture("defaults/texture");
        }
        const RenderShaderHandle shaderHandle = GetOrCreateShader(shaderKey);
        if (!meshHandle.IsValid() || !textureHandle.IsValid() || !shaderHandle.IsValid())
            return false;


        outSnapshot.mesh = meshHandle;
        outSnapshot.texture = textureHandle;
        outSnapshot.shader = shaderHandle;

        outSnapshot.tint =
        {
            tint[0],
            tint[1],
            tint[2],
            tint[3]
        };

        return true;
    }

    void RenderSystem::PrepareRenderPacket(
        const RenderSnapshot& snapshot,
        RenderPacket& outPacket) const
    {
        BuildMvp(
            outPacket.instance.mvp.data(),
            snapshot.pose,
            m_CameraPosition,
            m_CameraYaw,
            m_CameraPitch,
            m_CameraVerticalFovRadians,
            m_CameraAspectRatio,
            m_CameraNearPlane,
            m_CameraFarPlane, outPacket.instance.model.data());

        outPacket.mesh = snapshot.mesh;
        outPacket.texture = snapshot.texture;
        outPacket.shader = snapshot.shader;
        outPacket.instance.tint = snapshot.tint;
    }

    void RenderSystem::SubmitRenderPacket(
        const RenderPacket& packet)
    {
        if (m_Renderer == nullptr)
            return;

        if (!packet.mesh.IsValid() ||
            !packet.texture.IsValid() ||
            !packet.shader.IsValid())
        {
            return;
        }

        m_Renderer->SetTestTransform(packet.instance.mvp.data());

        m_Renderer->SetTestColor(
            packet.instance.tint[0],
            packet.instance.tint[1],
            packet.instance.tint[2],
            packet.instance.tint[3]);

        m_Renderer->BindShader(packet.shader);
        m_Renderer->BindTexture(0, packet.texture);
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
        const std::string& key)
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

        if (resource == nullptr || !resource->IsUsable())
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

        // ѕока запрос ждал в очереди, CPU resource мог обновитьс€.
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