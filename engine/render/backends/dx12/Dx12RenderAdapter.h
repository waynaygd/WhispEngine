#pragma once
#include "../../IRenderAdapter.h"

#ifdef _WIN32
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <unordered_map>
#include <vector>

class Dx12RenderAdapter final : public IRenderAdapter
{
public:
	bool Initialize(IWindow* window) override;
	bool InitializeEditorUi(IWindow* window) override;
	void BeginEditorUiFrame() override;
	void RenderEditorUiFrame() override;
	bool BeginViewportRender(int width, int height, const float* clearColor) override;
	void EndViewportRender() override;
	std::uint64_t GetViewportTextureId() const override;
	void BeginFrame() override;
	void Clear(float r, float g, float b, float a) override;
	void DrawTestTriangle() override;
	void DrawTestLine() override;
	void DrawTestQuad() override;
	void DrawTestCube() override;
	void EndFrame() override;
	void Present() override;
	void ShutdownEditorUi() override;
	void Shutdown() override;

	void SetTestTransform(const float* mvp16) override;
	void SetTestColor(float r, float g, float b, float a) override;
	RenderMeshHandle UploadMesh(const MeshData& meshData) override;
	void DestroyMesh(RenderMeshHandle handle) override;
	RenderTextureHandle CreateTexture2D(const TextureData& textureData) override;
	void DestroyTexture(RenderTextureHandle handle) override;
	RenderShaderHandle CreateShaderProgram(const ShaderResource& shaderResource) override;
	void DestroyShader(RenderShaderHandle handle) override;
	void BindShader(RenderShaderHandle handle) override;
	void BindTexture(std::uint32_t slot, RenderTextureHandle handle) override;
	void DrawMesh(RenderMeshHandle handle) override;

private:
	bool CreateDevice(HWND hwnd);
	bool CreateCommandObjects();
	bool CreateSwapchain(HWND hwnd);
	bool CreateRtvHeapAndTargets();
	bool CreateDepthResources();
	bool ResizeBackBufferResources(UINT width, UINT height);
	void ResizeBackBufferIfNeeded();
	bool CreateSyncObjects();
	bool CreatePipelineAndAssets();
	bool EnsureViewportResources(UINT width, UINT height);
	void ReleaseViewportResources();

	void WaitForGpu();
	void MoveToNextFrame();
	static void AllocateImGuiDescriptor(
		struct ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle);
	static void FreeImGuiDescriptor(
		struct ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

	static constexpr UINT FrameCount = 2;
    static constexpr UINT MaxDrawsPerFrame = 2048;

	struct UploadedMesh
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
		D3D12_VERTEX_BUFFER_VIEW vertexView{};
		Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
		D3D12_INDEX_BUFFER_VIEW indexView{};
		UINT indexCount = 0;
	};

	struct UploadedTexture
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> texture;
		UINT width = 0;
		UINT height = 0;
		UINT descriptorIndex = 0;
		D3D12_CPU_DESCRIPTOR_HANDLE cpuSrvHandle{};
		D3D12_GPU_DESCRIPTOR_HANDLE gpuSrvHandle{};
	};

	struct UploadedShader
	{
		Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
	};

	struct ViewportTarget
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> color;
		Microsoft::WRL::ComPtr<ID3D12Resource> depth;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap;
		D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
		D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
		D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{};
		D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};
		UINT width = 0;
		UINT height = 0;
		UINT srvDescriptorIndex = UINT_MAX;
		bool rendering = false;
	};

	Microsoft::WRL::ComPtr<IDXGIFactory6> m_Factory;
	Microsoft::WRL::ComPtr<ID3D12Device> m_Device;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_Queue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_Allocator[FrameCount];
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_CmdList;

	Microsoft::WRL::ComPtr<IDXGISwapChain3> m_Swapchain;
	HWND m_Hwnd = nullptr;
	UINT m_FrameIndex = 0;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
	UINT m_RtvSize = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_Rt[FrameCount];
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DsvHeap;
	UINT m_DsvSize = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_Depth;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SrvHeap;
	UINT m_SrvDescriptorSize = 0;

	Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
	UINT64 m_FenceValue = 0;
	HANDLE m_FenceEvent = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSig;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_Pso;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_LinePso;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_VB;
	D3D12_VERTEX_BUFFER_VIEW m_VbView{};
	Microsoft::WRL::ComPtr<ID3D12Resource> m_LineVB;
	D3D12_VERTEX_BUFFER_VIEW m_LineVbView{};
	Microsoft::WRL::ComPtr<ID3D12Resource> m_QuadVB;
	D3D12_VERTEX_BUFFER_VIEW m_QuadVbView{};
	Microsoft::WRL::ComPtr<ID3D12Resource> m_CubeVB;
	D3D12_VERTEX_BUFFER_VIEW m_CubeVbView{};

	float m_ClearColor[4] = { 0.08f, 0.08f, 0.12f, 1.0f };

	UINT m_Width = 1280;
	UINT m_Height = 720;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_Cb[FrameCount][MaxDrawsPerFrame];
	uint8_t* m_CbMapped[FrameCount][MaxDrawsPerFrame] = {};
	D3D12_GPU_VIRTUAL_ADDRESS m_CbGpu[FrameCount][MaxDrawsPerFrame] = {};
	UINT m_DrawCbIndex = 0;
	static constexpr UINT MaxTextures = 128;
	std::unordered_map<std::uint64_t, UploadedMesh> m_UploadedMeshes;
	std::uint64_t m_NextMeshHandle = 1;
	std::unordered_map<std::uint64_t, UploadedTexture> m_UploadedTextures;
	std::vector<UINT> m_FreeTextureDescriptorIndices;
	std::vector<UINT> m_FreeImGuiDescriptorIndices;
	std::uint64_t m_NextTextureHandle = 1;
	std::unordered_map<std::uint64_t, UploadedShader> m_UploadedShaders;
	std::uint64_t m_NextShaderHandle = 1;
	ViewportTarget m_ViewportTarget;
	RenderShaderHandle m_BoundShaderHandle{};
	RenderTextureHandle m_BoundTextureHandle{};
	bool m_EditorUiInitialized = false;

	float m_PendingMVP[16] = {
		1,0,0,0,
		0,1,0,0,
		0,0,1,0,
		0,0,0,1
	};
	float m_PendingColor[4] = { 1,1,1,1 };
};
#endif
