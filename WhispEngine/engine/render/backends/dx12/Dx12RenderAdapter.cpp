#include "Dx12RenderAdapter.h"
#ifdef _WIN32

#include "../../../core/Logger.h"
#include "../../../platform/GlfwWindow.h"   // <-- важно

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>                   // <-- важно для D3D12SerializeRootSignature

#include "../../../external/d3dx12.h" // <-- важно для CD3DX12_* (путь подстрой под твой)

#include <stdexcept>
#include <vector>
#include <fstream>


static std::vector<uint8_t> ReadFileBinary(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    size_t sz = (size_t)f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(sz);
    f.read((char*)data.data(), sz);
    return data;
}

static void ThrowIfFailed(HRESULT hr, const char* msg)
{
    if (FAILED(hr))
    {
        Logger::Get().Error(msg);
        throw std::runtime_error(msg);
    }
}

bool Dx12RenderAdapter::Initialize(IWindow* window)
{
    try
    {
        auto* glfwWin = static_cast<GlfwWindow*>(window);
        HWND hwnd = (HWND)glfwGetWin32Window(glfwWin->GetGlfwHandle());

        UINT dxgiFlags = 0;
#if defined(_DEBUG)
        // Можно включить debug layer позже, когда появится D3D12SDKLayers.dll (из Graphics Tools).
#endif

        ThrowIfFailed(CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&m_Factory)), "CreateDXGIFactory2 failed");

        if (!CreateDevice(hwnd)) return false;
        if (!CreateCommandObjects()) return false;
        if (!CreateSwapchain(hwnd)) return false;
        if (!CreateRtvHeapAndTargets()) return false;
        if (!CreateSyncObjects()) return false;
        if (!CreatePipelineAndAssets()) return false;

        Logger::Get().Info("DX12 adapter initialized");
        return true;
    }
    catch (const std::exception& e)
    {
        Logger::Get().Error(std::string("DX12 init exception: ") + e.what());
        return false;
    }
}

bool Dx12RenderAdapter::CreateDevice(HWND)
{
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;

    for (UINT i = 0; m_Factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;

        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_Device))))
            break;
    }

    if (!m_Device)
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
        ThrowIfFailed(m_Factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "EnumWarpAdapter failed");
        ThrowIfFailed(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_Device)), "D3D12CreateDevice (WARP) failed");
    }
    return true;
}

bool Dx12RenderAdapter::CreateCommandObjects()
{
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(m_Device->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_Queue)), "CreateCommandQueue failed");

    for (UINT i = 0; i < FrameCount; ++i)
        ThrowIfFailed(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_Allocator[i])), "CreateCommandAllocator failed");

    ThrowIfFailed(m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_Allocator[0].Get(), nullptr, IID_PPV_ARGS(&m_CmdList)),
        "CreateCommandList failed");
    ThrowIfFailed(m_CmdList->Close(), "CommandList close failed");
    return true;
}

bool Dx12RenderAdapter::CreateSwapchain(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = m_Width;
    sd.Height = m_Height;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = FrameCount;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    sd.Scaling = DXGI_SCALING_STRETCH;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> sc1;
    ThrowIfFailed(m_Factory->CreateSwapChainForHwnd(
        m_Queue.Get(), hwnd, &sd, nullptr, nullptr, &sc1),
        "CreateSwapChainForHwnd failed");

    ThrowIfFailed(m_Factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER), "MakeWindowAssociation failed");

    ThrowIfFailed(sc1.As(&m_Swapchain), "SwapChain cast to IDXGISwapChain3 failed");
    m_FrameIndex = m_Swapchain->GetCurrentBackBufferIndex();
    return true;
}

bool Dx12RenderAdapter::CreateRtvHeapAndTargets()
{
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = FrameCount;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(m_Device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_RtvHeap)), "CreateDescriptorHeap RTV failed");

    m_RtvSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < FrameCount; ++i)
    {
        ThrowIfFailed(m_Swapchain->GetBuffer(i, IID_PPV_ARGS(&m_Rt[i])), "Swapchain GetBuffer failed");
        m_Device->CreateRenderTargetView(m_Rt[i].Get(), nullptr, handle);
        handle.ptr += m_RtvSize;
    }
    return true;
}

bool Dx12RenderAdapter::CreateSyncObjects()
{
    ThrowIfFailed(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence)), "CreateFence failed");
    m_FenceValue = 1;
    m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_FenceEvent)
        throw std::runtime_error("CreateEvent failed");
    return true;
}

