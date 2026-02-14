#pragma once
#include "IRenderAdapter.h"

class NullRenderAdapter : public IRenderAdapter
{
public:
    bool Initialize(IWindow*) override { return true; }
    void BeginFrame() override {}
    void Clear(float, float, float, float) override {}
    void DrawTestTriangle() override {}
    void EndFrame() override {}
    void Present() override {}
    void Shutdown() override {}
};
