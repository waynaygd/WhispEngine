#pragma once

#include "RenderResourceHandles.h"

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

    virtual void DrawMesh(RenderMeshHandle handle)
    {
        (void)handle;
    }

    virtual void DrawTestTriangle() = 0;
    virtual void DrawTestLine() = 0;
    virtual void DrawTestQuad() = 0;
    virtual void DrawTestCube() = 0;
    virtual void EndFrame() = 0;
    virtual void Present() = 0;
    virtual void Shutdown() = 0;
};