bool Dx12RenderAdapter::CreatePipelineAndAssets()
{
    // Root signature: пустая, только IA + VS/PS без ресурсов
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 0;
    rsd.pParameters = nullptr;
    rsd.NumStaticSamplers = 0;
    rsd.pStaticSamplers = nullptr;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> serialized;
    Microsoft::WRL::ComPtr<ID3DBlob> error;

    ThrowIfFailed(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error),
        "D3D12SerializeRootSignature failed");
    ThrowIfFailed(m_Device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&m_RootSig)),
        "CreateRootSignature failed");

    // Загружаем DXIL из build/shaders/dx12
    auto vs = ReadFileBinary("shaders/dx12/triangle_vs.dxil");
    auto ps = ReadFileBinary("shaders/dx12/triangle_ps.dxil");
    if (vs.empty() || ps.empty())
        throw std::runtime_error("DXIL not found. Ensure shaders are built to build/shaders/dx12 and working directory is build dir.");

    D3D12_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.InputLayout = { layout, _countof(layout) };
    pso.pRootSignature = m_RootSig.Get();
    pso.VS = { vs.data(), vs.size() };
    pso.PS = { ps.data(), ps.size() };
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;

    ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_Pso)), "CreateGraphicsPipelineState failed");

    struct Vtx { float x, y, z; float r, g, b; };
    Vtx tri[3] = {
      { 0.0f,  0.5f, 0.0f, 1,0,0},
      { 0.5f, -0.5f, 0.0f, 0,1,0},
      {-0.5f, -0.5f, 0.0f, 0,0,1},
    };

    const UINT vbSize = sizeof(tri);

    // Для простоты и надёжности: upload heap (медленнее, но корректно)
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(vbSize);

    ThrowIfFailed(m_Device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_VB)),
        "CreateCommittedResource VB failed");

    void* mapped = nullptr;
    CD3DX12_RANGE range(0, 0);
    ThrowIfFailed(m_VB->Map(0, &range, &mapped), "VB Map failed");
    memcpy(mapped, tri, vbSize);
    m_VB->Unmap(0, nullptr);

    m_VbView.BufferLocation = m_VB->GetGPUVirtualAddress();
    m_VbView.StrideInBytes = sizeof(Vtx);
    m_VbView.SizeInBytes = vbSize;

    return true;
}

void Dx12RenderAdapter::BeginFrame()
{
    ThrowIfFailed(m_Allocator[m_FrameIndex]->Reset(), "Allocator Reset failed");
    ThrowIfFailed(m_CmdList->Reset(m_Allocator[m_FrameIndex].Get(), m_Pso.Get()), "CmdList Reset failed");
}

void Dx12RenderAdapter::Clear(float r, float g, float b, float a)
{
    m_ClearColor[0] = r; m_ClearColor[1] = g; m_ClearColor[2] = b; m_ClearColor[3] = a;

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_Rt[m_FrameIndex].Get(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );
    m_CmdList->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)m_FrameIndex * m_RtvSize;

    m_CmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    m_CmdList->ClearRenderTargetView(rtv, m_ClearColor, 0, nullptr);
}

void Dx12RenderAdapter::DrawTestTriangle()
{
    m_CmdList->SetGraphicsRootSignature(m_RootSig.Get());
    m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_CmdList->IASetVertexBuffers(0, 1, &m_VbView);
    m_CmdList->DrawInstanced(3, 1, 0, 0);
}

void Dx12RenderAdapter::EndFrame()
{
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_Rt[m_FrameIndex].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT
    );
    m_CmdList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(m_CmdList->Close(), "CmdList Close failed");
    ID3D12CommandList* lists[] = { m_CmdList.Get() };
    m_Queue->ExecuteCommandLists(1, lists);
}

void Dx12RenderAdapter::Present()
{
    ThrowIfFailed(m_Swapchain->Present(1, 0), "Present failed");
    MoveToNextFrame();
}

void Dx12RenderAdapter::MoveToNextFrame()
{
    const UINT64 fenceToWait = m_FenceValue;
    ThrowIfFailed(m_Queue->Signal(m_Fence.Get(), fenceToWait), "Queue Signal failed");
    m_FenceValue++;

    m_FrameIndex = m_Swapchain->GetCurrentBackBufferIndex();

    if (m_Fence->GetCompletedValue() < fenceToWait)
    {
        ThrowIfFailed(m_Fence->SetEventOnCompletion(fenceToWait, m_FenceEvent), "Fence SetEventOnCompletion failed");
        WaitForSingleObject(m_FenceEvent, INFINITE);
    }
}

void Dx12RenderAdapter::WaitForGpu()
{
    const UINT64 fenceToWait = m_FenceValue;
    ThrowIfFailed(m_Queue->Signal(m_Fence.Get(), fenceToWait), "Queue Signal failed");
    m_FenceValue++;

    ThrowIfFailed(m_Fence->SetEventOnCompletion(fenceToWait, m_FenceEvent), "Fence SetEventOnCompletion failed");
    WaitForSingleObject(m_FenceEvent, INFINITE);
}

void Dx12RenderAdapter::Shutdown()
{
    try
    {
        if (m_Device)
            WaitForGpu();
    }
    catch (...) {}

    if (m_FenceEvent)
    {
        CloseHandle(m_FenceEvent);
        m_FenceEvent = nullptr;
    }

    m_VB.Reset();
    for (UINT i = 0; i < FrameCount; ++i) m_Rt[i].Reset();
    m_RtvHeap.Reset();
    m_Swapchain.Reset();
    m_CmdList.Reset();
    for (UINT i = 0; i < FrameCount; ++i) m_Allocator[i].Reset();
    m_Queue.Reset();
    m_Pso.Reset();
    m_RootSig.Reset();
    m_Fence.Reset();
    m_Device.Reset();
    m_Factory.Reset();

    Logger::Get().Info("DX12 adapter shutdown");
}

#endif
