#pragma once
#include "../../IRenderAdapter.h"
#if defined(ENABLE_VULKAN)
#include <vulkan/vulkan.h>
#include <vector>

class IWindow;

class VkRenderAdapter final : public IRenderAdapter
{
public:
	bool Initialize(IWindow* window) override;
	void BeginFrame() override;
	void Clear(float r, float g, float b, float a) override;
	void DrawTestTriangle() override;
	void EndFrame() override;
	void Present() override;
	void Shutdown() override;

private:
	bool CreateInstance();
	bool CreateSurface(IWindow* window);
	bool PickPhysicalDevice();
	bool CreateDeviceAndQueues();
	bool CreateSwapchain();
	bool CreateImageViews();
	bool CreateRenderPass();
	bool CreatePipeline();
	bool CreateFramebuffers();
	bool CreateCommandPoolAndBuffers();
	bool CreateSyncObjects();

	void RecreateSwapchain(); // позже, по resize
	VkShaderModule LoadShaderModule(const char* spvPath);

private:
	VkInstance m_Instance = VK_NULL_HANDLE;
	VkPhysicalDevice m_Physical = VK_NULL_HANDLE;
	VkDevice m_Device = VK_NULL_HANDLE;
	VkSurfaceKHR m_Surface = VK_NULL_HANDLE;

	VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
	VkQueue m_PresentQueue = VK_NULL_HANDLE;
	uint32_t m_GraphicsFamily = 0;
	uint32_t m_PresentFamily = 0;

	VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
	VkFormat m_SwapFormat{};
	VkExtent2D m_SwapExtent{};
	std::vector<VkImage> m_SwapImages;
	std::vector<VkImageView> m_SwapViews;

	VkRenderPass m_RenderPass = VK_NULL_HANDLE;
	VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_Pipeline = VK_NULL_HANDLE;

	std::vector<VkFramebuffer> m_Framebuffers;

	VkCommandPool m_CmdPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> m_CmdBuffers;

	static constexpr int MaxFramesInFlight = 2;
	VkSemaphore m_ImageAvailable[MaxFramesInFlight]{};
	VkSemaphore m_RenderFinished[MaxFramesInFlight]{};
	VkFence m_InFlight[MaxFramesInFlight]{};
	uint32_t m_ImageIndex = 0;
	int m_Frame = 0;

	float m_Clear[4] = { 0.08f, 0.08f, 0.12f, 1.0f };

	VkBuffer m_VB = VK_NULL_HANDLE;
	VkDeviceMemory m_VBMem = VK_NULL_HANDLE;

};
#endif
