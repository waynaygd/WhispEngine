#include "Dx12RenderAdapter.h"
#ifdef _WIN32

#include "../../../core/Logger.h"
#include "../../../platform/GlfwWindow.h"   
#include "../../../resources/MeshData.h"
#include "../../../resources/ShaderResource.h"
#include "../../../resources/TextureData.h"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>                  

#include "../../../external/d3dx12.h" 

#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_glfw.h>

#include <stdexcept>
#include <algorithm>
#include <string>
#include <vector>
#include <fstream>
#include <functional>
#include <cstring>


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

struct Dx12PrimitiveVertex
{
    float position[3];
    float color[3];
};

struct Dx12TexturedVertex
{
    float position[3];
    float normal[3];
    float uv[2];
};

static void CreateUploadBuffer(
    ID3D12Device* device,
    const void* srcData,
    UINT size,
    Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
    const char* errorName)
{
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&resource)),
        errorName);

    CD3DX12_RANGE readRange(0, 0);
    void* mapped = nullptr;
    ThrowIfFailed(resource->Map(0, &readRange, &mapped), "DX12 mesh buffer map failed");
    memcpy(mapped, srcData, size);
    resource->Unmap(0, nullptr);
}

static void ExecuteImmediateCommandList(
    ID3D12Device* device,
    ID3D12CommandQueue* queue,
    ID3D12Fence* fence,
    HANDLE fenceEvent,
    UINT64& fenceValue,
    const std::function<void(ID3D12GraphicsCommandList*)>& recorder)
{
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;

    ThrowIfFailed(
        device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
        "DX12 immediate upload allocator creation failed");
    ThrowIfFailed(
        device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)),
        "DX12 immediate upload command list creation failed");

    recorder(commandList.Get());

    ThrowIfFailed(commandList->Close(), "DX12 immediate upload command list close failed");
    ID3D12CommandList* commandLists[] = { commandList.Get() };
    queue->ExecuteCommandLists(1, commandLists);

    const UINT64 fenceToWait = fenceValue;
    ThrowIfFailed(queue->Signal(fence, fenceToWait), "DX12 immediate upload queue signal failed");
    ++fenceValue;

    ThrowIfFailed(fence->SetEventOnCompletion(fenceToWait, fenceEvent), "DX12 immediate upload fence completion failed");
    WaitForSingleObject(fenceEvent, INFINITE);
}

static Microsoft::WRL::ComPtr<ID3DBlob> CompileHlsl(
    const std::string& source,
    const char* sourceName,
    const char* entryPoint,
    const char* profile)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(
        source.data(),
        source.size(),
        sourceName,
        nullptr,
        nullptr,
        entryPoint,
        profile,
        flags,
        0,
        &bytecode,
        &errors);

    if (FAILED(hr))
    {
        std::string message = "DX12 shader compile failed";
        if (errors && errors->GetBufferPointer())
            message += ": " + std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        Logger::Get().Error(message);
        throw std::runtime_error(message);
    }

    return bytecode;
}

void Dx12RenderAdapter::SetTestTransform(const float* mvp16)
{
    memcpy(m_PendingMVP, mvp16, sizeof(float) * 16);
}

void Dx12RenderAdapter::SetTestColor(float r, float g, float b, float a)
{
    m_PendingColor[0] = r;
    m_PendingColor[1] = g;
    m_PendingColor[2] = b;
    m_PendingColor[3] = a;
}

RenderMeshHandle Dx12RenderAdapter::UploadMesh(const MeshData& meshData)
{
    if (!m_Device)
    {
        Logger::Get().Warn("DX12 UploadMesh: device is not initialized");
        return RenderMeshHandle::Invalid();
    }

    if (meshData.vertices.empty() || meshData.indices.empty())
    {
        Logger::Get().Warn("DX12 UploadMesh: mesh data is empty");
        return RenderMeshHandle::Invalid();
    }

    try
    {
        std::vector<Dx12TexturedVertex> vertices(meshData.vertices.size());
        for (std::size_t i = 0; i < meshData.vertices.size(); ++i)
        {
            const MeshVertex& source = meshData.vertices[i];
            Dx12TexturedVertex& destination = vertices[i];

            destination.position[0] = source.position[0];
            destination.position[1] = source.position[1];
            destination.position[2] = source.position[2];
            destination.normal[0] = source.normal[0];
            destination.normal[1] = source.normal[1];
            destination.normal[2] = source.normal[2];
            destination.uv[0] = source.uv[0];
            destination.uv[1] = source.uv[1];
        }

        UploadedMesh uploadedMesh;
        const UINT vertexBufferSize = static_cast<UINT>(vertices.size() * sizeof(Dx12TexturedVertex));
        const UINT indexBufferSize = static_cast<UINT>(meshData.indices.size() * sizeof(std::uint32_t));

        CreateUploadBuffer(
            m_Device.Get(),
            vertices.data(),
            vertexBufferSize,
            uploadedMesh.vertexBuffer,
            "DX12 UploadMesh: create vertex buffer failed");

        CreateUploadBuffer(
            m_Device.Get(),
            meshData.indices.data(),
            indexBufferSize,
            uploadedMesh.indexBuffer,
            "DX12 UploadMesh: create index buffer failed");

        uploadedMesh.vertexView.BufferLocation = uploadedMesh.vertexBuffer->GetGPUVirtualAddress();
        uploadedMesh.vertexView.StrideInBytes = sizeof(Dx12TexturedVertex);
        uploadedMesh.vertexView.SizeInBytes = vertexBufferSize;

        uploadedMesh.indexView.BufferLocation = uploadedMesh.indexBuffer->GetGPUVirtualAddress();
        uploadedMesh.indexView.SizeInBytes = indexBufferSize;
        uploadedMesh.indexView.Format = DXGI_FORMAT_R32_UINT;
        uploadedMesh.indexCount = static_cast<UINT>(meshData.indices.size());

        const RenderMeshHandle handle{ m_NextMeshHandle++ };
        m_UploadedMeshes.emplace(handle.value, std::move(uploadedMesh));

        Logger::Get().Info(
            "DX12 UploadMesh: handle=" + std::to_string(handle.value) +
            " vertices=" + std::to_string(meshData.vertices.size()) +
            " indices=" + std::to_string(meshData.indices.size()));
        return handle;
    }
    catch (const std::exception& e)
    {
        Logger::Get().Error(std::string("DX12 UploadMesh exception: ") + e.what());
        return RenderMeshHandle::Invalid();
    }
}

