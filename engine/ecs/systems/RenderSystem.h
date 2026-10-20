#pragma once
#include <nlohmann/json.hpp>
#include "../../ui/RuntimeUI.h"
#include "../../render/Environment.h"
#include "../PostProcessEvaluation.h"
#include "../../render/SceneLights.h"
#include "../../resources/EnvironmentResource.h"
#include "../../resources/Resource.h"

#include "ISystem.h"
#include "../MathTypes.h"
#include "../components/TransformComponent.h"
#include "../RenderInterpolation.h"
#include "../PlacementPreview.h"
#include "../../render/RenderResourceHandles.h"
#include "../../render/RenderInstanceData.h"
#include "../../render/Shadows.h"
#include "../../render/MaterialState.h"
#include "../../resources/Resource.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <deque>

class IRenderAdapter;
class ResourceManager;
struct MeshResource;
struct MeshData;
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
    // A transient editor view mask. Never changes ECS/runtime visibility.
    void SetEditorIsolation(const std::vector<Entity>& entities){
        if(entities==m_EditorIsolation)return;m_EditorIsolation=entities;
        m_EditorIsolated.clear();m_EditorIsolated.insert(entities.begin(),entities.end());
    }
    void SetPlacementPreview(const std::vector<PlacementRenderItem>& items){m_PlacementItems=items;}
    void SetViewportUnlit(bool value){m_ViewportUnlit=value;}
    bool IsViewportUnlit()const{return m_ViewportUnlit;}
    ~RenderSystem() override;

    const char* Name() const override { return "RenderSystem"; }
    void Update(World& world, float dt) override;
    void SetRenderAdapter(IRenderAdapter* renderer);
    void SetCameraTransform(const Vec3& position, float yawRadians, float pitchRadians);
    void SetCameraProjection(float verticalFovRadians, float aspectRatio, float nearPlane, float farPlane);
    void SetResourceManager(ResourceManager* resourceManager) { m_ResourceManager = resourceManager; }
    void SetDebugCollidersEnabled(bool enabled) { m_DebugCollidersEnabled = enabled; }
    void SetInterpolation(RenderInterpolation* interpolation) { m_Interpolation = interpolation; }
    void SetPhysicsDebugPose(bool enabled) { m_PhysicsDebugPose = enabled; }
    void SetShadowSettings(const ShadowSettings* settings) { m_ShadowSettings=settings; }
    ui::DrawList BuildUIDesignerPreview(const ui::Document&,ui::Pair preview,IRenderAdapter* renderer);
    void SetUIPreview(bool value){m_UIPreview=value;}
    const ui::DrawList& GetUIDrawList()const{return m_UIDrawList;}
    std::size_t UIInstanceCount()const{return m_RuntimeUI.Instances().size();}
    void SetPostFxOverrides(const PostFxRuntimeOverrides* value){m_PostFxOverrides=value;}
    const EvaluatedPostProcess& GetEvaluatedPostProcess()const{return m_EvaluatedPostProcess;}
    void SetAtmosphereSettings(const SceneBackground* settings){m_AtmosphereSettings=settings;}
    void SetEnvironmentSettings(const EnvironmentSettings* settings){m_EnvironmentSettings=settings;}
    void SetLightRenderingMode(LightRenderingMode mode){m_LightRenderingMode=mode;}
    void SetLightingSettings(const LightingSettings* settings) { m_LightingSettings=settings; }
    void SetTextureMapMask(unsigned mask){m_TextureMapMask=mask&7;}
    void SetShadingModelOverride(int mode) { m_ShadingModelOverride=mode; }
    void SetInstancingEnabled(bool enabled) { m_InstancingEnabled = enabled; }
    bool IsInstancingEnabled() const { return m_InstancingEnabled; }
    void SetMaterialDiagnostics(bool enabled){m_TraceMaterials=enabled;}
    const auto& GetMaterialDiagnostics()const{return m_MaterialDiagnostics;}
    struct Statistics {
        std::size_t visibleObjects=0;
        std::size_t materialCacheHits=0,materialCacheMisses=0,visibleSubmeshes=0;
        double materialResolveMs=0;
        double gatherMs=0,prepareMs=0,batchMs=0,submitMs=0,shadowPrepareMs=0,shadowMs=0;
        RenderSubmissionStatistics submitted;
    };
    const Statistics& GetStatistics() const { return m_Statistics; }
    void ReleaseGpuResources();

