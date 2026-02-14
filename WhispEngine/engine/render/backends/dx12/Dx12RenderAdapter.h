#pragma once
#include "../../IRenderAdapter.h"

#ifdef _WIN32
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>

class Dx12RenderAdapter final : public IRenderAdapter
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
	bool CreateDevice(HWND hwnd);
	bool CreateCommandObjects();
	bool CreateSwapchain(HWND hwnd);
	bool CreateRtvHeapAndTargets();
	bool CreateSyncObjects();
	bool CreatePipelineAndAssets();

	void WaitForGpu();
	void MoveToNextFrame();

	static constexpr UINT FrameCount = 2;

	Microsoft::WRL::ComPtr<IDXGIFactory6> m_Factory;
	Microsoft::WRL::ComPtr<ID3D12Device> m_Device;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_Queue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_Allocator[FrameCount];
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_CmdList;

	Microsoft::WRL::ComPtr<IDXGISwapChain3> m_Swapchain;
	UINT m_FrameIndex = 0;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
	UINT m_RtvSize = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_Rt[FrameCount];

	Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
	UINT64 m_FenceValue = 0;
	HANDLE m_FenceEvent = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSig;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_Pso;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_VB;
	D3D12_VERTEX_BUFFER_VIEW m_VbView{};

	float m_ClearColor[4] = { 0.08f, 0.08f, 0.12f, 1.0f };

	UINT m_Width = 1280;
	UINT m_Height = 720;
};
#endif
