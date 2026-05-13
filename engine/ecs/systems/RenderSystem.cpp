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

void BuildScaleMatrix(const ecs::Vec3& scale, float* out16)
{
    SetIdentity(out16);
    out16[0] = scale.x;
    out16[5] = scale.y;
    out16[10] = scale.z;
}

void BuildRotationX(float angle, float* out16)
{
    SetIdentity(out16);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    out16[5] = c;
    out16[6] = s;
    out16[9] = -s;
    out16[10] = c;
}

void BuildRotationY(float angle, float* out16)
{
    SetIdentity(out16);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    out16[0] = c;
    out16[2] = -s;
    out16[8] = s;
    out16[10] = c;
}

void BuildRotationZ(float angle, float* out16)
{
    SetIdentity(out16);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    out16[0] = c;
    out16[1] = s;
    out16[4] = -s;
    out16[5] = c;
}

void BuildTranslationMatrix(const ecs::Vec3& position, float* out16)
{
    SetIdentity(out16);
    out16[12] = position.x;
    out16[13] = position.y;
    out16[14] = position.z;
}

void BuildModelMatrix(float* out16, const ecs::TransformComponent& transform)
{
    float scaleMatrix[16];
    float rotateX[16];
    float rotateY[16];
    float rotateZ[16];
    float translation[16];
    float temp0[16];
    float temp1[16];
    float temp2[16];

    BuildScaleMatrix(transform.scale, scaleMatrix);
    BuildRotationX(transform.rotation.x, rotateX);
    BuildRotationY(transform.rotation.y, rotateY);
    BuildRotationZ(transform.rotation.z, rotateZ);
    BuildTranslationMatrix(transform.position, translation);

    MultiplyMatrix(rotateX, scaleMatrix, temp0);
    MultiplyMatrix(rotateY, temp0, temp1);
    MultiplyMatrix(rotateZ, temp1, temp2);
    MultiplyMatrix(translation, temp2, out16);
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
    const ecs::TransformComponent& transform,
    const ecs::Vec3& cameraPosition,
    float cameraYaw,
    float cameraPitch,
    float verticalFovRadians,
    float aspectRatio,
    float nearPlane,
    float farPlane)
{
    float modelMatrix[16];
    float viewMatrix[16];
    float projectionMatrix[16];
    float viewModel[16];

    BuildModelMatrix(modelMatrix, transform);
    BuildViewMatrix(viewMatrix, cameraPosition, cameraYaw, cameraPitch);
    BuildPerspectiveMatrix(projectionMatrix, verticalFovRadians, aspectRatio, nearPlane, farPlane);
    MultiplyMatrix(viewMatrix, modelMatrix, viewModel);
    MultiplyMatrix(projectionMatrix, viewModel, out16);
}
}

