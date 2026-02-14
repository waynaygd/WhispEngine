#include "RenderFactory.h"

#if defined(ENABLE_DX12)
#include "backends/dx12/Dx12RenderAdapter.h"
#endif

#if defined(ENABLE_VULKAN)
#include "backends/vulkan/VkRenderAdapter.h"
#endif

std::unique_ptr<IRenderAdapter> RenderFactory::Create(RenderBackend backend)
{
    switch (backend)
    {
#if defined(ENABLE_DX12)
    case RenderBackend::DX12:
        return std::make_unique<Dx12RenderAdapter>();
#endif

#if defined(ENABLE_VULKAN)
    case RenderBackend::Vulkan:
        return std::make_unique<VkRenderAdapter>();
#endif
    }
}

