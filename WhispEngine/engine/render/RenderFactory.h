#pragma once
#include <memory>

class IRenderAdapter;

enum class RenderBackend
{
    DX12,
    Vulkan
};

class RenderFactory
{
public:
    static std::unique_ptr<IRenderAdapter> Create(RenderBackend backend);
};
