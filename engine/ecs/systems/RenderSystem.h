#pragma once

#include "ISystem.h"
#include "../MathTypes.h"
#include "../../render/RenderResourceHandles.h"
#include "../../resources/Resource.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

class IRenderAdapter;
class ResourceManager;
struct MeshResource;
struct TextureResource;
struct ShaderResource;
struct MaterialResource;

namespace ecs
{
struct MaterialComponent;
struct MeshRendererComponent;
struct TransformComponent;

class RenderSystem final : public ISystem
{
public:
    ~RenderSystem() override;

    const char* Name() const override { return "RenderSystem"; }
    void Update(World& world, float dt) override;
    void SetRenderAdapter(IRenderAdapter* renderer);
    void SetCameraTransform(const Vec3& position, float yawRadians, float pitchRadians);
    void SetCameraProjection(float verticalFovRadians, float aspectRatio, float nearPlane, float farPlane);
    void SetResourceManager(ResourceManager* resourceManager) { m_ResourceManager = resourceManager; }
    void ReleaseGpuResources();

private:
    bool TryDrawResourceMesh(
        const TransformComponent& transform,
        const MeshRendererComponent& meshRenderer,
        const MaterialComponent* materialComponent);
    RenderMeshHandle GetOrUploadMesh(const std::string& key);
    RenderTextureHandle GetOrCreateTexture(const std::string& key);
    RenderShaderHandle GetOrCreateShader(const std::string& key);
    ResourceHandle<MaterialResource> GetOrLoadMaterial(const std::string& key);

    IRenderAdapter* m_Renderer = nullptr;
    IRenderAdapter* m_ResourceOwnerRenderer = nullptr;
    ResourceManager* m_ResourceManager = nullptr;
    Vec3 m_CameraPosition{ 0.0f, 0.0f, -2.25f };
    float m_CameraYaw = 0.0f;
    float m_CameraPitch = 0.0f;
    float m_CameraVerticalFovRadians = 1.04719755f;
    float m_CameraAspectRatio = 16.0f / 9.0f;
    float m_CameraNearPlane = 0.01f;
    float m_CameraFarPlane = 100.0f;
    std::unordered_map<std::string, ResourceHandle<MeshResource>> m_MeshResources;
    std::unordered_map<std::string, ResourceHandle<TextureResource>> m_TextureResources;
    std::unordered_map<std::string, ResourceHandle<ShaderResource>> m_ShaderResources;
    std::unordered_map<std::string, ResourceHandle<MaterialResource>> m_MaterialResources;
    std::unordered_set<std::string> m_FailedMeshKeys;
    std::unordered_set<std::string> m_FailedTextureKeys;
    std::unordered_set<std::string> m_FailedShaderKeys;
    std::unordered_set<std::string> m_FailedMaterialKeys;
    std::unordered_map<std::string, std::uint64_t> m_FailedMeshGpuVersions;
    std::unordered_map<std::string, std::uint64_t> m_FailedTextureGpuVersions;
    std::unordered_map<std::string, std::uint64_t> m_FailedShaderGpuVersions;
    std::unordered_set<std::string> m_LoggedMeshReuseKeys;
    std::unordered_set<std::string> m_LoggedTextureReuseKeys;
    std::unordered_set<std::string> m_LoggedShaderReuseKeys;
};
}
