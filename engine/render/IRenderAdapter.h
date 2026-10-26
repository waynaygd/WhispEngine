#pragma once
#include <string>

#include "RenderResourceHandles.h"
#include "Particles.h"
#include "RenderInstanceData.h"
#include "Shadows.h"
#include "MaterialState.h"
#include "MaterialPreview.h"
#include "Environment.h"
#include "AntiAliasing.h"
#include "EditorVisuals.h"
#include "SceneLights.h"
#include "Fog.h"
#include "ScreenEffects.h"
#include "../resources/PostFxResource.h"
namespace ui {struct DrawList;}
struct EnvironmentResource;
#include <span>

#include <cstdint>

class IWindow;
struct MeshData;
struct TextureData;
struct ShaderResource;

class IRenderAdapter
{
public:
    virtual ~IRenderAdapter() = default;

    virtual bool Initialize(IWindow* window) = 0;
    virtual bool InitializeEditorUi(IWindow* window)
    {
        (void)window;
        return false;
    }
    virtual void BeginEditorUiFrame() {}
    virtual void RenderEditorUiFrame() {}
    virtual bool BeginViewportRender(int width, int height, const float* clearColor)
    {
        (void)width;
        (void)height;
        (void)clearColor;
        return false;
    }
    virtual void EndViewportRender() {}
    virtual std::array<unsigned,2> RenderAreaSize()const{return {0,0};}
    virtual void DrawRuntimeUI(const ui::DrawList&) {}
    virtual std::uint64_t GetViewportTextureId() const { return 0; }
    // One editor auxiliary view, time-multiplexed with the main scene pass.
    virtual bool SupportsCameraPreview()const{return false;}
    virtual bool BeginCameraPreview(int,int,const float*){return false;}
    virtual void EndCameraPreview(){}
    virtual std::uint64_t GetCameraPreviewTextureId()const{return 0;}
    virtual void ReleaseCameraPreview(){}
    virtual std::uint64_t GetTexturePreviewId(RenderTextureHandle)const{return 0;}
    // Called before scene rendering. The caller owns the returned cached texture.
    virtual RenderTextureHandle RenderMeshThumbnail(const MeshData&, RenderShaderHandle, RenderTextureHandle) { return {}; }
    virtual RenderTextureHandle CreateThumbnailTexture(const TextureData& data) { return CreateTexture2D(data); }
    virtual std::uint64_t RenderMaterialPreview(const MaterialPreviewFrame&) {return 0;}
    virtual void ReleaseMaterialPreview() {}
    virtual MaterialPreviewMetrics GetMaterialPreviewMetrics()const{return {};}
    virtual void BeginFrame() = 0;
    virtual void Clear(float r, float g, float b, float a) = 0;

    virtual void SetTestTransform(const float* mvp16) = 0;
    virtual void SetTestColor(float r, float g, float b, float a) = 0;

    virtual bool ReloadShaders() { return false; }
    virtual bool HotReloadShaders() { return false; }

    virtual RenderMeshHandle UploadMesh(const MeshData& meshData)
    {
        (void)meshData;
        return RenderMeshHandle::Invalid();
    }

    virtual void DestroyMesh(RenderMeshHandle handle)
    {
        (void)handle;
    }

    virtual RenderTextureHandle CreateTexture2D(const TextureData& textureData)
    {
        (void)textureData;
        return RenderTextureHandle::Invalid();
    }

    virtual void DestroyTexture(RenderTextureHandle handle)
    {
        (void)handle;
    }

    virtual RenderShaderHandle CreateShaderProgram(const ShaderResource& shaderResource)
    {
        (void)shaderResource;
        return RenderShaderHandle::Invalid();
    }

    virtual void DestroyShader(RenderShaderHandle handle)
    {
        (void)handle;
    }

    virtual void BindShader(RenderShaderHandle handle)
    {
        (void)handle;
    }

    virtual void BindTexture(std::uint32_t slot, RenderTextureHandle handle)
    {
        (void)slot;
        (void)handle;
    }