void Dx12RenderAdapter::DestroyMesh(RenderMeshHandle handle)
{
    if (!handle.IsValid())
    {
        Logger::Get().Warn("DX12 DestroyMesh: invalid handle");
        return;
    }

    const auto it = m_UploadedMeshes.find(handle.value);
    if (it == m_UploadedMeshes.end())
    {
        Logger::Get().Warn("DX12 DestroyMesh: mesh handle was not found");
        return;
    }

    if (m_Device)
        WaitForGpu();

    m_UploadedMeshes.erase(it);
    Logger::Get().Info("DX12 DestroyMesh: handle=" + std::to_string(handle.value));
}

RenderTextureHandle Dx12RenderAdapter::CreateTexture2D(const TextureData& textureData)
{
    if (!m_Device || !m_Queue || !m_Fence || !m_FenceEvent)
    {
        Logger::Get().Warn("DX12 CreateTexture2D: device is not initialized");
        return RenderTextureHandle::Invalid();
    }

    if (!m_SrvHeap)
    {
        Logger::Get().Warn("DX12 CreateTexture2D: SRV heap is unavailable");
        return RenderTextureHandle::Invalid();
    }

    if (textureData.width <= 0 || textureData.height <= 0 || textureData.channelCount != 4)
    {
        Logger::Get().Warn("DX12 CreateTexture2D: expected RGBA8 texture data");
        return RenderTextureHandle::Invalid();
    }

    const std::size_t expectedSize =
        static_cast<std::size_t>(textureData.width) *
        static_cast<std::size_t>(textureData.height) * 4u;
    if (textureData.pixels.size() != expectedSize)
    {
        Logger::Get().Warn("DX12 CreateTexture2D: pixel data size does not match RGBA8 dimensions");
        return RenderTextureHandle::Invalid();
    }

    if (m_FreeTextureDescriptorIndices.empty())
    {
        Logger::Get().Warn("DX12 CreateTexture2D: no free SRV descriptors remain");
        return RenderTextureHandle::Invalid();
    }

    bool descriptorReserved = false;
    UINT reservedDescriptorIndex = 0;

    try
    {
        UploadedTexture uploadedTexture;
        uploadedTexture.width = static_cast<UINT>(textureData.width);
        uploadedTexture.height = static_cast<UINT>(textureData.height);
        reservedDescriptorIndex = m_FreeTextureDescriptorIndices.back();
        uploadedTexture.descriptorIndex = reservedDescriptorIndex;
        m_FreeTextureDescriptorIndices.pop_back();
        descriptorReserved = true;

        D3D12_RESOURCE_DESC textureDesc{};
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureDesc.Width = uploadedTexture.width;
        textureDesc.Height = uploadedTexture.height;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.MipLevels = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        ThrowIfFailed(
            m_Device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &textureDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&uploadedTexture.texture)),
            "DX12 CreateTexture2D: texture resource creation failed");

        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(uploadedTexture.texture.Get(), 0, 1);
        Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
        D3D12_HEAP_PROPERTIES uploadHeap{};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
        ThrowIfFailed(
            m_Device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &uploadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&uploadBuffer)),
            "DX12 CreateTexture2D: upload buffer creation failed");

        D3D12_SUBRESOURCE_DATA subresourceData{};
        subresourceData.pData = textureData.pixels.data();
        subresourceData.RowPitch = static_cast<LONG_PTR>(textureData.width * 4);
        subresourceData.SlicePitch = subresourceData.RowPitch * textureData.height;

        ExecuteImmediateCommandList(
            m_Device.Get(),
            m_Queue.Get(),
            m_Fence.Get(),
            m_FenceEvent,
            m_FenceValue,
            [&](ID3D12GraphicsCommandList* commandList)
            {
                UpdateSubresources(commandList, uploadedTexture.texture.Get(), uploadBuffer.Get(), 0, 0, 1, &subresourceData);

                const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                    uploadedTexture.texture.Get(),
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                commandList->ResourceBarrier(1, &barrier);
            });

        uploadedTexture.cpuSrvHandle = m_SrvHeap->GetCPUDescriptorHandleForHeapStart();
        uploadedTexture.cpuSrvHandle.ptr += static_cast<SIZE_T>(uploadedTexture.descriptorIndex) * m_SrvDescriptorSize;
        uploadedTexture.gpuSrvHandle = m_SrvHeap->GetGPUDescriptorHandleForHeapStart();
        uploadedTexture.gpuSrvHandle.ptr += static_cast<UINT64>(uploadedTexture.descriptorIndex) * m_SrvDescriptorSize;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_Device->CreateShaderResourceView(uploadedTexture.texture.Get(), &srvDesc, uploadedTexture.cpuSrvHandle);

        const RenderTextureHandle handle{ m_NextTextureHandle++ };
        m_UploadedTextures.emplace(handle.value, std::move(uploadedTexture));

        Logger::Get().Info(
            "DX12 CreateTexture2D: handle=" + std::to_string(handle.value) +
            " size=" + std::to_string(textureData.width) + "x" + std::to_string(textureData.height));
        return handle;
    }
    catch (const std::exception& e)
    {
        if (descriptorReserved)
            m_FreeTextureDescriptorIndices.push_back(reservedDescriptorIndex);
        Logger::Get().Error(std::string("DX12 CreateTexture2D exception: ") + e.what());
        return RenderTextureHandle::Invalid();
    }
}