private:
    std::vector<Entity> m_EditorIsolation;
    std::unordered_set<Entity,EntityHash> m_EditorIsolated;
    std::vector<PlacementRenderItem> m_PlacementItems;
    const PostFxRuntimeOverrides* m_PostFxOverrides=nullptr;float m_PostFxTime=0;
    ui::RuntimeUI m_RuntimeUI;ui::DrawList m_UIDrawList;
    std::optional<ui::FontAtlas> m_UIFont;RenderTextureHandle m_UIFontTexture;bool m_UIPreview=true;
    bool m_TraceMaterials=false;
    std::uint64_t m_ResourceCacheEpoch=0;
    std::vector<nlohmann::json> m_MaterialDiagnostics;

    struct RenderSnapshot
    {
        RenderPose pose{};

        RenderMeshRange range;
        MaterialRenderState materialState;
        bool castShadows=true;
        RenderMeshHandle mesh = RenderMeshHandle::Invalid();
        std::array<RenderTextureHandle,3> maps{};
        RenderTextureHandle texture = RenderTextureHandle::Invalid();
        RenderShaderHandle shader = RenderShaderHandle::Invalid();

        std::array<float,4> surface{0,.5f,0,0},baseColor{1,1,1,1},textureParameters{1,1,0,1};
        std::array<float, 4> tint = { 1.0f, 1.0f, 1.0f, 1.0f };
    };

    struct RenderPacket
    {
        RenderInstanceData instance{};

        RenderMeshRange range;
        MaterialRenderState materialState;
        bool castShadows=true;
        RenderMeshHandle mesh = RenderMeshHandle::Invalid();
        std::array<RenderTextureHandle,3> maps{};
        RenderTextureHandle texture = RenderTextureHandle::Invalid();
        RenderShaderHandle shader = RenderShaderHandle::Invalid();

    };

    bool TryBuildRenderSnapshot(
        const MeshRendererComponent& meshRenderer,
        const MaterialComponent* materialComponent,
        RenderSnapshot& outSnapshot, const MeshData& mesh, RenderMeshHandle meshHandle, std::size_t submeshIndex);

    void PrepareRenderPacket(
        const RenderSnapshot& snapshot,
        RenderPacket& outPacket) const;

    void SubmitRenderPacket(
        const RenderPacket& packet);

    RenderMeshHandle GetOrUploadMesh(const std::string& key);
    RenderTextureHandle GetOrCreateTexture(const std::string& key,bool optionalMap=false);
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
    std::unordered_map<std::uint64_t,ShadowCasterBounds> m_ShadowLocalBounds;
    std::vector<ShadowCasterBounds> m_ShadowWorldBounds;
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
    bool m_DebugCollidersEnabled = false;
    bool m_PhysicsDebugPose = false;
    RenderInterpolation* m_Interpolation = nullptr;

    std::vector<RenderSnapshot> m_RenderSnapshots;
    std::vector<RenderPacket> m_RenderPackets;
    struct Batch { std::size_t begin=0,count=0; };
    std::vector<Batch> m_Batches, m_ShadowBatches;
    ShadowSettings m_DefaultShadows;
    const ShadowSettings* m_ShadowSettings=nullptr;
    std::vector<RenderInstanceData> m_InstanceData;
    unsigned m_TextureMapMask=7;
    int m_ShadingModelOverride=-1;
    bool m_InstancingEnabled=true;
    bool m_ViewportUnlit=false;
    std::string m_FogWarning;
    EvaluatedPostProcess m_EvaluatedPostProcess;
    const SceneBackground* m_AtmosphereSettings=nullptr;
    const EnvironmentSettings* m_EnvironmentSettings=nullptr;
    std::string m_EnvironmentKey,m_RequestedEnvironment;
    ResourceHandle<EnvironmentResource> m_EnvironmentAsset;
    std::uint64_t m_EnvironmentVersion=0,m_FailedEnvironmentVersion=0;
    std::string m_FailedEnvironmentKey;
    LightRenderingMode m_LightRenderingMode=LightRenderingMode::Tiled;
    LightingSettings m_DefaultLighting;
    const LightingSettings* m_LightingSettings=nullptr;
    Statistics m_Statistics;

    enum class GpuFinalizeKind
    {
        Mesh,
        Texture,
        Shader
    };

    struct GpuFinalizeRequest
    {
        GpuFinalizeKind kind;
        std::string key;
        std::uint64_t version = 0;
    };

    void QueueGpuFinalization(
        GpuFinalizeKind kind,
        const std::string& key,
        std::uint64_t version);

    std::size_t PumpGpuFinalization(std::size_t maxItems);

    void FinalizeMeshGpu(const std::string& key, std::uint64_t version);
    void FinalizeTextureGpu(const std::string& key, std::uint64_t version);
    void FinalizeShaderGpu(const std::string& key, std::uint64_t version);

    std::deque<GpuFinalizeRequest> m_GpuFinalizeQueue;
    std::unordered_set<std::string> m_PendingGpuFinalizations;

};
}