    // Prepared instance spans are immutable until EndMainPass. A backend may reuse
    // their upload ranges across the shadow and main passes of this scene.
    virtual bool BeginShadowPass(const ShadowFrame&) { return false; }
    virtual void DrawShadowMesh(RenderMeshHandle, const RenderInstanceData&) {}
    virtual bool DrawShadowMeshInstanced(RenderMeshHandle, std::span<const RenderInstanceData>) { return false; }
    virtual void EndShadowPass() {}
    virtual void SetEditorVisuals(const EditorVisualFrame&) {}
    virtual bool UploadEditorSky(const EnvironmentResource&,std::uint64_t){return false;}
    virtual void ReleaseEditorSky() {}
    virtual EditorVisualDiagnostics GetEditorVisualDiagnostics()const{return {};}
    virtual void BeginMainPass() {}
    virtual void EndMainPass() {}
    // Same color target/material path, independent shared depth for all viewmodels.
    virtual void DrawParticles(const ParticleDrawList&) {}
    virtual ParticleGpuStatistics GetParticleStatistics()const{return {};}
    virtual bool BeginViewmodelPass() {return false;}
    virtual void EndViewmodelPass() {}
    virtual std::uint64_t GetShadowPreviewTextureId() const { return 0; }
    virtual bool SaveFrameImage(const std::string&) { return false; }
    virtual RenderGpuTimings GetGpuTimings() const { return {}; }
    virtual void SetMeshRange(const RenderMeshRange&) {}
    virtual void SetMaterialState(const MaterialRenderState&) {}
    virtual void SetModelMatrix(const std::array<float,16>&) {}
    virtual void SetSurfaceParameters(const std::array<float,4>&) {}
    virtual void SetTextureParameters(const std::array<float,4>&) {}
    virtual void SetBaseColor(const std::array<float,4>&) {}
    virtual void SetCameraPosition(const std::array<float,4>&) {}
    virtual void SetLighting(const LightingConstants&) {}
    virtual void SetSceneLights(std::span<const GpuSceneLight>,LightRenderingMode,const SceneLightDiagnostics& = {}) {}
    virtual SceneLightDiagnostics GetSceneLightDiagnostics() const {return {};}

    virtual void SetCustomPostFx(const PostFxFrame&) {}
    virtual std::string CustomPostFxStatus(const std::string&)const{return "Not requested";}
    virtual void SetScreenEffects(const ScreenEffects&) {}
    virtual void SetFog(const FogFrame&) {}
    virtual FogDiagnostics GetFogDiagnostics()const{return {};}
    virtual void SetAntiAliasing(AntiAliasingMode) {}
    virtual AntiAliasingDiagnostics GetAntiAliasingDiagnostics()const{return {};}
    virtual void SetEnvironmentSettings(const EnvironmentSettings&) {}
    virtual bool UploadEnvironment(const EnvironmentResource&,std::uint64_t) { return false; }
    virtual void ReleaseEnvironment() {}
    virtual EnvironmentDiagnostics GetEnvironmentDiagnostics() const { return {}; }
    virtual void SetNormalMatrix(const std::array<float,12>&) {}
    virtual void DrawMesh(RenderMeshHandle handle)
    {
        (void)handle;
    }
    virtual bool SupportsInstancing(RenderShaderHandle) const { return false; }
    // False means no instanced command was recorded: caller may use normal draws.
    virtual bool DrawMeshInstanced(RenderMeshHandle, std::span<const RenderInstanceData>) { return false; }
    virtual RenderSubmissionStatistics GetSubmissionStatistics() const { return {}; }

    virtual void DrawTestTriangle() = 0;
    virtual void DrawTestLine() = 0;
    virtual void DrawTestQuad() = 0;
    virtual void DrawTestCube() = 0;
    virtual void EndFrame() = 0;
    virtual void Present() = 0;
    virtual void ShutdownEditorUi() {}
    virtual void Shutdown() = 0;
};