void Dx12RenderAdapter::DestroyTexture(RenderTextureHandle handle)
{
    if (!handle.IsValid())
    {
        Logger::Get().Warn("DX12 DestroyTexture: invalid handle");
        return;
    }

    const auto it = m_UploadedTextures.find(handle.value);
    if (it == m_UploadedTextures.end())
    {
        Logger::Get().Warn("DX12 DestroyTexture: texture handle was not found");
        return;
    }

    if (m_Device)
        WaitForGpu();

    if (m_BoundTextureHandle == handle)
        m_BoundTextureHandle = RenderTextureHandle::Invalid();

    m_FreeTextureDescriptorIndices.push_back(it->second.descriptorIndex);
    m_UploadedTextures.erase(it);
    Logger::Get().Info("DX12 DestroyTexture: handle=" + std::to_string(handle.value));
}

void Dx12RenderAdapter::BindTexture(std::uint32_t slot, RenderTextureHandle handle)
{
    if (slot != 0)
    {
        Logger::Get().Warn("DX12 BindTexture: only slot 0 is currently supported");
        return;
    }

    if (!handle.IsValid())
    {
        Logger::Get().Warn("DX12 BindTexture: invalid handle");
        return;
    }

    if (m_UploadedTextures.find(handle.value) == m_UploadedTextures.end())
    {
        Logger::Get().Warn("DX12 BindTexture: texture handle was not found");
        return;
    }

    if (m_BoundTextureHandle != handle)
    {
        m_BoundTextureHandle = handle;
    }
}

RenderShaderHandle Dx12RenderAdapter::CreateShaderProgram(const ShaderResource& shaderResource)
{
    if (!m_Device)
    {
        Logger::Get().Warn("DX12 CreateShaderProgram: device is not initialized");
        return RenderShaderHandle::Invalid();
    }

    if (shaderResource.language != "hlsl")
    {
        Logger::Get().Warn("DX12 CreateShaderProgram: only HLSL source is supported");
        return RenderShaderHandle::Invalid();
    }

    if (shaderResource.vertexSource.empty() || shaderResource.fragmentSource.empty())
    {
        Logger::Get().Warn("DX12 CreateShaderProgram: shader source is empty");
        return RenderShaderHandle::Invalid();
    }

    try
    {
        UploadedShader uploadedShader;
        const auto vs = CompileHlsl(shaderResource.vertexSource, shaderResource.vertexPath.c_str(), "VSMain", "vs_5_1");
        const auto ps = CompileHlsl(shaderResource.fragmentSource, shaderResource.fragmentPath.c_str(), "PSMain", "ps_5_1");

        CD3DX12_DESCRIPTOR_RANGE descriptorRange;
        descriptorRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

        D3D12_ROOT_PARAMETER rootParameters[2]{};
        rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameters[0].Descriptor.ShaderRegister = 0;
        rootParameters[0].Descriptor.RegisterSpace = 0;
        rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[1].DescriptorTable.pDescriptorRanges = &descriptorRange;
        rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        CD3DX12_STATIC_SAMPLER_DESC staticSampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
        staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

        D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
        rootSignatureDesc.NumParameters = _countof(rootParameters);
        rootSignatureDesc.pParameters = rootParameters;
        rootSignatureDesc.NumStaticSamplers = 1;
        rootSignatureDesc.pStaticSamplers = &staticSampler;
        rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSignature;
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
        ThrowIfFailed(
            D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSignature, &errorBlob),
            "DX12 CreateShaderProgram: root signature serialization failed");
        ThrowIfFailed(
            m_Device->CreateRootSignature(
                0,
                serializedRootSignature->GetBufferPointer(),
                serializedRootSignature->GetBufferSize(),
                IID_PPV_ARGS(&uploadedShader.rootSignature)),
            "DX12 CreateShaderProgram: root signature creation failed");

        D3D12_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.InputLayout = { layout, _countof(layout) };
        psoDesc.pRootSignature = uploadedShader.rootSignature.Get();
        psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc.Count = 1;

        ThrowIfFailed(
            m_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&uploadedShader.pipelineState)),
            "DX12 CreateShaderProgram: pipeline creation failed");

        const RenderShaderHandle handle{ m_NextShaderHandle++ };
        m_UploadedShaders.emplace(handle.value, std::move(uploadedShader));
        Logger::Get().Info("DX12 CreateShaderProgram: handle=" + std::to_string(handle.value) + " name=" + shaderResource.name);
        return handle;
    }
    catch (const std::exception& e)
    {
        Logger::Get().Error(std::string("DX12 CreateShaderProgram exception: ") + e.what());
        return RenderShaderHandle::Invalid();
    }
}

void Dx12RenderAdapter::DestroyShader(RenderShaderHandle handle)
{
    if (!handle.IsValid())
    {
        Logger::Get().Warn("DX12 DestroyShader: invalid handle");
        return;
    }

    const auto it = m_UploadedShaders.find(handle.value);
    if (it == m_UploadedShaders.end())
    {
        Logger::Get().Warn("DX12 DestroyShader: shader handle was not found");
        return;
    }

    if (m_Device)
        WaitForGpu();

    if (m_BoundShaderHandle == handle)
        m_BoundShaderHandle = RenderShaderHandle::Invalid();

    m_UploadedShaders.erase(it);
    Logger::Get().Info("DX12 DestroyShader: handle=" + std::to_string(handle.value));
}

void Dx12RenderAdapter::BindShader(RenderShaderHandle handle)
{
    if (!handle.IsValid())
    {
        Logger::Get().Warn("DX12 BindShader: invalid handle");
        return;
    }

    if (m_UploadedShaders.find(handle.value) == m_UploadedShaders.end())
    {
        Logger::Get().Warn("DX12 BindShader: shader handle was not found");
        return;
    }

    if (m_BoundShaderHandle != handle)
    {
        m_BoundShaderHandle = handle;
    }
}

