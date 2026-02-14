#include "VkRenderAdapter.h"

#if defined(ENABLE_VULKAN)

#include "../../../core/Logger.h"

bool VkRenderAdapter::Initialize(IWindow* window)
{
    (void)window;
    Logger::Get().Info("Vulkan adapter stub: Initialize");
    return false; // или true, но тогда надо реально рисовать/презентить
}

void VkRenderAdapter::BeginFrame() {}
void VkRenderAdapter::Clear(float, float, float, float) {}
void VkRenderAdapter::DrawTestTriangle() {}
void VkRenderAdapter::EndFrame() {}
void VkRenderAdapter::Present() {}
void VkRenderAdapter::Shutdown() {}

#endif