namespace ecs
{
RenderSystem::~RenderSystem()
{
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
    (void)dt;
    if (m_Renderer == nullptr)
        return;

    world.ForEach<TransformComponent, MeshRendererComponent>(
        [&](Entity entity, TransformComponent& transform, MeshRendererComponent& meshRenderer)
        {
            if (!meshRenderer.visible)
                return;

            (void)TryDrawResourceMesh(
                transform,
                meshRenderer,
                world.GetComponent<MaterialComponent>(entity));
        });

    if (!m_DebugCollidersEnabled)
        return;

    world.ForEach<TransformComponent, ColliderComponent>(
        [&](Entity, TransformComponent& transform, ColliderComponent& collider)
        {
            if (collider.type == ColliderType::Sphere)
                return; // avoid misleading cube proxy for sphere collider debug

            float mvp[16];
            TransformComponent debugTransform = transform;
            debugTransform.position.x += collider.offset.x;
            debugTransform.position.y += collider.offset.y;
            debugTransform.position.z += collider.offset.z;
            // Box colliders can be oriented; keep rotation for box debug draw.
            debugTransform.scale = ecs::Vec3{
                collider.halfExtents.x * 2.0f,
                collider.halfExtents.y * 2.0f,
                collider.halfExtents.z * 2.0f
            };
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

bool RenderSystem::TryDrawResourceMesh(
    const TransformComponent& transform,
    const MeshRendererComponent& meshRenderer,
    const MaterialComponent* materialComponent)
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

    float mvp[16];
    BuildMvp(
        mvp,
        transform,
        m_CameraPosition,
        m_CameraYaw,
        m_CameraPitch,
        m_CameraVerticalFovRadians,
        m_CameraAspectRatio,
        m_CameraNearPlane,
        m_CameraFarPlane);
    m_Renderer->SetTestTransform(mvp);
    m_Renderer->SetTestColor(
        tint[0],
        tint[1],
        tint[2],
        tint[3]);
    m_Renderer->BindShader(shaderHandle);
    m_Renderer->BindTexture(0, textureHandle);
    m_Renderer->DrawMesh(meshHandle);
    return true;
}

RenderMeshHandle RenderSystem::GetOrUploadMesh(const std::string& key)
{
    auto resourceIt = m_MeshResources.find(key);
    auto resource = resourceIt != m_MeshResources.end() ? resourceIt->second : m_ResourceManager->Load<MeshResource>(key);
    if (resource == nullptr || !resource->IsUsable())
    {
        m_FailedMeshKeys.insert(key);
        return RenderMeshHandle::Invalid();
    }
    auto& mesh = resource->GetData();
    const std::uint64_t version = resource->GetVersion();
    if (mesh.gpuHandle.IsValid() && mesh.gpuHandleVersion != resource->GetVersion())
    {
        m_Renderer->DestroyMesh(mesh.gpuHandle);
        mesh.gpuHandle = RenderMeshHandle::Invalid();
        mesh.gpuHandleVersion = 0;
        m_FailedMeshGpuVersions.erase(key);
        m_LoggedMeshReuseKeys.erase(key);
        Logger::Get().Info("RenderSystem: mesh resource version changed, reuploading key=" + key);
    }
    if (mesh.gpuHandle.IsValid() && !m_LoggedMeshReuseKeys.contains(key))
    {
        Logger::Get().Info(
            "RenderSystem: reusing mesh GPU handle key=" + key +
            " handle=" + std::to_string(mesh.gpuHandle.value));
        m_LoggedMeshReuseKeys.insert(key);
    }
    if (mesh.gpuHandle.IsValid())
    {
        m_FailedMeshKeys.erase(key);
        m_FailedMeshGpuVersions.erase(key);
        m_MeshResources[key] = resource;
        return mesh.gpuHandle;
    }
    if (m_FailedMeshKeys.contains(key))
        return RenderMeshHandle::Invalid();
    if (const auto failedVersion = m_FailedMeshGpuVersions.find(key);
        failedVersion != m_FailedMeshGpuVersions.end() && failedVersion->second == version)
    {
        return RenderMeshHandle::Invalid();
    }

    const RenderMeshHandle handle = m_Renderer->UploadMesh(mesh.meshData);
    if (!handle.IsValid())
    {
        m_FailedMeshGpuVersions[key] = version;
        return RenderMeshHandle::Invalid();
    }

    mesh.gpuHandle = handle;
    mesh.gpuHandleVersion = resource->GetVersion();
    m_FailedMeshKeys.erase(key);
    m_FailedMeshGpuVersions.erase(key);
    m_MeshResources[key] = resource;
    Logger::Get().Info("RenderSystem: uploaded mesh resource key=" + key);
    return mesh.gpuHandle;
}

RenderTextureHandle RenderSystem::GetOrCreateTexture(const std::string& key)
{
    auto resourceIt = m_TextureResources.find(key);
    auto resource = resourceIt != m_TextureResources.end() ? resourceIt->second : m_ResourceManager->Load<TextureResource>(key);
    if (resource == nullptr || !resource->IsUsable())
    {
        m_FailedTextureKeys.insert(key);
        return RenderTextureHandle::Invalid();
    }
    auto& texture = resource->GetData();
    const std::uint64_t version = resource->GetVersion();
    if (texture.gpuHandle.IsValid() && texture.gpuHandleVersion != resource->GetVersion())
    {
        m_Renderer->DestroyTexture(texture.gpuHandle);
        texture.gpuHandle = RenderTextureHandle::Invalid();
        texture.gpuHandleVersion = 0;
        m_FailedTextureGpuVersions.erase(key);
        m_LoggedTextureReuseKeys.erase(key);
        Logger::Get().Info("RenderSystem: texture resource version changed, reuploading key=" + key);
    }
    if (texture.gpuHandle.IsValid() && !m_LoggedTextureReuseKeys.contains(key))
    {
        Logger::Get().Info(
            "RenderSystem: reusing texture GPU handle key=" + key +
            " handle=" + std::to_string(texture.gpuHandle.value));
        m_LoggedTextureReuseKeys.insert(key);
    }
    if (texture.gpuHandle.IsValid())
    {
        m_FailedTextureKeys.erase(key);
        m_FailedTextureGpuVersions.erase(key);
        m_TextureResources[key] = resource;
        return texture.gpuHandle;
    }
    if (m_FailedTextureKeys.contains(key))
        return RenderTextureHandle::Invalid();
    if (const auto failedVersion = m_FailedTextureGpuVersions.find(key);
        failedVersion != m_FailedTextureGpuVersions.end() && failedVersion->second == version)
    {
        return RenderTextureHandle::Invalid();
    }

    const RenderTextureHandle handle = m_Renderer->CreateTexture2D(texture.textureData);
    if (!handle.IsValid())
    {
        m_FailedTextureGpuVersions[key] = version;
        return RenderTextureHandle::Invalid();
    }

    texture.gpuHandle = handle;
    texture.gpuHandleVersion = resource->GetVersion();
    m_FailedTextureKeys.erase(key);
    m_FailedTextureGpuVersions.erase(key);
    m_TextureResources[key] = resource;
    Logger::Get().Info("RenderSystem: uploaded texture resource key=" + key);
    return texture.gpuHandle;
}

RenderShaderHandle RenderSystem::GetOrCreateShader(const std::string& key)
{
    auto resourceIt = m_ShaderResources.find(key);
    auto resource = resourceIt != m_ShaderResources.end() ? resourceIt->second : m_ResourceManager->Load<ShaderResource>(key);
    if (resource == nullptr || !resource->IsUsable())
    {
        m_FailedShaderKeys.insert(key);
        return RenderShaderHandle::Invalid();
    }
    auto& shader = resource->GetData();
    const std::uint64_t version = resource->GetVersion();
    if (shader.gpuHandle.IsValid() && shader.gpuHandleVersion != resource->GetVersion())
    {
        m_Renderer->DestroyShader(shader.gpuHandle);
        shader.gpuHandle = RenderShaderHandle::Invalid();
        shader.gpuHandleVersion = 0;
        m_FailedShaderGpuVersions.erase(key);
        m_LoggedShaderReuseKeys.erase(key);
        Logger::Get().Info("RenderSystem: shader resource version changed, recreating key=" + key);
    }
    if (shader.gpuHandle.IsValid() && !m_LoggedShaderReuseKeys.contains(key))
    {
        Logger::Get().Info(
            "RenderSystem: reusing shader GPU handle key=" + key +
            " handle=" + std::to_string(shader.gpuHandle.value));
        m_LoggedShaderReuseKeys.insert(key);
    }
    if (shader.gpuHandle.IsValid())
    {
        m_FailedShaderKeys.erase(key);
        m_FailedShaderGpuVersions.erase(key);
        m_ShaderResources[key] = resource;
        return shader.gpuHandle;
    }
    if (m_FailedShaderKeys.contains(key))
        return RenderShaderHandle::Invalid();
    if (const auto failedVersion = m_FailedShaderGpuVersions.find(key);
        failedVersion != m_FailedShaderGpuVersions.end() && failedVersion->second == version)
    {
        return RenderShaderHandle::Invalid();
    }

    const RenderShaderHandle handle = m_Renderer->CreateShaderProgram(shader);
    if (!handle.IsValid())
    {
        m_FailedShaderGpuVersions[key] = version;
        return RenderShaderHandle::Invalid();
    }

    shader.gpuHandle = handle;
    shader.gpuHandleVersion = resource->GetVersion();
    m_FailedShaderKeys.erase(key);
    m_FailedShaderGpuVersions.erase(key);
    m_ShaderResources[key] = resource;
    Logger::Get().Info("RenderSystem: created shader resource key=" + key);
    return shader.gpuHandle;
}

ResourceHandle<MaterialResource> RenderSystem::GetOrLoadMaterial(const std::string& key)
{
    const auto cached = m_MaterialResources.find(key);
    if (cached != m_MaterialResources.end())
        return cached->second;

    auto resource = m_ResourceManager->Load<MaterialResource>(key);
    if (resource == nullptr || !resource->IsUsable())
    {
        m_FailedMaterialKeys.insert(key);
        return nullptr;
    }
    m_FailedMaterialKeys.erase(key);

    m_MaterialResources[key] = resource;
    Logger::Get().Info("RenderSystem: loaded material resource key=" + key);
    return resource;
}
}