void Dx12RenderAdapter::DrawMesh(RenderMeshHandle handle)
{
    if (!handle.IsValid())
    {
        Logger::Get().Warn("DX12 DrawMesh: invalid handle");
        return;
    }

    const auto it = m_UploadedMeshes.find(handle.value);
    if (it == m_UploadedMeshes.end())
    {
        Logger::Get().Warn("DX12 DrawMesh: mesh handle was not found");
        return;
    }

    if (!m_BoundShaderHandle.IsValid())
    {
        Logger::Get().Warn("DX12 DrawMesh: no shader is currently bound");
        return;
    }

    const auto shaderIt = m_UploadedShaders.find(m_BoundShaderHandle.value);
    if (shaderIt == m_UploadedShaders.end())
    {
        Logger::Get().Warn("DX12 DrawMesh: bound shader handle was not found");
        return;
    }

    if (!m_BoundTextureHandle.IsValid())
    {
        Logger::Get().Warn("DX12 DrawMesh: no texture is currently bound");
        return;
    }

    const auto textureIt = m_UploadedTextures.find(m_BoundTextureHandle.value);
    if (textureIt == m_UploadedTextures.end())
    {
        Logger::Get().Warn("DX12 DrawMesh: bound texture handle was not found");
        return;
    }

    struct alignas(256) CB { float mvp[16]; float color[4]; };
    CB cb{};
    memcpy(cb.mvp, m_PendingMVP, sizeof(cb.mvp));
    memcpy(cb.color, m_PendingColor, sizeof(cb.color));

    const UINT cbSlot = (m_DrawCbIndex < MaxDrawsPerFrame) ? m_DrawCbIndex : (MaxDrawsPerFrame - 1);
    memcpy(m_CbMapped[m_FrameIndex][cbSlot], &cb, sizeof(CB));

    ID3D12DescriptorHeap* descriptorHeaps[] = { m_SrvHeap.Get() };
    m_CmdList->SetDescriptorHeaps(1, descriptorHeaps);
    m_CmdList->SetGraphicsRootSignature(shaderIt->second.rootSignature.Get());
    m_CmdList->SetPipelineState(shaderIt->second.pipelineState.Get());
    m_CmdList->SetGraphicsRootConstantBufferView(0, m_CbGpu[m_FrameIndex][cbSlot]);
    m_CmdList->SetGraphicsRootDescriptorTable(1, textureIt->second.gpuSrvHandle);
    m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_CmdList->IASetVertexBuffers(0, 1, &it->second.vertexView);
    m_CmdList->IASetIndexBuffer(&it->second.indexView);
    m_CmdList->DrawIndexedInstanced(it->second.indexCount, 1, 0, 0, 0);

    if (m_DrawCbIndex < MaxDrawsPerFrame)
        ++m_DrawCbIndex;
}

bool Dx12RenderAdapter::Initialize(IWindow* window)
{
    try
    {
        auto* glfwWin = static_cast<GlfwWindow*>(window);
        HWND hwnd = (HWND)glfwGetWin32Window(glfwWin->GetGlfwHandle());
        m_Hwnd = hwnd;

        UINT dxgiFlags = 0;
#if defined(_DEBUG)
#endif

        ThrowIfFailed(CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&m_Factory)), "CreateDXGIFactory2 failed");

        if (!CreateDevice(hwnd)) return false;
        if (!CreateCommandObjects()) return false;
        if (!CreateSwapchain(hwnd)) return false;
        if (!CreateRtvHeapAndTargets()) return false;
        if (!CreateDepthResources()) return false;
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

bool Dx12RenderAdapter::InitializeEditorUi(IWindow* window)
{
    if (m_EditorUiInitialized)
        return true;

    auto* glfwWin = dynamic_cast<GlfwWindow*>(window);
    if (glfwWin == nullptr || glfwWin->GetGlfwHandle() == nullptr || !m_Device || !m_Queue || !m_SrvHeap)
        return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForOther(glfwWin->GetGlfwHandle(), true))
    {
        ImGui::DestroyContext();
        return false;
    }

    ImGui_ImplDX12_InitInfo initInfo;
    initInfo.Device = m_Device.Get();
    initInfo.CommandQueue = m_Queue.Get();
    initInfo.NumFramesInFlight = FrameCount;
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    initInfo.SrvDescriptorHeap = m_SrvHeap.Get();
    initInfo.SrvDescriptorAllocFn = &Dx12RenderAdapter::AllocateImGuiDescriptor;
    initInfo.SrvDescriptorFreeFn = &Dx12RenderAdapter::FreeImGuiDescriptor;
    initInfo.UserData = this;

    if (!ImGui_ImplDX12_Init(&initInfo))
    {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    m_EditorUiInitialized = true;
    Logger::Get().Info("DX12 editor UI initialized");
    return true;
}

