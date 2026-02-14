#pragma once

class IWindow;

class IRenderAdapter
{
public:
    virtual ~IRenderAdapter() = default;

    virtual bool Initialize(IWindow* window) = 0;
    virtual void BeginFrame() = 0;
    virtual void Clear(float r, float g, float b, float a) = 0;

    virtual void SetTestTransform(const float* mvp16) = 0;

    virtual void DrawTestTriangle() = 0;
    virtual void EndFrame() = 0;
    virtual void Present() = 0;
    virtual void Shutdown() = 0;
};
