#pragma once
#include "../../IRenderAdapter.h"
#if defined(ENABLE_VULKAN)
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <filesystem>
#include <map>
#include <atomic>
#include "../../../resources/MeshData.h"
#include "../../../resources/TextureData.h"

class IWindow;

class VkRenderAdapter final : public IRenderAdapter
{
public:
	bool Initialize(IWindow* window) override;
    RenderMeshHandle UploadMesh(const MeshData&) override;
    void DestroyMesh(RenderMeshHandle) override;
    RenderTextureHandle CreateTexture2D(const TextureData&) override;
    void DestroyTexture(RenderTextureHandle) override;
    RenderShaderHandle CreateShaderProgram(const ShaderResource&) override;
    void BindShader(RenderShaderHandle) override {}
    void BindTexture(std::uint32_t slot,RenderTextureHandle t) override {if(slot==0)m_Texture=t;}
    void SetMeshRange(const RenderMeshRange& range) override {m_Range=range;}
    void SetMaterialState(const MaterialRenderState& state) override {m_Material=state;}
    void SetBaseColor(const std::array<float,4>& value) override {m_BaseColor=value;}
    void DrawMesh(RenderMeshHandle) override;
    bool BeginViewmodelPass() override;
    void EndViewmodelPass() override;
    void DrawRuntimeUI(const ui::DrawList&) override;
    std::array<unsigned,2> RenderAreaSize()const override {return {m_SwapExtent.width,m_SwapExtent.height};}
    void EnableReadback(bool value=true){m_ReadbackEnabled=value;}
    std::vector<unsigned char> ReadBackPixels();
    std::uint64_t GetValidationErrorCount()const{return m_ValidationErrors.load();}
    bool ValidationEnabled()const{return m_DebugMessenger!=VK_NULL_HANDLE;}
    RenderSubmissionStatistics GetSubmissionStatistics()const override{return m_Statistics;}

	void BeginFrame() override;
	void Clear(float r, float g, float b, float a) override;
	void DrawTestTriangle() override;
	void DrawTestLine() override;
	void DrawTestQuad() override;
	void DrawTestCube() override;
	void EndFrame() override;
	void Present() override;
	void Shutdown() override;

	void SetTestTransform(const float* mvp16) override;
	void SetTestColor(float r, float g, float b, float a) override;

	bool ReloadShaders() override;
	bool HotReloadShaders() override;

private:
    bool CreateSyncObjects();
    struct DepthImage {VkImage image=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;VkImageView view=VK_NULL_HANDLE;};
    std::vector<DepthImage> m_WorldDepth,m_ViewmodelDepth;
    VkRenderPass m_ViewmodelPass=VK_NULL_HANDLE,m_ResumePass=VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_ViewmodelFrames;
    bool m_Recording=false,m_Skip=false,m_ReadbackEnabled=false;
    int m_LastSubmittedFrame=0;
    IWindow* m_Window=nullptr;
    VkDebugUtilsMessengerEXT m_DebugMessenger=VK_NULL_HANDLE;std::atomic<std::uint64_t> m_ValidationErrors{0};
    struct Buffer {VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;VkDeviceSize capacity=0;};
    struct Mesh {Buffer vertices,indices;MeshData source;};
    struct Texture {DepthImage gpu;VkDescriptorSet set=VK_NULL_HANDLE;TextureData source;};
    std::map<std::uint64_t,Mesh> m_Meshes;
    std::map<std::uint64_t,Texture> m_Textures;
    std::vector<Mesh> m_RetiredMeshes[2];std::vector<Texture> m_RetiredTextures[2];
    Buffer m_Readback[2],m_UIBuffers[2];VkDeviceSize m_UIUsed=0;int m_LastReadback=0;
    VkDescriptorSetLayout m_TextureLayout=VK_NULL_HANDLE;VkDescriptorPool m_TexturePool=VK_NULL_HANDLE;VkSampler m_TextureSampler=VK_NULL_HANDLE;
    VkPipelineLayout m_SceneLayout=VK_NULL_HANDLE;
    VkPipeline m_ScenePso[3]{},m_UIPso=VK_NULL_HANDLE;
    RenderTextureHandle m_Texture,m_White;RenderMeshRange m_Range;MaterialRenderState m_Material;
    std::array<float,4> m_BaseColor{1,1,1,1};std::uint64_t m_NextMesh=1,m_NextTexture=1;
    RenderSubmissionStatistics m_Statistics;
    Buffer MakeBuffer(VkDeviceSize,VkBufferUsageFlags);
    void FreeBuffer(Buffer&);
    DepthImage MakeDepth();void FreeImage(DepthImage&);
    void InitializeSubmission();void ReleaseSubmission();void RetireSubmission();
    void StartPass(VkRenderPass,VkFramebuffer,bool clear);
    void CopyReadback();void RecreateWindow();


	bool CreatePipelineFromModules(VkShaderModule vs, VkShaderModule fs, VkPrimitiveTopology topology, VkPipeline& outPipeline);

	VkShaderModule LoadShaderModule(const char* spvPath);

	std::string m_VertSrcPath = "engine/shaders/vulkan/triangle.vert";
	std::string m_FragSrcPath = "engine/shaders/vulkan/triangle.frag";

	std::string m_VertSpvPath = "shaders/vulkan/triangle.vert.spv";
	std::string m_FragSpvPath = "shaders/vulkan/triangle.frag.spv";

	bool CompileGlslToSpv(const std::string& srcPath,
		const std::string& outSpvPath,
		const char* stage, std::string& outErr);

	static std::string Quote(const std::string& s);

	bool RecreateGraphicsPipeline();

	bool m_HotReloadInited = false;
	std::filesystem::file_time_type m_VertStamp{};
	std::filesystem::file_time_type m_FragStamp{};

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
	VkPipeline m_LinePipeline = VK_NULL_HANDLE;

	std::vector<VkFramebuffer> m_Framebuffers;

	VkCommandPool m_CmdPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> m_CmdBuffers;

	static constexpr int MaxFramesInFlight = 2;
	VkSemaphore m_ImageAvailable[MaxFramesInFlight]{};
	std::vector<VkSemaphore> m_RenderFinished; // one per swapchain image: presentation may outlive a frame fence.
	VkFence m_InFlight[MaxFramesInFlight]{};
	uint32_t m_ImageIndex = 0;
	int m_Frame = 0;

	float m_Clear[4] = { 0.08f, 0.08f, 0.12f, 1.0f };

	VkBuffer m_VB = VK_NULL_HANDLE;
	VkDeviceMemory m_VBMem = VK_NULL_HANDLE;
	VkBuffer m_LineVB = VK_NULL_HANDLE;
	VkDeviceMemory m_LineVBMem = VK_NULL_HANDLE;
	VkBuffer m_QuadVB = VK_NULL_HANDLE;
	VkDeviceMemory m_QuadVBMem = VK_NULL_HANDLE;
	VkBuffer m_CubeVB = VK_NULL_HANDLE;
	VkDeviceMemory m_CubeVBMem = VK_NULL_HANDLE;

	float m_PendingMVP[16] = {
		1,0,0,0,
		0,1,0,0,
		0,0,1,0,
		0,0,0,1
	};
	float m_PendingColor[4] = { 1,1,1,1 };
};
#endif