void Dx12RenderAdapter::BeginEditorUiFrame()
{
    if (!m_EditorUiInitialized)
        return;

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Dx12RenderAdapter::RenderEditorUiFrame()
{
    if (!m_EditorUiInitialized)
        return;

    ImGui::Render();
    ID3D12DescriptorHeap* descriptorHeaps[] = { m_SrvHeap.Get() };
    m_CmdList->SetDescriptorHeaps(1, descriptorHeaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_CmdList.Get());
}

bool Dx12RenderAdapter::EnsureViewportResources(UINT width, UINT height)
{
    if (width == 0 || height == 0 || !m_Device || !m_SrvHeap)
        return false;

    if (m_ViewportTarget.color &&
        m_ViewportTarget.width == width &&
        m_ViewportTarget.height == height)
    {
        return true;
    }

    if (m_Device)
        WaitForGpu();

    ReleaseViewportResources();

    if (m_FreeTextureDescriptorIndices.empty())
    {
        Logger::Get().Warn("DX12 viewport: no free SRV descriptors remain");
        return false;
    }

    m_ViewportTarget.srvDescriptorIndex = m_FreeTextureDescriptorIndices.back();
    m_FreeTextureDescriptorIndices.pop_back();
    m_ViewportTarget.width = width;
    m_ViewportTarget.height = height;

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(
        m_Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_ViewportTarget.rtvHeap)),
        "DX12 viewport RTV heap creation failed");
    m_ViewportTarget.rtv = m_ViewportTarget.rtvHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(
        m_Device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_ViewportTarget.dsvHeap)),
        "DX12 viewport DSV heap creation failed");
    m_ViewportTarget.dsv = m_ViewportTarget.dsvHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_RESOURCE_DESC colorDesc{};
    colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    colorDesc.Width = width;
    colorDesc.Height = height;
    colorDesc.DepthOrArraySize = 1;
    colorDesc.MipLevels = 1;
    colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE colorClear{};
    colorClear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorClear.Color[0] = m_ClearColor[0];
    colorClear.Color[1] = m_ClearColor[1];
    colorClear.Color[2] = m_ClearColor[2];
    colorClear.Color[3] = m_ClearColor[3];

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ThrowIfFailed(
        m_Device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &colorDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &colorClear,
            IID_PPV_ARGS(&m_ViewportTarget.color)),
        "DX12 viewport color target creation failed");
    m_Device->CreateRenderTargetView(m_ViewportTarget.color.Get(), nullptr, m_ViewportTarget.rtv);

    D3D12_RESOURCE_DESC depthDesc{};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE depthClear{};
    depthClear.Format = DXGI_FORMAT_D32_FLOAT;
    depthClear.DepthStencil.Depth = 1.0f;
    depthClear.DepthStencil.Stencil = 0;
    ThrowIfFailed(
        m_Device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &depthDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &depthClear,
            IID_PPV_ARGS(&m_ViewportTarget.depth)),
        "DX12 viewport depth target creation failed");

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    m_Device->CreateDepthStencilView(m_ViewportTarget.depth.Get(), &dsvDesc, m_ViewportTarget.dsv);

    m_ViewportTarget.srvCpu = m_SrvHeap->GetCPUDescriptorHandleForHeapStart();
    m_ViewportTarget.srvCpu.ptr += static_cast<SIZE_T>(m_ViewportTarget.srvDescriptorIndex) * m_SrvDescriptorSize;
    m_ViewportTarget.srvGpu = m_SrvHeap->GetGPUDescriptorHandleForHeapStart();
    m_ViewportTarget.srvGpu.ptr += static_cast<UINT64>(m_ViewportTarget.srvDescriptorIndex) * m_SrvDescriptorSize;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    m_Device->CreateShaderResourceView(m_ViewportTarget.color.Get(), &srvDesc, m_ViewportTarget.srvCpu);

    Logger::Get().Info(
        "DX12 viewport target resized to " + std::to_string(width) + "x" + std::to_string(height));
    return true;
}

bool Dx12RenderAdapter::BeginViewportRender(int width, int height, const float* clearColor)
{
    const UINT targetWidth = static_cast<UINT>(width > 1 ? width : 1);
    const UINT targetHeight = static_cast<UINT>(height > 1 ? height : 1);
    if (!EnsureViewportResources(targetWidth, targetHeight))
        return false;

    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_ViewportTarget.color.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_CmdList->ResourceBarrier(1, &barrier);

    D3D12_VIEWPORT viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(targetWidth);
    viewport.Height = static_cast<float>(targetHeight);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissor{};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = static_cast<LONG>(targetWidth);
    scissor.bottom = static_cast<LONG>(targetHeight);

    m_CmdList->RSSetViewports(1, &viewport);
    m_CmdList->RSSetScissorRects(1, &scissor);
    m_CmdList->OMSetRenderTargets(1, &m_ViewportTarget.rtv, FALSE, &m_ViewportTarget.dsv);

    const float fallbackClear[4] = { 0.08f, 0.10f, 0.13f, 1.0f };
    const float* chosenClear = clearColor != nullptr ? clearColor : fallbackClear;
    m_CmdList->ClearRenderTargetView(m_ViewportTarget.rtv, chosenClear, 0, nullptr);
    m_CmdList->ClearDepthStencilView(m_ViewportTarget.dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    m_ViewportTarget.rendering = true;
    return true;
}

void Dx12RenderAdapter::EndViewportRender()
{
    if (!m_ViewportTarget.rendering || !m_ViewportTarget.color)
        return;

    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_ViewportTarget.color.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_CmdList->ResourceBarrier(1, &barrier);
    m_ViewportTarget.rendering = false;
}

std::uint64_t Dx12RenderAdapter::GetViewportTextureId() const
{
    return m_ViewportTarget.color ? static_cast<std::uint64_t>(m_ViewportTarget.srvGpu.ptr) : 0;
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

bool Dx12RenderAdapter::CreateDepthResources()
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heapDesc.NumDescriptors = 1;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(m_Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_DsvHeap)), "CreateDescriptorHeap DSV failed");
    m_DsvSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    D3D12_RESOURCE_DESC depthDesc{};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = m_Width;
    depthDesc.Height = m_Height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    ThrowIfFailed(
        m_Device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &depthDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clearValue,
            IID_PPV_ARGS(&m_Depth)),
        "CreateCommittedResource depth failed");

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    m_Device->CreateDepthStencilView(m_Depth.Get(), &dsvDesc, m_DsvHeap->GetCPUDescriptorHandleForHeapStart());
    return true;
}

bool Dx12RenderAdapter::ResizeBackBufferResources(UINT width, UINT height)
{
    if (!m_Swapchain || width == 0 || height == 0)
        return false;

    if (m_Width == width && m_Height == height)
        return true;

    WaitForGpu();

    for (UINT i = 0; i < FrameCount; ++i)
        m_Rt[i].Reset();
    m_Depth.Reset();
    m_RtvHeap.Reset();
    m_DsvHeap.Reset();

    DXGI_SWAP_CHAIN_DESC desc{};
    ThrowIfFailed(m_Swapchain->GetDesc(&desc), "Swapchain GetDesc failed");
    ThrowIfFailed(
        m_Swapchain->ResizeBuffers(FrameCount, width, height, desc.BufferDesc.Format, desc.Flags),
        "Swapchain ResizeBuffers failed");

    m_Width = width;
    m_Height = height;
    m_FrameIndex = m_Swapchain->GetCurrentBackBufferIndex();

    if (!CreateRtvHeapAndTargets())
        return false;
    if (!CreateDepthResources())
        return false;

    Logger::Get().Info("DX12 backbuffer resized to " + std::to_string(width) + "x" + std::to_string(height));
    return true;
}

void Dx12RenderAdapter::ResizeBackBufferIfNeeded()
{
    if (m_Hwnd == nullptr)
        return;

    RECT clientRect{};
    if (!GetClientRect(m_Hwnd, &clientRect))
        return;

    const UINT width = static_cast<UINT>(std::max<LONG>(clientRect.right - clientRect.left, 1));
    const UINT height = static_cast<UINT>(std::max<LONG>(clientRect.bottom - clientRect.top, 1));
    if (width != m_Width || height != m_Height)
        ResizeBackBufferResources(width, height);
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
    D3D12_ROOT_PARAMETER rp{};
    rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp.Descriptor.ShaderRegister = 0; 
    rp.Descriptor.RegisterSpace = 0;
    rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 1;
    rsd.pParameters = &rp;
    rsd.NumStaticSamplers = 0;
    rsd.pStaticSamplers = nullptr;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;


    Microsoft::WRL::ComPtr<ID3DBlob> serialized;
    Microsoft::WRL::ComPtr<ID3DBlob> error;

    ThrowIfFailed(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error),
        "D3D12SerializeRootSignature failed");
    ThrowIfFailed(m_Device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&m_RootSig)),
        "CreateRootSignature failed");

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

    D3D12_GRAPHICS_PIPELINE_STATE_DESC linePso = pso;
    linePso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&linePso, IID_PPV_ARGS(&m_LinePso)), "CreateGraphicsPipelineState line failed");

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.NumDescriptors = MaxTextures;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(
        m_Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_SrvHeap)),
        "CreateDescriptorHeap SRV failed");
    m_SrvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_FreeTextureDescriptorIndices.clear();
    m_FreeTextureDescriptorIndices.reserve(MaxTextures);
    m_FreeImGuiDescriptorIndices.clear();
    m_FreeImGuiDescriptorIndices.push_back(0);
    for (UINT descriptorIndex = 1; descriptorIndex < MaxTextures; ++descriptorIndex)
        m_FreeTextureDescriptorIndices.push_back(MaxTextures - descriptorIndex);

    struct Vtx { float x, y, z; float r, g, b; };
    Vtx tri[3] = {
      { 0.0f,  0.5f, 0.0f, 1,1,1},
      { 0.5f, -0.5f, 0.0f, 1,1,1},
      {-0.5f, -0.5f, 0.0f, 1,1,1},
    };
    Vtx line[2] = {
      {-0.5f, 0.0f, 0.0f, 1,1,1},
      { 0.5f, 0.0f, 0.0f, 1,1,1},
    };
    Vtx quad[6] = {
      {-0.5f,  0.5f, 0.0f, 1,1,1},
      { 0.5f,  0.5f, 0.0f, 1,1,1},
      {-0.5f, -0.5f, 0.0f, 1,1,1},
      {-0.5f, -0.5f, 0.0f, 1,1,1},
      { 0.5f,  0.5f, 0.0f, 1,1,1},
      { 0.5f, -0.5f, 0.0f, 1,1,1},
    };
    Vtx cube[24] = {
      { -0.5f, -0.5f,  0.5f, 1,1,1 }, {  0.5f, -0.5f,  0.5f, 1,1,1 },
      {  0.5f, -0.5f,  0.5f, 1,1,1 }, {  0.5f,  0.5f,  0.5f, 1,1,1 },
      {  0.5f,  0.5f,  0.5f, 1,1,1 }, { -0.5f,  0.5f,  0.5f, 1,1,1 },
      { -0.5f,  0.5f,  0.5f, 1,1,1 }, { -0.5f, -0.5f,  0.5f, 1,1,1 },
      { -0.5f, -0.5f, -0.5f, 1,1,1 }, {  0.5f, -0.5f, -0.5f, 1,1,1 },
      {  0.5f, -0.5f, -0.5f, 1,1,1 }, {  0.5f,  0.5f, -0.5f, 1,1,1 },
      {  0.5f,  0.5f, -0.5f, 1,1,1 }, { -0.5f,  0.5f, -0.5f, 1,1,1 },
      { -0.5f,  0.5f, -0.5f, 1,1,1 }, { -0.5f, -0.5f, -0.5f, 1,1,1 },
      { -0.5f, -0.5f,  0.5f, 1,1,1 }, { -0.5f, -0.5f, -0.5f, 1,1,1 },
      {  0.5f, -0.5f,  0.5f, 1,1,1 }, {  0.5f, -0.5f, -0.5f, 1,1,1 },
      {  0.5f,  0.5f,  0.5f, 1,1,1 }, {  0.5f,  0.5f, -0.5f, 1,1,1 },
      { -0.5f,  0.5f,  0.5f, 1,1,1 }, { -0.5f,  0.5f, -0.5f, 1,1,1 },
    };

    const UINT vbSize = sizeof(tri);
    const UINT lineVbSize = sizeof(line);
    const UINT quadVbSize = sizeof(quad);
    const UINT cubeVbSize = sizeof(cube);

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    CD3DX12_RANGE range(0, 0);

    auto createVertexBuffer = [&](const void* srcData, UINT size, Microsoft::WRL::ComPtr<ID3D12Resource>& resource, D3D12_VERTEX_BUFFER_VIEW& view, const char* errorName)
    {
        D3D12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(size);
        ThrowIfFailed(m_Device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource)),
            errorName);

        void* mapped = nullptr;
        ThrowIfFailed(resource->Map(0, &range, &mapped), "VB Map failed");
        memcpy(mapped, srcData, size);
        resource->Unmap(0, nullptr);

        view.BufferLocation = resource->GetGPUVirtualAddress();
        view.StrideInBytes = sizeof(Vtx);
        view.SizeInBytes = size;
    };

    createVertexBuffer(tri, vbSize, m_VB, m_VbView, "CreateCommittedResource Triangle VB failed");
    createVertexBuffer(line, lineVbSize, m_LineVB, m_LineVbView, "CreateCommittedResource Line VB failed");
    createVertexBuffer(quad, quadVbSize, m_QuadVB, m_QuadVbView, "CreateCommittedResource Quad VB failed");
    createVertexBuffer(cube, cubeVbSize, m_CubeVB, m_CubeVbView, "CreateCommittedResource Cube VB failed");

    struct alignas(256) CB
    {
        float mvp[16];
        float color[4];
    };

    for (UINT frame = 0; frame < FrameCount; ++frame)
    {
        for (UINT draw = 0; draw < MaxDrawsPerFrame; ++draw)
        {
            D3D12_HEAP_PROPERTIES hp{};
            hp.Type = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(sizeof(CB));

            ThrowIfFailed(m_Device->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&m_Cb[frame][draw])
            ), "CreateCommittedResource CB failed");

            CD3DX12_RANGE range(0, 0);
            void* mapped = nullptr;
            ThrowIfFailed(m_Cb[frame][draw]->Map(0, &range, &mapped), "CB Map failed");
            m_CbMapped[frame][draw] = reinterpret_cast<uint8_t*>(mapped);
            m_CbGpu[frame][draw] = m_Cb[frame][draw]->GetGPUVirtualAddress();

            CB init{};
            memcpy(init.mvp, m_PendingMVP, sizeof(init.mvp));
            memcpy(init.color, m_PendingColor, sizeof(init.color));
            memcpy(m_CbMapped[frame][draw], &init, sizeof(CB));
        }
    }


    return true;
}

void Dx12RenderAdapter::BeginFrame()
{
    ResizeBackBufferIfNeeded();

    ThrowIfFailed(m_Allocator[m_FrameIndex]->Reset(), "Allocator Reset failed");
    ThrowIfFailed(m_CmdList->Reset(m_Allocator[m_FrameIndex].Get(), m_Pso.Get()), "CmdList Reset failed");
    m_DrawCbIndex = 0;

    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (float)m_Width;
    vp.Height = (float)m_Height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    D3D12_RECT sc{};
    sc.left = 0;
    sc.top = 0;
    sc.right = (LONG)m_Width;
    sc.bottom = (LONG)m_Height;

    m_CmdList->RSSetViewports(1, &vp);
    m_CmdList->RSSetScissorRects(1, &sc);

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

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_DsvHeap->GetCPUDescriptorHandleForHeapStart();
    m_CmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    m_CmdList->ClearRenderTargetView(rtv, m_ClearColor, 0, nullptr);
    m_CmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void Dx12RenderAdapter::DrawTestTriangle()
{
    struct alignas(256) CB { float mvp[16]; float color[4]; };
    CB cb{};
    memcpy(cb.mvp, m_PendingMVP, sizeof(cb.mvp));
    memcpy(cb.color, m_PendingColor, sizeof(cb.color));

    const UINT cbSlot = (m_DrawCbIndex < MaxDrawsPerFrame) ? m_DrawCbIndex : (MaxDrawsPerFrame - 1);
    memcpy(m_CbMapped[m_FrameIndex][cbSlot], &cb, sizeof(CB));

    m_CmdList->SetGraphicsRootSignature(m_RootSig.Get());
    m_CmdList->SetPipelineState(m_Pso.Get());
    m_CmdList->SetGraphicsRootConstantBufferView(0, m_CbGpu[m_FrameIndex][cbSlot]);

    m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_CmdList->IASetVertexBuffers(0, 1, &m_VbView);
    m_CmdList->DrawInstanced(3, 1, 0, 0);

    if (m_DrawCbIndex < MaxDrawsPerFrame)
        ++m_DrawCbIndex;
}

void Dx12RenderAdapter::DrawTestLine()
{
    struct alignas(256) CB { float mvp[16]; float color[4]; };
    CB cb{};
    memcpy(cb.mvp, m_PendingMVP, sizeof(cb.mvp));
    memcpy(cb.color, m_PendingColor, sizeof(cb.color));

    const UINT cbSlot = (m_DrawCbIndex < MaxDrawsPerFrame) ? m_DrawCbIndex : (MaxDrawsPerFrame - 1);
    memcpy(m_CbMapped[m_FrameIndex][cbSlot], &cb, sizeof(CB));

    m_CmdList->SetGraphicsRootSignature(m_RootSig.Get());
    m_CmdList->SetPipelineState(m_LinePso.Get());
    m_CmdList->SetGraphicsRootConstantBufferView(0, m_CbGpu[m_FrameIndex][cbSlot]);

    m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    m_CmdList->IASetVertexBuffers(0, 1, &m_LineVbView);
    m_CmdList->DrawInstanced(2, 1, 0, 0);

    if (m_DrawCbIndex < MaxDrawsPerFrame)
        ++m_DrawCbIndex;
}

void Dx12RenderAdapter::DrawTestQuad()
{
    struct alignas(256) CB { float mvp[16]; float color[4]; };
    CB cb{};
    memcpy(cb.mvp, m_PendingMVP, sizeof(cb.mvp));
    memcpy(cb.color, m_PendingColor, sizeof(cb.color));

    const UINT cbSlot = (m_DrawCbIndex < MaxDrawsPerFrame) ? m_DrawCbIndex : (MaxDrawsPerFrame - 1);
    memcpy(m_CbMapped[m_FrameIndex][cbSlot], &cb, sizeof(CB));

    m_CmdList->SetGraphicsRootSignature(m_RootSig.Get());
    m_CmdList->SetPipelineState(m_Pso.Get());
    m_CmdList->SetGraphicsRootConstantBufferView(0, m_CbGpu[m_FrameIndex][cbSlot]);

    m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_CmdList->IASetVertexBuffers(0, 1, &m_QuadVbView);
    m_CmdList->DrawInstanced(6, 1, 0, 0);

    if (m_DrawCbIndex < MaxDrawsPerFrame)
        ++m_DrawCbIndex;
}

void Dx12RenderAdapter::DrawTestCube()
{
    struct alignas(256) CB { float mvp[16]; float color[4]; };
    CB cb{};
    memcpy(cb.mvp, m_PendingMVP, sizeof(cb.mvp));
    memcpy(cb.color, m_PendingColor, sizeof(cb.color));

    const UINT cbSlot = (m_DrawCbIndex < MaxDrawsPerFrame) ? m_DrawCbIndex : (MaxDrawsPerFrame - 1);
    memcpy(m_CbMapped[m_FrameIndex][cbSlot], &cb, sizeof(CB));

    m_CmdList->SetGraphicsRootSignature(m_RootSig.Get());
    m_CmdList->SetPipelineState(m_LinePso.Get());
    m_CmdList->SetGraphicsRootConstantBufferView(0, m_CbGpu[m_FrameIndex][cbSlot]);

    m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    m_CmdList->IASetVertexBuffers(0, 1, &m_CubeVbView);
    m_CmdList->DrawInstanced(24, 1, 0, 0);

    if (m_DrawCbIndex < MaxDrawsPerFrame)
        ++m_DrawCbIndex;
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
    ThrowIfFailed(m_Swapchain->Present(0, 0), "Present failed");
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

void Dx12RenderAdapter::AllocateImGuiDescriptor(
    ImGui_ImplDX12_InitInfo* info,
    D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
{
    auto* self = static_cast<Dx12RenderAdapter*>(info->UserData);
    if (self == nullptr || self->m_FreeImGuiDescriptorIndices.empty())
        throw std::runtime_error("DX12 ImGui descriptor allocation failed");

    const UINT descriptorIndex = self->m_FreeImGuiDescriptorIndices.back();
    self->m_FreeImGuiDescriptorIndices.pop_back();

    *outCpuHandle = self->m_SrvHeap->GetCPUDescriptorHandleForHeapStart();
    outCpuHandle->ptr += static_cast<SIZE_T>(descriptorIndex) * self->m_SrvDescriptorSize;
    *outGpuHandle = self->m_SrvHeap->GetGPUDescriptorHandleForHeapStart();
    outGpuHandle->ptr += static_cast<UINT64>(descriptorIndex) * self->m_SrvDescriptorSize;
}

void Dx12RenderAdapter::FreeImGuiDescriptor(
    ImGui_ImplDX12_InitInfo* info,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
{
    (void)cpuHandle;
    (void)gpuHandle;

    auto* self = static_cast<Dx12RenderAdapter*>(info->UserData);
    if (self != nullptr)
        self->m_FreeImGuiDescriptorIndices.push_back(0);
}

void Dx12RenderAdapter::ReleaseViewportResources()
{
    if (m_ViewportTarget.srvDescriptorIndex != UINT_MAX)
    {
        m_FreeTextureDescriptorIndices.push_back(m_ViewportTarget.srvDescriptorIndex);
        m_ViewportTarget.srvDescriptorIndex = UINT_MAX;
    }

    m_ViewportTarget.color.Reset();
    m_ViewportTarget.depth.Reset();
    m_ViewportTarget.rtvHeap.Reset();
    m_ViewportTarget.dsvHeap.Reset();
    m_ViewportTarget.rtv = {};
    m_ViewportTarget.dsv = {};
    m_ViewportTarget.srvCpu = {};
    m_ViewportTarget.srvGpu = {};
    m_ViewportTarget.width = 0;
    m_ViewportTarget.height = 0;
    m_ViewportTarget.rendering = false;
}

void Dx12RenderAdapter::ShutdownEditorUi()
{
    if (!m_EditorUiInitialized)
        return;

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_EditorUiInitialized = false;
    Logger::Get().Info("DX12 editor UI shutdown");
}

void Dx12RenderAdapter::Shutdown()
{
    try
    {
        if (m_Device)
            WaitForGpu();
    }
    catch (...) {}

    ShutdownEditorUi();

    if (m_FenceEvent)
    {
        CloseHandle(m_FenceEvent);
        m_FenceEvent = nullptr;
    }

    for (UINT frame = 0; frame < FrameCount; ++frame)
    {
        for (UINT draw = 0; draw < MaxDrawsPerFrame; ++draw)
        {
            if (m_Cb[frame][draw] && m_CbMapped[frame][draw])
            {
                m_Cb[frame][draw]->Unmap(0, nullptr);
                m_CbMapped[frame][draw] = nullptr;
            }
            m_Cb[frame][draw].Reset();
        }
    }

    m_UploadedMeshes.clear();
    m_UploadedTextures.clear();
    m_UploadedShaders.clear();
    ReleaseViewportResources();
    m_FreeTextureDescriptorIndices.clear();
    m_FreeImGuiDescriptorIndices.clear();
    m_BoundShaderHandle = RenderShaderHandle::Invalid();
    m_BoundTextureHandle = RenderTextureHandle::Invalid();
    m_VB.Reset();
    m_LineVB.Reset();
    m_QuadVB.Reset();
    m_CubeVB.Reset();
    for (UINT i = 0; i < FrameCount; ++i) m_Rt[i].Reset();
    m_Depth.Reset();
    m_DsvHeap.Reset();
    m_SrvHeap.Reset();
    m_RtvHeap.Reset();
    m_Swapchain.Reset();
    m_CmdList.Reset();
    for (UINT i = 0; i < FrameCount; ++i) m_Allocator[i].Reset();
    m_Queue.Reset();
    m_Pso.Reset();
    m_LinePso.Reset();
    m_RootSig.Reset();
    m_Fence.Reset();
    m_Device.Reset();
    m_Factory.Reset();

    Logger::Get().Info("DX12 adapter shutdown");
}

#endif
