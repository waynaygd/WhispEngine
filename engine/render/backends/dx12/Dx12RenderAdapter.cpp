#include <chrono>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Dx12RenderAdapter.h"
#include "../../../editor/EditorTheme.h"
#ifdef _WIN32

// Dynamic ImGui font atlas resizing can retain an old texture for in-flight frames.
static constexpr unsigned ExtraImGuiDescriptors=8;

#include "../../../core/Logger.h"
#include "../../../core/AssetPaths.h"
#include <d3d12sdklayers.h>
#include "../../../platform/GlfwWindow.h"   
#include "../../../resources/MeshData.h"
#include "../../../resources/TangentSpace.h"
#include "../../../resources/ShaderResource.h"
#include "../../../resources/TextureData.h"

#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#else
#define ZoneScopedN(name) ((void)0)
#endif
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3d12shader.h>
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
#include <DirectXMath.h>


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
        MeshData prepared=meshData;TangentSpace::Generate(prepared);
        const auto& vertices=prepared.vertices;
        UploadedMesh uploadedMesh;
        const UINT vertexBufferSize = static_cast<UINT>(vertices.size() * sizeof(MeshVertex));
        const UINT indexBufferSize = static_cast<UINT>(meshData.indices.size() * sizeof(std::uint32_t));

        CreateUploadBuffer(
            m_Device.Get(),
            vertices.data(),
            vertexBufferSize,
            uploadedMesh.vertexBuffer,
            "DX12 UploadMesh: create vertex buffer failed");

        CreateUploadBuffer(
            m_Device.Get(),
            prepared.indices.data(),
            indexBufferSize,
            uploadedMesh.indexBuffer,
            "DX12 UploadMesh: create index buffer failed");

        uploadedMesh.vertexView.BufferLocation = uploadedMesh.vertexBuffer->GetGPUVirtualAddress();
        uploadedMesh.vertexView.StrideInBytes = sizeof(MeshVertex);
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

    if(m_FrameRecording) {
        m_RetiredResources[m_FrameIndex].emplace_back(it->second.vertexBuffer.Get());
        m_RetiredResources[m_FrameIndex].emplace_back(it->second.indexBuffer.Get());
    } else if(m_Device)WaitForGpu();

    m_UploadedMeshes.erase(it);
    Logger::Get().Info("DX12 DestroyMesh: handle=" + std::to_string(handle.value));
}

RenderTextureHandle Dx12RenderAdapter::CreateTexture2D(const TextureData& textureData)
{
    return CreateTexture(textureData, false);
}

RenderTextureHandle Dx12RenderAdapter::CreateTexture(const TextureData& textureData, bool renderTarget, bool deferred)
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
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        textureDesc.Flags = renderTarget ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE;
        uploadedTexture.allocationBytes=m_Device->GetResourceAllocationInfo(0,1,&textureDesc).SizeInBytes;

        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        ThrowIfFailed(
            m_Device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &textureDesc,
                renderTarget ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COPY_DEST,
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

        auto upload=[&](ID3D12GraphicsCommandList* commandList)
            {
                UpdateSubresources(commandList, uploadedTexture.texture.Get(), uploadBuffer.Get(), 0, 0, 1, &subresourceData);

                const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                    uploadedTexture.texture.Get(),
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                commandList->ResourceBarrier(1, &barrier);
            };
        if(!renderTarget){
            if(deferred&&m_FrameRecording){upload(m_CmdList.Get());m_RetiredResources[m_FrameIndex].emplace_back(uploadBuffer.Get());}
            else ExecuteImmediateCommandList(m_Device.Get(),m_Queue.Get(),m_Fence.Get(),m_FenceEvent,m_FenceValue,upload);
        }

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

RenderTextureHandle Dx12RenderAdapter::RenderMeshThumbnail(const MeshData& data, RenderShaderHandle shader, RenderTextureHandle white)
{
    using namespace DirectX;
    if (!m_FrameRecording || m_ViewportTarget.rendering || m_HdrActive ||
        !m_UploadedShaders.count(shader.value) || !m_UploadedTextures.count(white.value) ||
        data.vertices.empty() || data.indices.empty() || m_DrawCbIndex >= MaxDrawsPerFrame)
        return {};

    XMFLOAT3 lo{FLT_MAX,FLT_MAX,FLT_MAX}, hi{-FLT_MAX,-FLT_MAX,-FLT_MAX};
    for (const auto& v : data.vertices) {
        for (float p : v.position) if (!std::isfinite(p)) return {};
        lo.x=std::min(lo.x,v.position[0]); lo.y=std::min(lo.y,v.position[1]); lo.z=std::min(lo.z,v.position[2]);
        hi.x=std::max(hi.x,v.position[0]); hi.y=std::max(hi.y,v.position[1]); hi.z=std::max(hi.z,v.position[2]);
    }
    // Normalize before constructing the camera: very small and off-origin assets fit alike.
    const auto center=XMVectorScale(XMVectorAdd(XMLoadFloat3(&lo),XMLoadFloat3(&hi)),.5f);
    float radius=0;
    for(const auto& v:data.vertices){
        const auto point=XMVectorSet(v.position[0],v.position[1],v.position[2],0);
        radius=std::max(radius,XMVectorGetX(XMVector3Length(XMVectorSubtract(point,center))));
    }
    if (!std::isfinite(radius) || radius < 1e-12f) return {};
    const auto model=XMMatrixTranslationFromVector(XMVectorNegate(center))*XMMatrixScaling(1/radius,1/radius,1/radius);
    const auto eye=XMVectorSet(2.4f,1.7f,-2.8f,1);
    const auto mvp=model*XMMatrixLookAtLH(eye,XMVectorZero(),XMVectorSet(0,1,0,0))*XMMatrixPerspectiveFovLH(XM_PIDIV4,1,.1f,10);

    RenderTextureHandle result{}; RenderMeshHandle mesh{};
    Microsoft::WRL::ComPtr<ID3D12Resource> depth;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap,dsvHeap;
    try {
        TextureData pixels; pixels.width=pixels.height=256; pixels.pixels.resize(256*256*4);
        result=CreateTexture(pixels,true); if(!result.IsValid())return {};
        mesh=UploadMesh(data); if(!mesh.IsValid()){DestroyTexture(result);return {};}
        D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.NumDescriptors=1; heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        ThrowIfFailed(m_Device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&rtvHeap)),"Thumbnail RTV heap");
        heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        ThrowIfFailed(m_Device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&dsvHeap)),"Thumbnail DSV heap");
        auto desc=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT,256,256,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        D3D12_HEAP_PROPERTIES props{}; props.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_CLEAR_VALUE clear{}; clear.Format=DXGI_FORMAT_D32_FLOAT; clear.DepthStencil.Depth=1;
        ThrowIfFailed(m_Device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&depth)),"Thumbnail depth");
        auto* color=m_UploadedTextures.at(result.value).texture.Get();
        D3D12_RENDER_TARGET_VIEW_DESC view{}; view.Format=DXGI_FORMAT_R8G8B8A8_UNORM; view.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
        const auto rtv=rtvHeap->GetCPUDescriptorHandleForHeapStart(),dsv=dsvHeap->GetCPUDescriptorHandleForHeapStart();
        m_Device->CreateRenderTargetView(color,&view,rtv); m_Device->CreateDepthStencilView(depth.Get(),nullptr,dsv);
        auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(color,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_CmdList->ResourceBarrier(1,&barrier);
        D3D12_VIEWPORT viewport{0,0,256,256,0,1}; D3D12_RECT rect{0,0,256,256};
        m_CmdList->RSSetViewports(1,&viewport); m_CmdList->RSSetScissorRects(1,&rect); m_CmdList->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
        const float background[]={.085f,.095f,.115f,1}; m_CmdList->ClearRenderTargetView(rtv,background,0,nullptr); m_CmdList->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);

        // Reuse the ordinary material renderer. Preserve scene state and use its own
        // immutable constant slices; no scene entities, shadows, fog or IBL are changed.
        const auto oldShader=m_BoundShaderHandle; const auto oldTexture=m_BoundTextureHandle;
        const auto oldMaps=m_BoundMaps; const auto oldModel=m_Model; const auto oldNormal=m_Normal;
        const auto oldBase=m_BaseColor; const auto oldSurface=m_Surface; const auto oldParams=m_TextureParameters;
        const auto oldCamera=m_Camera; const auto oldLighting=m_Lighting; const auto oldRange=m_MeshRange;
        const auto oldMaterial=m_MaterialState; const auto oldEnv=m_EnvironmentSettings;
        const auto oldShadows=m_ShadowConstants; const auto oldLightPass=m_ActiveLightPass;
        const auto oldLights=m_PendingLights; const auto oldStats=m_SubmissionStatistics;
        const auto oldDiagnostics=m_SceneLightDiagnostics;
        const auto oldWidth=m_ViewportTarget.width,oldHeight=m_ViewportTarget.height;
        const bool oldRendering=m_ViewportTarget.rendering;
        float oldMvp[16],oldColor[4]; std::memcpy(oldMvp,m_PendingMVP,sizeof(oldMvp)); std::memcpy(oldColor,m_PendingColor,sizeof(oldColor));
        struct Restore {std::function<void()> action;~Restore(){action();}} restore{[=,this]{
            m_BoundShaderHandle=oldShader; m_BoundTextureHandle=oldTexture; m_BoundMaps=oldMaps; m_Model=oldModel; m_Normal=oldNormal;
            m_BaseColor=oldBase; m_Surface=oldSurface; m_TextureParameters=oldParams; m_Camera=oldCamera; m_Lighting=oldLighting;
            m_MeshRange=oldRange; m_MaterialState=oldMaterial; m_EnvironmentSettings=oldEnv; m_ShadowConstants=oldShadows;
            m_ActiveLightPass=oldLightPass; m_PendingLights=oldLights; m_SubmissionStatistics=oldStats; m_FrameConstantsDirty=true; m_LastMaterialTable={};
            m_ViewportTarget.width=oldWidth;m_ViewportTarget.height=oldHeight;m_ViewportTarget.rendering=oldRendering;m_SceneLightDiagnostics=oldDiagnostics;
            SetTestTransform(oldMvp); SetTestColor(oldColor[0],oldColor[1],oldColor[2],oldColor[3]);
        }};
        // Light buffers use the dimensions of this offscreen pass, not the desktop.
        m_ViewportTarget.width=m_ViewportTarget.height=256;m_ViewportTarget.rendering=true;
        XMFLOAT4X4 matrix; XMStoreFloat4x4(&matrix,mvp); SetTestTransform(&matrix._11);
        XMStoreFloat4x4(&matrix,model); std::memcpy(m_Model.data(),&matrix,sizeof(matrix));
        m_Normal=BuildNormalMatrix(m_Model); m_BaseColor={.72f,.77f,.84f,1}; m_Surface={0,.65f,0,0}; m_TextureParameters={1,1,0,1};
        SetTestColor(1,1,1,1); m_Camera={2.4f,1.7f,-2.8f,0}; m_Lighting=BuildLightingConstants(LightingSettings{});
        m_MeshRange={}; m_MaterialState={true,false,false}; m_BoundMaps={};
        m_EnvironmentSettings.enabled=false; m_ShadowConstants={}; m_PendingLights.clear(); m_ActiveLightPass=UINT_MAX; m_FrameConstantsDirty=true;
        BindShader(shader); BindTexture(0,white); DrawMesh(mesh);
        if(m_SubmissionStatistics.drawCalls==oldStats.drawCalls)throw std::runtime_error("Thumbnail draw was not submitted");
        barrier=CD3DX12_RESOURCE_BARRIER::Transition(color,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_CmdList->ResourceBarrier(1,&barrier);
    } catch(const std::exception& e) {
        Logger::Get().Warn(std::string("Mesh thumbnail: ")+e.what());
        if(result.IsValid())DestroyTexture(result); result={};
    }
    // These allocations are referenced by this frame's command list.
    if(depth)m_RetiredResources[m_FrameIndex].emplace_back(depth.Get());
    if(rtvHeap)m_RetiredResources[m_FrameIndex].emplace_back(rtvHeap.Get());
    if(dsvHeap)m_RetiredResources[m_FrameIndex].emplace_back(dsvHeap.Get());
    if(mesh.IsValid())DestroyMesh(mesh);
    return result;
}

void Dx12RenderAdapter::ReleaseMaterialPreview(){
    if(m_MaterialPreviewColor.IsValid())DestroyTexture(m_MaterialPreviewColor);
    m_MaterialPreviewColor={};
    auto& t=m_MaterialPreviewTarget;
    if(m_FrameRecording){if(t.depth)m_RetiredResources[m_FrameIndex].emplace_back(t.depth.Get());if(t.rtvHeap)m_RetiredResources[m_FrameIndex].emplace_back(t.rtvHeap.Get());if(t.dsvHeap)m_RetiredResources[m_FrameIndex].emplace_back(t.dsvHeap.Get());}
    t={};m_MaterialPreviewMetrics.targetBytes=0;
}
std::uint64_t Dx12RenderAdapter::RenderMaterialPreview(const MaterialPreviewFrame& f){
    using namespace DirectX;const auto start=std::chrono::steady_clock::now();
    struct Timer{double& ms;std::chrono::steady_clock::time_point start;~Timer(){ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}} timer{m_MaterialPreviewMetrics.submissionMs,start};
    if(!m_FrameRecording||m_ViewportTarget.rendering||m_HdrActive||!m_UploadedMeshes.count(f.mesh.value)||!m_UploadedShaders.count(f.shader.value))return 0;
    auto& target=m_MaterialPreviewTarget;const UINT width=std::clamp(f.width,64u,1536u),height=std::clamp(f.height,64u,1536u);
    if(target.width!=width||target.height!=height){
        ReleaseMaterialPreview();TextureData pixels;pixels.width=width;pixels.height=height;pixels.pixels.resize(std::size_t(width)*height*4);
        m_MaterialPreviewColor=CreateTexture(pixels,true);if(!m_MaterialPreviewColor.IsValid())return 0;
        D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.NumDescriptors=1;heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;ThrowIfFailed(m_Device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&target.rtvHeap)),"Material preview RTV");heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;ThrowIfFailed(m_Device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&target.dsvHeap)),"Material preview DSV");
        auto desc=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT,width,height,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);D3D12_HEAP_PROPERTIES props{};props.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT;clear.DepthStencil.Depth=1;
        ThrowIfFailed(m_Device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&target.depth)),"Material preview depth");
        target.rtv=target.rtvHeap->GetCPUDescriptorHandleForHeapStart();target.dsv=target.dsvHeap->GetCPUDescriptorHandleForHeapStart();D3D12_RENDER_TARGET_VIEW_DESC view{};view.Format=DXGI_FORMAT_R8G8B8A8_UNORM;view.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
        m_Device->CreateRenderTargetView(m_UploadedTextures.at(m_MaterialPreviewColor.value).texture.Get(),&view,target.rtv);m_Device->CreateDepthStencilView(target.depth.Get(),nullptr,target.dsv);target.width=width;target.height=height;++m_MaterialPreviewMetrics.allocations;m_MaterialPreviewMetrics.targetBytes=std::uint64_t(width)*height*8;
    }
    if(!m_MaterialPreviewTime){
        D3D12_QUERY_HEAP_DESC query{};query.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;query.Count=FrameCount*2;ThrowIfFailed(m_Device->CreateQueryHeap(&query,IID_PPV_ARGS(&m_MaterialPreviewTime)),"Material preview time");D3D12_HEAP_PROPERTIES props{};props.Type=D3D12_HEAP_TYPE_READBACK;auto desc=CD3DX12_RESOURCE_DESC::Buffer(FrameCount*16);ThrowIfFailed(m_Device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&m_MaterialPreviewReadback)),"Material preview readback");
    }
    if(m_MaterialPreviewTimed[m_FrameIndex]){UINT64* ticks=nullptr;const SIZE_T offset=m_FrameIndex*16;D3D12_RANGE range{offset,offset+16};if(SUCCEEDED(m_MaterialPreviewReadback->Map(0,&range,reinterpret_cast<void**>(&ticks)))){UINT64 frequency=0;m_Queue->GetTimestampFrequency(&frequency);if(frequency&&ticks[m_FrameIndex*2+1]>=ticks[m_FrameIndex*2])m_MaterialPreviewMetrics.gpuMs=double(ticks[m_FrameIndex*2+1]-ticks[m_FrameIndex*2])*1000/frequency;D3D12_RANGE written{0,0};m_MaterialPreviewReadback->Unmap(0,&written);}}
    m_CmdList->EndQuery(m_MaterialPreviewTime.Get(),D3D12_QUERY_TYPE_TIMESTAMP,m_FrameIndex*2);
    auto* color=m_UploadedTextures.at(m_MaterialPreviewColor.value).texture.Get();auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(color,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);m_CmdList->ResourceBarrier(1,&barrier);
    D3D12_VIEWPORT viewport{0,0,float(width),float(height),0,1};D3D12_RECT rect{0,0,LONG(width),LONG(height)};m_CmdList->RSSetViewports(1,&viewport);m_CmdList->RSSetScissorRects(1,&rect);m_CmdList->OMSetRenderTargets(1,&target.rtv,FALSE,&target.dsv);const float value=f.dark?.025f:.12f;const float background[]={value,value,value,1};m_CmdList->ClearRenderTargetView(target.rtv,background,0,nullptr);m_CmdList->ClearDepthStencilView(target.dsv,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
        const auto oldShader=m_BoundShaderHandle; const auto oldTexture=m_BoundTextureHandle;
        const auto oldMaps=m_BoundMaps; const auto oldModel=m_Model; const auto oldNormal=m_Normal;
        const auto oldBase=m_BaseColor; const auto oldSurface=m_Surface; const auto oldParams=m_TextureParameters;
        const auto oldCamera=m_Camera; const auto oldLighting=m_Lighting; const auto oldRange=m_MeshRange;
        const auto oldMaterial=m_MaterialState; const auto oldEnv=m_EnvironmentSettings;
        const auto oldShadows=m_ShadowConstants; const auto oldLightPass=m_ActiveLightPass;
        const auto oldMode=m_LightMode;const auto oldLights=m_PendingLights; const auto oldStats=m_SubmissionStatistics;
        const auto oldDiagnostics=m_SceneLightDiagnostics;
        const auto oldWidth=m_ViewportTarget.width,oldHeight=m_ViewportTarget.height;
        const bool oldRendering=m_ViewportTarget.rendering;
        float oldMvp[16],oldColor[4]; std::memcpy(oldMvp,m_PendingMVP,sizeof(oldMvp)); std::memcpy(oldColor,m_PendingColor,sizeof(oldColor));
        struct Restore {std::function<void()> action;~Restore(){action();}} restore{[=,this]{
            m_BoundShaderHandle=oldShader; m_BoundTextureHandle=oldTexture; m_BoundMaps=oldMaps; m_Model=oldModel; m_Normal=oldNormal;
            m_BaseColor=oldBase; m_Surface=oldSurface; m_TextureParameters=oldParams; m_Camera=oldCamera; m_Lighting=oldLighting;
            m_MeshRange=oldRange; m_MaterialState=oldMaterial; m_EnvironmentSettings=oldEnv; m_ShadowConstants=oldShadows;
            m_ActiveLightPass=oldLightPass;m_LightMode=oldMode; m_PendingLights=oldLights; m_SubmissionStatistics=oldStats; m_FrameConstantsDirty=true; m_LastMaterialTable={};
            m_ViewportTarget.width=oldWidth;m_ViewportTarget.height=oldHeight;m_ViewportTarget.rendering=oldRendering;m_SceneLightDiagnostics=oldDiagnostics;
            SetTestTransform(oldMvp); SetTestColor(oldColor[0],oldColor[1],oldColor[2],oldColor[3]);
        }};

    m_ViewportTarget.width=width;m_ViewportTarget.height=height;m_ViewportTarget.rendering=true;
    const auto focus=XMVectorSet(f.panX,f.panY,0,1);const auto eye=XMVectorAdd(focus,XMVectorSet(std::sin(f.yaw)*std::cos(f.pitch)*f.distance,std::sin(f.pitch)*f.distance,-std::cos(f.yaw)*std::cos(f.pitch)*f.distance,0));
    const auto model=XMMatrixIdentity(),mvp=model*XMMatrixLookAtLH(eye,focus,XMVectorSet(0,1,0,0))*XMMatrixPerspectiveFovLH(XM_PIDIV4,float(width)/height,.05f,100.f);XMFLOAT4X4 matrix;XMStoreFloat4x4(&matrix,mvp);SetTestTransform(&matrix._11);XMStoreFloat4x4(&matrix,model);std::memcpy(m_Model.data(),&matrix,sizeof(matrix));m_Normal=BuildNormalMatrix(m_Model);
    const auto& material=f.material;m_BaseColor={material.baseColor[0],material.baseColor[1],material.baseColor[2],material.baseColor[3]};m_Surface={material.metallic,material.roughness,float(material.shadingModel),0};unsigned mask=0;for(int i=1;i<4;++i)if(f.textures[i].IsValid()&&material.textureTexCoords[i]==0)mask|=1u<<(i-1);m_TextureParameters={material.normalScale,material.occlusionStrength,float(mask),1};if(f.colorPipeline)SetTestColor(1,1,1,1);else {SetTestColor(material.baseColor[0],material.baseColor[1],material.baseColor[2],material.baseColor[3]);m_BaseColor={1,1,1,1};}XMFLOAT3 camera;XMStoreFloat3(&camera,eye);m_Camera={camera.x,camera.y,camera.z,0};LightingSettings lights;if(f.lighting==1){lights.directional.color={1,1,1};lights.directional.direction={-.2f,-.5f,1};lights.ambientIntensity=.4f;}if(f.lighting==2)lights.enabled=false;m_Lighting=BuildLightingConstants(lights);
    m_MeshRange={};m_MaterialState={material.doubleSided,false,false};m_EnvironmentSettings.enabled=false;m_ShadowConstants={};m_PendingLights.clear();m_LightMode=LightRenderingMode::Forward;if(lights.enabled){GpuSceneLight light;light.directionType={m_Lighting.directionEnabled[0],m_Lighting.directionEnabled[1],m_Lighting.directionEnabled[2],0};light.colorIntensity=m_Lighting.colorIntensity;m_PendingLights.push_back(light);m_Lighting.colorIntensity[3]=0;}m_ActiveLightPass=UINT_MAX;m_FrameConstantsDirty=true;BindShader(f.shader);for(unsigned i=0;i<4;++i)BindTexture(i,f.textures[i]);if(material.surfaceMode==MaterialSurfaceMode::Opaque){DrawMesh(f.mesh);++m_MaterialPreviewMetrics.draws;}
    barrier=CD3DX12_RESOURCE_BARRIER::Transition(color,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);m_CmdList->ResourceBarrier(1,&barrier);m_CmdList->EndQuery(m_MaterialPreviewTime.Get(),D3D12_QUERY_TYPE_TIMESTAMP,m_FrameIndex*2+1);m_CmdList->ResolveQueryData(m_MaterialPreviewTime.Get(),D3D12_QUERY_TYPE_TIMESTAMP,m_FrameIndex*2,2,m_MaterialPreviewReadback.Get(),m_FrameIndex*16);m_MaterialPreviewTimed[m_FrameIndex]=true;
    return GetTexturePreviewId(m_MaterialPreviewColor);
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

    if(m_FrameRecording)m_RetiredResources[m_FrameIndex].emplace_back(it->second.texture.Get());
    else if(m_Device)WaitForGpu();

    if (m_BoundTextureHandle == handle)
        m_BoundTextureHandle = RenderTextureHandle::Invalid();

    if(m_FrameRecording)m_RetiredTextureDescriptors[m_FrameIndex].push_back(it->second.descriptorIndex);
    else m_FreeTextureDescriptorIndices.push_back(it->second.descriptorIndex);
    for(auto& map:m_BoundMaps)if(map==handle)map={};
    m_LastMaterialTable={};
    m_UploadedTextures.erase(it);
    Logger::Get().Info("DX12 DestroyTexture: handle=" + std::to_string(handle.value));
}

void Dx12RenderAdapter::BindTexture(std::uint32_t slot, RenderTextureHandle handle)
{
    if(slot>3)return;
    if(slot>0){m_BoundMaps[slot-1]=handle;return;}
    m_BoundTextureHandle=m_UploadedTextures.count(handle.value)?handle:RenderTextureHandle{};
}
// Descriptor tables are immutable for every recorded command, and are reset
// only after the owning frame slot fence. Persistent UI/shadow SRVs are separate.
bool Dx12RenderAdapter::BindMaterialTextures(bool srgb)
{
    const std::array<std::uint64_t,5> key{m_BoundTextureHandle.value,m_BoundMaps[0].value,m_BoundMaps[1].value,m_BoundMaps[2].value,std::uint64_t(srgb)};
    m_MaterialAvailableMaps=0;for(unsigned i=0;i<3;++i)if(m_UploadedTextures.count(m_BoundMaps[i].value))m_MaterialAvailableMaps|=1u<<i;
    if(m_TraceMaterialBindings&&m_LastMaterialTable.ptr&&key==m_LastMaterialTableKey)m_MaterialBindingTrace.push_back({m_FrameIndex,key,m_LastMaterialTable.ptr,m_MaterialAvailableMaps});
    if(m_LastMaterialTable.ptr && key==m_LastMaterialTableKey) {
        m_CmdList->SetGraphicsRootDescriptorTable(1,m_LastMaterialTable);return true;
    }
    auto found=m_MaterialTables.find(key);
    if(found==m_MaterialTables.end()) {
        if(m_MaterialTableCount>=MaterialTablesPerFrame)return false;
        const UINT first=MaxTextures+(m_FrameIndex*MaterialTablesPerFrame+m_MaterialTableCount)*4;
        auto cpu=m_SrvHeap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(first)*m_SrvDescriptorSize;
        auto gpu=m_SrvHeap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=UINT64(first)*m_SrvDescriptorSize;
        for(unsigned i=0;i<4;++i) {
            const auto texture=m_UploadedTextures.find(key[i]);
            D3D12_SHADER_RESOURCE_VIEW_DESC desc{};desc.Format=i==0&&srgb?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;desc.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;desc.Texture2D.MipLevels=1;
            m_Device->CreateShaderResourceView(texture==m_UploadedTextures.end()?nullptr:texture->second.texture.Get(),&desc,cpu);
            cpu.ptr+=m_SrvDescriptorSize;
        }
        ++m_MaterialTableCount;found=m_MaterialTables.emplace(key,gpu).first;
    }
    m_LastMaterialTableKey=key;m_LastMaterialTable=found->second;
    if(m_TraceMaterialBindings)m_MaterialBindingTrace.push_back({m_FrameIndex,key,found->second.ptr,m_MaterialAvailableMaps});
    m_MaterialAvailableMaps=0;for(unsigned i=0;i<3;++i)if(m_UploadedTextures.count(m_BoundMaps[i].value))m_MaterialAvailableMaps|=1u<<i;
    m_CmdList->SetGraphicsRootDescriptorTable(1,found->second);return true;
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
        uploadedShader.srgbBaseColor=shaderResource.fragmentSource.find("PBR_TEXTURE_PIPELINE_V1")!=std::string::npos;
        const auto vs = CompileHlsl(shaderResource.vertexSource, shaderResource.vertexPath.c_str(), "VSMain", "vs_5_1");
        const auto ps = CompileHlsl(shaderResource.fragmentSource, shaderResource.fragmentPath.c_str(), "PSMain", "ps_5_1");

        Microsoft::WRL::ComPtr<ID3DBlob> hdrPs;
        if(shaderResource.fragmentSource.find("HDR_SCENE_V1")!=std::string::npos)
            hdrPs=CompileHlsl(shaderResource.fragmentSource,shaderResource.fragmentPath.c_str(),"PSMainHdr","ps_5_1");
        else if(shaderResource.fragmentSource.find("struct VSOut")!=std::string::npos) {
            const std::string wrapper=shaderResource.fragmentSource+R"(
float4 PSMainHdr(VSOut i):SV_Target {float4 c=PSMain(i);float3 v=saturate(c.rgb);
return float4(float3(v.x<=.04045?v.x/12.92:pow((v.x+.055)/1.055,2.4),v.y<=.04045?v.y/12.92:pow((v.y+.055)/1.055,2.4),v.z<=.04045?v.z/12.92:pow((v.z+.055)/1.055,2.4)),1);})";
            try{hdrPs=CompileHlsl(wrapper,shaderResource.fragmentPath.c_str(),"PSMainHdr","ps_5_1");}catch(const std::exception&){ }
        }
        if(!hdrPs){
            // Legacy/custom pixel shaders without a convertible VSOut use a safe
            // HDR unlit material fallback; their original LDR PSO remains intact.
            Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
            ThrowIfFailed(D3DReflect(vs->GetBufferPointer(),vs->GetBufferSize(),IID_PPV_ARGS(&reflection)),"HDR fallback reflection failed");
            D3D12_SHADER_DESC d{};reflection->GetDesc(&d);std::string fields,uv,color;
            for(UINT i=0;i<d.OutputParameters;++i){D3D12_SIGNATURE_PARAMETER_DESC o{};reflection->GetOutputParameterDesc(i,&o);
                unsigned count=0;for(unsigned k=0;k<4;++k)count+=(o.Mask>>k)&1;
                if(!count)continue;const std::string name="field"+std::to_string(i);
                const std::string type=o.ComponentType==D3D_REGISTER_COMPONENT_UINT32?"uint":o.ComponentType==D3D_REGISTER_COMPONENT_SINT32?"int":"float";
                if(type!="float"||std::string(o.SemanticName)=="COLOR")fields+="nointerpolation ";
                fields+=type+(count>1?std::to_string(count):"")+" "+name+":"+o.SemanticName+std::to_string(o.SemanticIndex)+";";
                if(o.ComponentType==D3D_REGISTER_COMPONENT_FLOAT32&&o.SemanticIndex==0){if(std::string(o.SemanticName)=="TEXCOORD"&&count>=2)uv="input."+name+".xy";if(std::string(o.SemanticName)=="COLOR"&&count>=3)color="input."+name+".xyz";}
            }
            // Preserve the complete signature, including SV_Position and unused
            // fields: dropping fields changes hardware register assignments.
            std::string source="Texture2D<float4> tex:register(t0);SamplerState samp:register(s0);struct HdrLegacyInput{"+fields+"};float4 PSMainHdr(HdrLegacyInput input):SV_Target{float3 c=";
            source+=uv.empty()?"float3(1,1,1)":"tex.Sample(samp,"+uv+").rgb";source+="*";source+=color.empty()?"float3(1,1,1)":color;
            source+=";c=saturate(c);return float4(float3(c.r<=.04045?c.r/12.92:pow((c.r+.055)/1.055,2.4),c.g<=.04045?c.g/12.92:pow((c.g+.055)/1.055,2.4),c.b<=.04045?c.b/12.92:pow((c.b+.055)/1.055,2.4)),1);}";
            hdrPs=CompileHlsl(source,"HDR unlit fallback","PSMainHdr","ps_5_1");
        }
        const auto validateEnvironment=[](ID3DBlob* code){Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
            ThrowIfFailed(D3DReflect(code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&reflection)),"IBL shader reflection failed");D3D12_SHADER_DESC d{};reflection->GetDesc(&d);
            for(UINT i=0;i<d.BoundResources;++i){D3D12_SHADER_INPUT_BIND_DESC b{};reflection->GetResourceBindingDesc(i,&b);
                if(b.Type==D3D_SIT_CBUFFER&&b.BindPoint==6){D3D12_SHADER_BUFFER_DESC cb{};reflection->GetConstantBufferByName(b.Name)->GetDesc(&cb);if(b.Space||std::string(b.Name)!="EnvironmentCB"||cb.Size!=32)throw std::runtime_error("Incompatible EnvironmentCB");}
                if(b.Type==D3D_SIT_CBUFFER&&b.BindPoint==7){D3D12_SHADER_BUFFER_DESC cb{};reflection->GetConstantBufferByName(b.Name)->GetDesc(&cb);if(b.Space||std::string(b.Name)!="SceneLightsCB"||cb.Size!=32)throw std::runtime_error("Incompatible SceneLightsCB");}
                if(b.BindPoint>=9&&b.BindPoint<=11&&b.Type!=D3D_SIT_CBUFFER){if(b.Space||b.BindCount!=1||b.Type!=D3D_SIT_STRUCTURED||b.Dimension!=D3D_SRV_DIMENSION_BUFFER)throw std::runtime_error("Incompatible scene light SRV");}
                if(b.Type==D3D_SIT_TEXTURE&&b.BindPoint>=5&&b.BindPoint<=8){if(b.Space||b.BindCount!=1||b.Dimension!=(b.BindPoint==8?D3D_SRV_DIMENSION_TEXTURE2D:D3D_SRV_DIMENSION_TEXTURECUBE))throw std::runtime_error("Incompatible IBL SRV");}
            }};
        validateEnvironment(ps.Get());validateEnvironment(hdrPs.Get());
        CD3DX12_DESCRIPTOR_RANGE descriptorRange;
        descriptorRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

        D3D12_ROOT_PARAMETER rootParameters[14]{};
        rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameters[0].Descriptor.ShaderRegister = 0;
        rootParameters[0].Descriptor.RegisterSpace = 0;
        rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        D3D12_DESCRIPTOR_RANGE materialRanges[2]={descriptorRange,{}};
        materialRanges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,3,2,0,1};
        rootParameters[1].DescriptorTable.NumDescriptorRanges = 2;
        rootParameters[1].DescriptorTable.pDescriptorRanges = materialRanges;
        rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rootParameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameters[2].Descriptor={1,0};
        rootParameters[2].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        rootParameters[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParameters[3].Constants={2,0,12};
        rootParameters[3].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;

        CD3DX12_DESCRIPTOR_RANGE shadowRange; shadowRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,1);
        rootParameters[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameters[4].Descriptor={3,0};rootParameters[4].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        rootParameters[5].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[5].DescriptorTable={1,&shadowRange};rootParameters[5].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        rootParameters[6].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParameters[6].Constants={4,0,8};rootParameters[6].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        rootParameters[7].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameters[7].Descriptor={5,0};rootParameters[7].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        // 53 DWORD: 39 + three root SRVs + 8 scene-light constants.
        CD3DX12_DESCRIPTOR_RANGE environmentRange;environmentRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,4,5);
        rootParameters[8].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[8].DescriptorTable={1,&environmentRange};rootParameters[8].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        rootParameters[9].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParameters[9].Constants={6,0,8};rootParameters[9].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        for(UINT i=0;i<3;++i){rootParameters[10+i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;
            rootParameters[10+i].Descriptor={9+i,0};rootParameters[10+i].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
        rootParameters[13].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParameters[13].Constants={7,0,8};rootParameters[13].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        CD3DX12_STATIC_SAMPLER_DESC staticSampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
        staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

        D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
        rootSignatureDesc.NumParameters = _countof(rootParameters);
        rootSignatureDesc.pParameters = rootParameters;
        CD3DX12_STATIC_SAMPLER_DESC comparison(1,D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT,
            D3D12_TEXTURE_ADDRESS_MODE_BORDER,D3D12_TEXTURE_ADDRESS_MODE_BORDER,D3D12_TEXTURE_ADDRESS_MODE_BORDER,
            0,1,D3D12_COMPARISON_FUNC_LESS_EQUAL,D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);
        CD3DX12_STATIC_SAMPLER_DESC environmentSampler(2,D3D12_FILTER_MIN_MAG_MIP_LINEAR,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        D3D12_STATIC_SAMPLER_DESC samplers[]={staticSampler,comparison,environmentSampler};
        rootSignatureDesc.NumStaticSamplers = 3;
        rootSignatureDesc.pStaticSamplers = samplers;
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
            {"TANGENT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
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
        if(hdrPs){auto h=psoDesc;h.PS={hdrPs->GetBufferPointer(),hdrPs->GetBufferSize()};h.RTVFormats[0]=DXGI_FORMAT_R32G32B32A32_FLOAT;ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&h,IID_PPV_ARGS(&uploadedShader.hdrStates[0])),"HDR PSO failed");}

        psoDesc.RasterizerState.FrontCounterClockwise=TRUE;
        ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&psoDesc,IID_PPV_ARGS(&uploadedShader.mirrorPipelineState)),"DX12 mirror PSO failed");
        if(hdrPs){auto h=psoDesc;h.PS={hdrPs->GetBufferPointer(),hdrPs->GetBufferSize()};h.RTVFormats[0]=DXGI_FORMAT_R32G32B32A32_FLOAT;ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&h,IID_PPV_ARGS(&uploadedShader.hdrStates[1])),"HDR PSO failed");}
        psoDesc.RasterizerState.FrontCounterClockwise=FALSE;
        psoDesc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&psoDesc,IID_PPV_ARGS(&uploadedShader.doublePipelineState)),"DX12 double sided PSO failed");
        if(hdrPs){auto h=psoDesc;h.PS={hdrPs->GetBufferPointer(),hdrPs->GetBufferSize()};h.RTVFormats[0]=DXGI_FORMAT_R32G32B32A32_FLOAT;ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&h,IID_PPV_ARGS(&uploadedShader.hdrStates[2])),"HDR PSO failed");}
        psoDesc.RasterizerState.CullMode=D3D12_CULL_MODE_BACK;
        if(shaderResource.vertexSource.find("WHISP_INSTANCE_LAYOUT_V1")!=std::string::npos) {
            try {
                const auto instanceVs=CompileHlsl(shaderResource.vertexSource,shaderResource.vertexPath.c_str(),"VSInstancedMain","vs_5_1");
                const auto verifyNoDrawConstants=[](ID3DBlob* bytecode) {
                    Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
                    ThrowIfFailed(D3DReflect(bytecode->GetBufferPointer(),bytecode->GetBufferSize(),IID_PPV_ARGS(&reflection)),"Instance shader reflection failed");
                    D3D12_SHADER_DESC desc{};ThrowIfFailed(reflection->GetDesc(&desc),"Instance shader description failed");
                    for(UINT binding=0;binding<desc.BoundResources;++binding) {
                        D3D12_SHADER_INPUT_BIND_DESC input{};
                        ThrowIfFailed(reflection->GetResourceBindingDesc(binding,&input),"Instance binding reflection failed");
                        if(input.Type==D3D_SIT_CBUFFER) {
                            D3D12_SHADER_BUFFER_DESC buffer{};
                            const bool lighting=input.BindPoint==1 && std::string(input.Name)=="LightingCB";
                            const bool shadow=input.BindPoint==3 && std::string(input.Name)=="ShadowCB";
                            const bool camera=input.BindPoint==5 && std::string(input.Name)=="CameraCB";
                            const bool sceneLights=input.BindPoint==7 && std::string(input.Name)=="SceneLightsCB";
                            const bool environment=input.BindPoint==6 && std::string(input.Name)=="EnvironmentCB";
                            const bool material=input.BindPoint==4 && std::string(input.Name)=="MaterialCB";
                            if(input.Space!=0 || (!lighting&&!shadow&&!material&&!camera&&!environment&&!sceneLights) ||
                                FAILED(reflection->GetConstantBufferByName(input.Name)->GetDesc(&buffer)) ||
                                (material?(buffer.Size!=16&&buffer.Size!=32):buffer.Size!=((environment||sceneLights)?32:camera?16:lighting?sizeof(LightingConstants):sizeof(ShadowConstants))))
                                throw std::runtime_error("instance variant still reads per-draw or incompatible constant buffer");
                        }
                    }
                };
                verifyNoDrawConstants(instanceVs.Get());verifyNoDrawConstants(ps.Get());
                D3D12_INPUT_ELEMENT_DESC instanceLayout[] = {
                    {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
                    {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
                    {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
                    {"TANGENT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
                    {"INSTANCE_MODEL",0,DXGI_FORMAT_R32G32B32A32_FLOAT,1,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
                    {"INSTANCE_MODEL",1,DXGI_FORMAT_R32G32B32A32_FLOAT,1,16,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
                    {"INSTANCE_MODEL",2,DXGI_FORMAT_R32G32B32A32_FLOAT,1,32,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
                    {"INSTANCE_MODEL",3,DXGI_FORMAT_R32G32B32A32_FLOAT,1,48,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
                    {"INSTANCE_MVP",0,DXGI_FORMAT_R32G32B32A32_FLOAT,1,64,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
                    {"INSTANCE_MVP",1,DXGI_FORMAT_R32G32B32A32_FLOAT,1,80,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
                    {"INSTANCE_MVP",2,DXGI_FORMAT_R32G32B32A32_FLOAT,1,96,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
                    {"INSTANCE_MVP",3,DXGI_FORMAT_R32G32B32A32_FLOAT,1,112,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
                    {"INSTANCE_TINT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,1,128,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
                    {"INSTANCE_NORMAL",0,DXGI_FORMAT_R32G32B32A32_FLOAT,1,144,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
                    {"INSTANCE_NORMAL",1,DXGI_FORMAT_R32G32B32A32_FLOAT,1,160,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
                    {"INSTANCE_NORMAL",2,DXGI_FORMAT_R32G32B32A32_FLOAT,1,176,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
                    {"INSTANCE_SURFACE",0,DXGI_FORMAT_R32G32B32A32_FLOAT,1,192,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
                    {"INSTANCE_BASECOLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,1,208,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
                    {"INSTANCE_TEXTURE",0,DXGI_FORMAT_R32G32B32A32_FLOAT,1,224,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
                };
                psoDesc.InputLayout={instanceLayout,_countof(instanceLayout)};
                psoDesc.VS={instanceVs->GetBufferPointer(),instanceVs->GetBufferSize()};
                ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&psoDesc,IID_PPV_ARGS(&uploadedShader.instancedPipelineState)),"DX12 instanced PSO creation failed");
        if(hdrPs){auto h=psoDesc;h.PS={hdrPs->GetBufferPointer(),hdrPs->GetBufferSize()};h.RTVFormats[0]=DXGI_FORMAT_R32G32B32A32_FLOAT;ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&h,IID_PPV_ARGS(&uploadedShader.hdrStates[3])),"HDR PSO failed");}
                psoDesc.RasterizerState.FrontCounterClockwise=TRUE;
                ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&psoDesc,IID_PPV_ARGS(&uploadedShader.mirrorInstancedPipelineState)),"DX12 mirror instanced PSO failed");
        if(hdrPs){auto h=psoDesc;h.PS={hdrPs->GetBufferPointer(),hdrPs->GetBufferSize()};h.RTVFormats[0]=DXGI_FORMAT_R32G32B32A32_FLOAT;ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&h,IID_PPV_ARGS(&uploadedShader.hdrStates[4])),"HDR PSO failed");}
                psoDesc.RasterizerState.FrontCounterClockwise=FALSE;
                psoDesc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
                ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&psoDesc,IID_PPV_ARGS(&uploadedShader.doubleInstancedPipelineState)),"DX12 double sided instanced PSO failed");
        if(hdrPs){auto h=psoDesc;h.PS={hdrPs->GetBufferPointer(),hdrPs->GetBufferSize()};h.RTVFormats[0]=DXGI_FORMAT_R32G32B32A32_FLOAT;ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&h,IID_PPV_ARGS(&uploadedShader.hdrStates[5])),"HDR PSO failed");}
            } catch(const std::exception& error) {
                uploadedShader.instancedPipelineState.Reset();uploadedShader.mirrorInstancedPipelineState.Reset();uploadedShader.doubleInstancedPipelineState.Reset();
                Logger::Get().Warn(std::string("DX12 instance variant unavailable; normal fallback: ")+error.what());
            }
        }
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

    if(m_FrameRecording) {
        m_RetiredResources[m_FrameIndex].emplace_back(it->second.rootSignature.Get());
        for(auto& p:it->second.hdrStates)if(p)m_RetiredResources[m_FrameIndex].emplace_back(p.Get());
        m_RetiredResources[m_FrameIndex].emplace_back(it->second.pipelineState.Get());
        if(it->second.instancedPipelineState)m_RetiredResources[m_FrameIndex].emplace_back(it->second.instancedPipelineState.Get());
        if(it->second.mirrorPipelineState)m_RetiredResources[m_FrameIndex].emplace_back(it->second.mirrorPipelineState.Get());
        if(it->second.mirrorInstancedPipelineState)m_RetiredResources[m_FrameIndex].emplace_back(it->second.mirrorInstancedPipelineState.Get());
        if(it->second.doublePipelineState)m_RetiredResources[m_FrameIndex].emplace_back(it->second.doublePipelineState.Get());
        if(it->second.doubleInstancedPipelineState)m_RetiredResources[m_FrameIndex].emplace_back(it->second.doubleInstancedPipelineState.Get());
    } else if(m_Device)WaitForGpu();

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

    if(!MaterialIndexCount(it->second.indexCount))return;
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

    if(m_DrawCbIndex>=MaxDrawsPerFrame) { Logger::Get().Warn("DX12 ordinary draw capacity exceeded; command skipped safely"); return; }
    struct alignas(256) CB { float mvp[16]; float color[4]; float model[16]; float baseColor[4]; };
    CB cb{};
    memcpy(cb.mvp, m_PendingMVP, sizeof(cb.mvp));
    memcpy(cb.color, m_PendingColor, sizeof(cb.color));
    memcpy(cb.model,m_Model.data(),sizeof(cb.model));
    memcpy(cb.baseColor,m_BaseColor.data(),sizeof(cb.baseColor));

    const UINT cbSlot = (m_DrawCbIndex < MaxDrawsPerFrame) ? m_DrawCbIndex : (MaxDrawsPerFrame - 1);
    memcpy(m_CbMapped[m_FrameIndex][cbSlot], &cb, sizeof(CB));

    if(m_ActiveLightPass==UINT_MAX)PrepareSceneLights();
    ID3D12DescriptorHeap* descriptorHeaps[] = { m_SrvHeap.Get() };
    m_CmdList->SetDescriptorHeaps(1, descriptorHeaps);
    m_CmdList->SetGraphicsRootSignature(shaderIt->second.rootSignature.Get());
    m_CmdList->SetPipelineState(m_HdrActive&&shaderIt->second.hdrStates[0]?shaderIt->second.hdrStates[m_MaterialState.doubleSided?2:m_MaterialState.reverseWinding?1:0].Get():(m_MaterialState.doubleSided?shaderIt->second.doublePipelineState.Get():(m_MaterialState.reverseWinding?shaderIt->second.mirrorPipelineState.Get():shaderIt->second.pipelineState.Get())));
    m_CmdList->SetGraphicsRootConstantBufferView(0, m_CbGpu[m_FrameIndex][cbSlot]);
    if(!BindMaterialTextures(shaderIt->second.srgbBaseColor))return;
    BindSceneLights();
    BindFrameConstants();
    if(!BindEnvironment())return;
    m_CmdList->SetGraphicsRoot32BitConstants(3,12,m_Normal.data(),0);
    const float material[8]={m_MaterialState.receiveShadows?1.f:0.f,m_Surface[0],m_Surface[1],m_Surface[2],m_TextureParameters[0],m_TextureParameters[1],m_TextureParameters[2],float(m_MaterialAvailableMaps)};
    m_CmdList->SetGraphicsRoot32BitConstants(6,8,material,0);
    BindShadows();
    m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_CmdList->IASetVertexBuffers(0, 1, &it->second.vertexView);
    m_CmdList->IASetIndexBuffer(&it->second.indexView);
    m_CmdList->DrawIndexedInstanced(MaterialIndexCount(it->second.indexCount), 1, m_MeshRange.firstIndex, 0, 0);
    ++m_SubmissionStatistics.drawCalls; ++m_SubmissionStatistics.renderedInstances;
    m_SubmissionStatistics.uploadBytes+=sizeof(CB);

    if (m_DrawCbIndex < MaxDrawsPerFrame)
        ++m_DrawCbIndex;
}

bool Dx12RenderAdapter::SupportsInstancing(RenderShaderHandle handle) const
{
    const auto it=m_UploadedShaders.find(handle.value);
    return it!=m_UploadedShaders.end() && it->second.instancedPipelineState!=nullptr;
}
RenderSubmissionStatistics Dx12RenderAdapter::GetSubmissionStatistics() const
{
    auto result=m_SubmissionStatistics;
    result.materialDescriptors=m_MaterialTableCount*4;
    for(const auto& [id,texture]:m_UploadedTextures)result.textureMemoryBytes+=texture.allocationBytes;
    for(const auto& pages:m_InstancePages)for(const auto& page:pages)result.instanceBufferCapacity+=page.capacity;
    return result;
}
// Each snapshot is immutable until this frame slot's submitted fence completes.
// Separate 256-byte CBV slices retain b1/b3/b5 shader compatibility.
void Dx12RenderAdapter::BindFrameConstants()
{
    if(m_FrameConstantsDirty || !m_FrameConstantAddress) {
    std::array<std::uint8_t,768> bytes{};
    static_assert(sizeof(LightingConstants)<=256 && sizeof(ShadowConstants)<=256);
    std::memcpy(bytes.data(),&m_Lighting,sizeof(m_Lighting));
    std::memcpy(bytes.data()+256,&m_ShadowConstants,sizeof(m_ShadowConstants));
    std::memcpy(bytes.data()+512,m_Camera.data(),sizeof(m_Camera));
    if(!m_FrameConstantAddress || bytes!=m_LastFrameConstants) {
        auto& pages=m_FrameConstantPages[m_FrameIndex];
        if(m_FrameConstantPageIndex<pages.size() && pages[m_FrameConstantPageIndex].used+bytes.size()>pages[m_FrameConstantPageIndex].capacity)++m_FrameConstantPageIndex;
        if(m_FrameConstantPageIndex==pages.size())pages.emplace_back();
        auto& page=pages[m_FrameConstantPageIndex];
        if(!page.resource) {
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
            const auto desc=CD3DX12_RESOURCE_DESC::Buffer(64*1024);
            ThrowIfFailed(m_Device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&page.resource)),"Frame constant page allocation failed");
            void* mapped=nullptr;const CD3DX12_RANGE read(0,0);
            ThrowIfFailed(page.resource->Map(0,&read,&mapped),"Frame constant map failed");
            page.mapped=static_cast<std::uint8_t*>(mapped);page.capacity=64*1024;
        }
        std::memcpy(page.mapped+page.used,bytes.data(),bytes.size());
        m_FrameConstantAddress=page.resource->GetGPUVirtualAddress()+page.used;
        page.used+=bytes.size();m_LastFrameConstants=bytes;
        m_SubmissionStatistics.frameConstantUploadBytes+=bytes.size();
    }
    m_FrameConstantsDirty=false;
    }
    m_CmdList->SetGraphicsRootConstantBufferView(2,m_FrameConstantAddress);
    m_CmdList->SetGraphicsRootConstantBufferView(4,m_FrameConstantAddress+256);
    m_CmdList->SetGraphicsRootConstantBufferView(7,m_FrameConstantAddress+512);
}
bool Dx12RenderAdapter::UploadInstances(std::span<const RenderInstanceData> instances,D3D12_VERTEX_BUFFER_VIEW& instanceView)
{
    if(instances.empty()||instances.size_bytes()>UINT_MAX)return false;
    if(m_PreparedScene)for(const auto& cached:m_CachedInstances) {
        const auto address=reinterpret_cast<std::uintptr_t>(instances.data());
        const auto begin=reinterpret_cast<std::uintptr_t>(cached.data);
        if(address>=begin && address+instances.size_bytes()<=begin+cached.count*sizeof(RenderInstanceData)) {
            instanceView=cached.view;instanceView.BufferLocation+=address-begin;
            instanceView.SizeInBytes=static_cast<UINT>(instances.size_bytes());return true;
        }
    }
    {
        ZoneScopedN("InstanceUpload");
        auto& pages=m_InstancePages[m_FrameIndex];
        if(m_InstancePageIndex<pages.size() && pages[m_InstancePageIndex].used>0 &&
            pages[m_InstancePageIndex].capacity-pages[m_InstancePageIndex].used<instances.size())++m_InstancePageIndex;
        if(m_InstancePageIndex==pages.size())pages.emplace_back();
        auto& page=pages[m_InstancePageIndex];
        if(page.capacity<instances.size()) {
            // This page has not been referenced by any command in this frame.
            // Previously submitted uses were waited for in BeginFrame.
            if(page.used!=0)return false;
            std::size_t capacity=512;
            while(capacity<instances.size())capacity*=2;
            if(capacity>UINT_MAX/sizeof(RenderInstanceData))return false;
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
            const auto desc=CD3DX12_RESOURCE_DESC::Buffer(capacity*sizeof(RenderInstanceData));
            if(FAILED(m_Device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&resource))))return false;
            void* mapped=nullptr;const CD3DX12_RANGE read(0,0);
            if(FAILED(resource->Map(0,&read,&mapped)))return false;
            if(page.resource&&page.mapped)page.resource->Unmap(0,nullptr);
            page.resource=std::move(resource);page.mapped=static_cast<std::uint8_t*>(mapped);page.capacity=capacity;
            ++m_SubmissionStatistics.instanceBufferGrowths;
        }
        const auto offset=page.used*sizeof(RenderInstanceData);
        std::memcpy(page.mapped+offset,instances.data(),instances.size_bytes());
        instanceView.BufferLocation=page.resource->GetGPUVirtualAddress()+offset;
        instanceView.SizeInBytes=static_cast<UINT>(instances.size_bytes());
        instanceView.StrideInBytes=sizeof(RenderInstanceData);
        page.used+=instances.size();
        m_SubmissionStatistics.uploadBytes+=instances.size_bytes();
    }

    if(m_PreparedScene)m_CachedInstances.push_back({instances.data(),instances.size(),instanceView});
    return true;
}

bool Dx12RenderAdapter::DrawMeshInstanced(RenderMeshHandle handle,std::span<const RenderInstanceData> instances)
{
    ZoneScopedN("InstancedDraw");
    const auto mesh=m_UploadedMeshes.find(handle.value);
    const auto shader=m_UploadedShaders.find(m_BoundShaderHandle.value);
    const auto texture=m_UploadedTextures.find(m_BoundTextureHandle.value);
    if(instances.empty() || instances.size_bytes()>UINT_MAX || mesh==m_UploadedMeshes.end() ||
        shader==m_UploadedShaders.end() || !shader->second.instancedPipelineState || texture==m_UploadedTextures.end())return false;
    if(!MaterialIndexCount(mesh->second.indexCount))return false;
    D3D12_VERTEX_BUFFER_VIEW instanceView{};
    if(!UploadInstances(instances,instanceView))return false;
    if(m_ActiveLightPass==UINT_MAX)PrepareSceneLights();
    ID3D12DescriptorHeap* heaps[]={m_SrvHeap.Get()};m_CmdList->SetDescriptorHeaps(1,heaps);
    m_CmdList->SetGraphicsRootSignature(shader->second.rootSignature.Get());
    m_CmdList->SetPipelineState(m_HdrActive&&shader->second.hdrStates[3]?shader->second.hdrStates[m_MaterialState.doubleSided?5:m_MaterialState.reverseWinding?4:3].Get():(m_MaterialState.doubleSided?shader->second.doubleInstancedPipelineState.Get():(m_MaterialState.reverseWinding?shader->second.mirrorInstancedPipelineState.Get():shader->second.instancedPipelineState.Get())));
    // The instanced contract does not read CB0; bind a valid address nevertheless.
    // No CB memory is overwritten here, including earlier normal draws.
    m_CmdList->SetGraphicsRootConstantBufferView(0,m_CbGpu[m_FrameIndex][0]);
    if(!BindMaterialTextures(shader->second.srgbBaseColor))return false;
    BindSceneLights();
    BindFrameConstants();
    if(!BindEnvironment())return false;
    const float material[8]={m_MaterialState.receiveShadows?1.f:0.f,m_Surface[0],m_Surface[1],m_Surface[2],m_TextureParameters[0],m_TextureParameters[1],m_TextureParameters[2],float(m_MaterialAvailableMaps)};
    m_CmdList->SetGraphicsRoot32BitConstants(6,8,material,0);
    BindShadows();
    m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_VERTEX_BUFFER_VIEW views[]={mesh->second.vertexView,instanceView};
    m_CmdList->IASetVertexBuffers(0,2,views);m_CmdList->IASetIndexBuffer(&mesh->second.indexView);
    m_CmdList->DrawIndexedInstanced(MaterialIndexCount(mesh->second.indexCount),static_cast<UINT>(instances.size()),m_MeshRange.firstIndex,0,0);
    ++m_SubmissionStatistics.drawCalls;++m_SubmissionStatistics.instancedDrawCalls;
    m_SubmissionStatistics.renderedInstances+=instances.size();
    return true;
}
std::vector<std::uint8_t> Dx12RenderAdapter::ReadBackFramePixels()
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 rowBytes=0,totalBytes=0;
    const auto desc=m_Rt[m_FrameIndex]->GetDesc();
    m_Device->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowBytes,&totalBytes);
    Microsoft::WRL::ComPtr<ID3D12Resource> staging;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    const auto bufferDesc=CD3DX12_RESOURCE_DESC::Buffer(totalBytes);
    ThrowIfFailed(m_Device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bufferDesc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&staging)),"Readback allocation failed");
    ExecuteImmediateCommandList(m_Device.Get(),m_Queue.Get(),m_Fence.Get(),m_FenceEvent,m_FenceValue,[&](ID3D12GraphicsCommandList* command) {
        auto before=CD3DX12_RESOURCE_BARRIER::Transition(m_Rt[m_FrameIndex].Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_SOURCE);command->ResourceBarrier(1,&before);
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=m_Rt[m_FrameIndex].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=staging.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
        command->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        auto after=CD3DX12_RESOURCE_BARRIER::Transition(m_Rt[m_FrameIndex].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PRESENT);command->ResourceBarrier(1,&after);
    });
    void* mapped=nullptr;D3D12_RANGE range{0,static_cast<SIZE_T>(totalBytes)};
    ThrowIfFailed(staging->Map(0,&range,&mapped),"Readback Map failed");
    std::vector<std::uint8_t> pixels(std::size_t(rows)*std::size_t(rowBytes));
    for(UINT row=0;row<rows;++row)std::memcpy(pixels.data()+row*rowBytes,static_cast<const std::uint8_t*>(mapped)+footprint.Offset+row*footprint.Footprint.RowPitch,static_cast<std::size_t>(rowBytes));
    const D3D12_RANGE noWrites{0,0};staging->Unmap(0,&noWrites);
    return pixels;
}

std::vector<std::uint8_t> Dx12RenderAdapter::ReadBackViewportPixels()
{
    return ReadBackColorPixels(m_ViewportTarget.color.Get());
}
std::vector<std::uint8_t> Dx12RenderAdapter::ReadBackTexturePixels(RenderTextureHandle handle)
{
    auto it=m_UploadedTextures.find(handle.value);
    return it==m_UploadedTextures.end()?std::vector<std::uint8_t>{}:ReadBackColorPixels(it->second.texture.Get());
}
std::vector<std::uint8_t> Dx12RenderAdapter::ReadBackColorPixels(ID3D12Resource* color)
{
    if(!color||m_FrameRecording)return {};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 rowBytes=0,totalBytes=0;
    const auto desc=color->GetDesc();
    m_Device->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowBytes,&totalBytes);
    Microsoft::WRL::ComPtr<ID3D12Resource> staging;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    const auto bufferDesc=CD3DX12_RESOURCE_DESC::Buffer(totalBytes);
    ThrowIfFailed(m_Device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bufferDesc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&staging)),"Readback allocation failed");
    ExecuteImmediateCommandList(m_Device.Get(),m_Queue.Get(),m_Fence.Get(),m_FenceEvent,m_FenceValue,[&](ID3D12GraphicsCommandList* command) {
        auto before=CD3DX12_RESOURCE_BARRIER::Transition(color,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);command->ResourceBarrier(1,&before);
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=color;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=staging.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
        command->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        auto after=CD3DX12_RESOURCE_BARRIER::Transition(color,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);command->ResourceBarrier(1,&after);
    });
    void* mapped=nullptr;D3D12_RANGE range{0,static_cast<SIZE_T>(totalBytes)};
    ThrowIfFailed(staging->Map(0,&range,&mapped),"Readback Map failed");
    std::vector<std::uint8_t> pixels(std::size_t(rows)*std::size_t(rowBytes));
    for(UINT row=0;row<rows;++row)std::memcpy(pixels.data()+row*rowBytes,static_cast<const std::uint8_t*>(mapped)+footprint.Offset+row*footprint.Footprint.RowPitch,static_cast<std::size_t>(rowBytes));
    const D3D12_RANGE noWrites{0,0};staging->Unmap(0,&noWrites);
    return pixels;
}

bool Dx12RenderAdapter::Initialize(IWindow* window)
{
    try
    {
        auto* glfwWin = dynamic_cast<GlfwWindow*>(window);
        if(!glfwWin||!glfwWin->GetGlfwHandle())return false;
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

    // One atlas, initialized once. Segoe is the common UI face; code/logs opt
    // into a separate monospace face. Both carry Latin and Cyrillic glyphs.
    static const ImWchar ranges[]={0x0020,0x00ff,0x0400,0x052f,0x25a0,0x25cf,0};
    wchar_t windowsDirectory[MAX_PATH]{};ImFont* uiFont=nullptr;
    if(GetWindowsDirectoryW(windowsDirectory,MAX_PATH)){
        auto fonts=std::filesystem::path(windowsDirectory)/"Fonts";
        for(const auto* name:{L"segoeui.ttf",L"arial.ttf"})if(!uiFont&&std::filesystem::is_regular_file(fonts/name)){
            ImFontConfig config;strcpy_s(config.Name,"Browser UI");uiFont=io.Fonts->AddFontFromFileTTF(AssetPaths::ToUtf8String(fonts/name).c_str(),16,&config,ranges);
        }
        if(std::filesystem::is_regular_file(fonts/L"consola.ttf")){ImFontConfig config;strcpy_s(config.Name,"Editor Mono");io.Fonts->AddFontFromFileTTF(AssetPaths::ToUtf8String(fonts/L"consola.ttf").c_str(),16,&config,ranges);}
    }
    if(!uiFont){ImFontConfig config;config.SizePixels=16;uiFont=io.Fonts->AddFontDefault(&config);}
    io.FontDefault=uiFont;
    // Reserve the supported DPI sizes before the backend creates its atlas SRV.
    io.Fonts->TexMinWidth=1024;io.Fonts->TexMinHeight=1024;
    for(auto* font:io.Fonts->Fonts)for(float size:{16.f,24.f,36.f}){auto* baked=font->GetFontBaked(size);for(ImWchar c=32;c<256;++c)baked->FindGlyph(c);for(ImWchar c=0x400;c<=0x52f;++c)baked->FindGlyph(c);if(font==uiFont)for(ImWchar c=0x25a0;c<=0x25cf;++c)baked->FindGlyph(c);}

    float dpiX=1,dpiY=1;glfwGetWindowContentScale(glfwWin->GetGlfwHandle(),&dpiX,&dpiY);
    m_EditorUiWindow=glfwWin->GetGlfwHandle();m_EditorUiScale=(std::max)(dpiX,dpiY);editor::ApplyEditorTheme(m_EditorUiScale);

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

    float dpiX=1,dpiY=1;glfwGetWindowContentScale(m_EditorUiWindow,&dpiX,&dpiY);const float dpi=(std::max)(dpiX,dpiY);
    if(std::abs(dpi-m_EditorUiScale)>.01f){m_EditorUiScale=dpi;editor::ApplyEditorTheme(dpi);}
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
    depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
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

bool Dx12RenderAdapter::BeginCameraPreview(int width,int height,const float* clear){
    if(!m_FrameRecording||m_ViewportTarget.rendering||m_HdrActive||m_CameraPreviewRendering)return false;
    std::swap(m_ViewportTarget,m_CameraPreviewTarget);
    try{
        if(BeginViewportRender(width,height,clear)){m_CameraPreviewRendering=true;return true;}
    }catch(...){std::swap(m_ViewportTarget,m_CameraPreviewTarget);throw;}
    std::swap(m_ViewportTarget,m_CameraPreviewTarget);return false;
}
void Dx12RenderAdapter::EndCameraPreview(){
    if(!m_CameraPreviewRendering)return;
    EndViewportRender();std::swap(m_ViewportTarget,m_CameraPreviewTarget);m_CameraPreviewRendering=false;
}
void Dx12RenderAdapter::ReleaseCameraPreview(){
    if(m_CameraPreviewRendering)EndCameraPreview();
    auto& t=m_CameraPreviewTarget;
    if(!t.color&&t.srvDescriptorIndex==UINT_MAX)return;
    if(m_FrameRecording){
        // ImGui may already have recorded this SRV earlier in the current UI
        // frame. Retain descriptors AND resources through this frame's fence.
        for(auto* resource:{static_cast<IUnknown*>(t.color.Get()),static_cast<IUnknown*>(t.depth.Get()),static_cast<IUnknown*>(t.rtvHeap.Get()),static_cast<IUnknown*>(t.dsvHeap.Get())})
            if(resource)m_RetiredResources[m_FrameIndex].emplace_back(resource);
        if(t.srvDescriptorIndex!=UINT_MAX)m_RetiredTextureDescriptors[m_FrameIndex].push_back(t.srvDescriptorIndex);
        t={};
    }else{
        WaitForGpu();std::swap(m_ViewportTarget,m_CameraPreviewTarget);
        ReleaseViewportResources();std::swap(m_ViewportTarget,m_CameraPreviewTarget);
    }
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
    std::memcpy(m_SceneClear,chosenClear,sizeof(m_SceneClear));
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
    if(GetEnvironmentVariableA("WHISP_DX12_DEBUG",nullptr,0)) {
        Microsoft::WRL::ComPtr<ID3D12Debug> debug;
        if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            if(GetEnvironmentVariableA("WHISP_DX12_GBV",nullptr,0)) {
                Microsoft::WRL::ComPtr<ID3D12Debug1> validation;
                if(SUCCEEDED(debug.As(&validation)))validation->SetEnableGPUBasedValidation(TRUE);
                else Logger::Get().Warn("DX12 GPU based validation unavailable");
            }
        }
        else Logger::Get().Warn("DX12 debug layer unavailable");
    }
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;

    for (UINT i = 0; m_Factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;

        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_Device)))) {
            char name[256]{};WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,name,sizeof(name),nullptr,nullptr);
            Logger::Get().Info(std::string("DX12 GPU: ")+name);break;
        }
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
    depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
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
    srvHeapDesc.NumDescriptors = MaxTextures+FrameCount*MaterialTablesPerFrame*4+ExtraImGuiDescriptors;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(
        m_Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_SrvHeap)),
        "CreateDescriptorHeap SRV failed");
    m_SrvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_FreeTextureDescriptorIndices.clear();
    m_FreeTextureDescriptorIndices.reserve(MaxTextures);
    m_FreeImGuiDescriptorIndices.clear();
    const UINT extraImGuiBase=MaxTextures+FrameCount*MaterialTablesPerFrame*4;
    for(UINT i=ExtraImGuiDescriptors;i>0;--i)m_FreeImGuiDescriptorIndices.push_back(extraImGuiBase+i-1);
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
    ZoneScopedN("DX12BeginFrame");
    m_CpuWaitMs={};ResizeBackBufferIfNeeded();
    const auto slotWaitStart=std::chrono::steady_clock::now();
    // Never reset/write a slot until its submitted GPU work has completed.
    if(m_Fence->GetCompletedValue()<m_InstanceFrameFence[m_FrameIndex]) {
        ThrowIfFailed(m_Fence->SetEventOnCompletion(m_InstanceFrameFence[m_FrameIndex],m_FenceEvent),"Instance slot fence failed");
        WaitForSingleObject(m_FenceEvent,INFINITE);
    }
    m_CpuWaitMs[0]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-slotWaitStart).count();
    m_RetiredResources[m_FrameIndex].clear();
    for(auto descriptor:m_RetiredTextureDescriptors[m_FrameIndex])m_FreeTextureDescriptorIndices.push_back(descriptor);
    m_RetiredTextureDescriptors[m_FrameIndex].clear();
    m_SubmissionStatistics={}; m_InstancePageIndex=0;
    m_MaterialBindingTrace.clear();m_MaterialTables.clear();m_MaterialTableCount=0;m_EnvironmentTable={};m_LastMaterialTable={};m_FrameConstantsDirty=true;
    for(auto& page:m_InstancePages[m_FrameIndex])page.used=0;
    CollectFogTiming(m_FrameIndex);m_FogPassCursor=0;++m_PostFxFrame;
    m_FrameConstantPageIndex=0;m_FrameConstantAddress=0;
    for(auto& page:m_FrameConstantPages[m_FrameIndex])page.used=0;

    ThrowIfFailed(m_Allocator[m_FrameIndex]->Reset(), "Allocator Reset failed");
    ThrowIfFailed(m_CmdList->Reset(m_Allocator[m_FrameIndex].Get(), m_Pso.Get()), "CmdList Reset failed");
    m_DrawCbIndex = 0;
    m_FrameRecording=true;
    CollectGpuTiming(m_FrameIndex);
    CollectSceneLights(m_FrameIndex);m_LightPassCursor=0;m_ActiveLightPass=UINT_MAX;
    m_TimePassCount=0;m_TimeCurrentPass=UINT_MAX;m_TimeOverflow=false;m_TimeExtras.fill(0);
    if(!m_TimeHeap)InitializeGpuTiming();
    if(m_TimeHeap)m_CmdList->EndQuery(m_TimeHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,m_FrameIndex*TimingQueries);
    m_CachedInstances.clear();m_PreparedScene=false;m_ShadowConstants={};

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
    std::memcpy(m_SceneClear,m_ClearColor,sizeof(m_SceneClear));
    m_CmdList->ClearRenderTargetView(rtv, m_ClearColor, 0, nullptr);
    m_CmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void Dx12RenderAdapter::DrawTestTriangle()
{
    if(m_DrawCbIndex>=MaxDrawsPerFrame) { Logger::Get().Warn("DX12 ordinary draw capacity exceeded; command skipped safely"); return; }
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
    ++m_SubmissionStatistics.drawCalls; m_SubmissionStatistics.uploadBytes+=sizeof(CB);

    if (m_DrawCbIndex < MaxDrawsPerFrame)
        ++m_DrawCbIndex;
}

void Dx12RenderAdapter::DrawTestLine()
{
    if(m_DrawCbIndex>=MaxDrawsPerFrame) { Logger::Get().Warn("DX12 ordinary draw capacity exceeded; command skipped safely"); return; }
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
    ++m_SubmissionStatistics.drawCalls; m_SubmissionStatistics.uploadBytes+=sizeof(CB);

    if (m_DrawCbIndex < MaxDrawsPerFrame)
        ++m_DrawCbIndex;
}

void Dx12RenderAdapter::DrawTestQuad()
{
    if(m_DrawCbIndex>=MaxDrawsPerFrame) { Logger::Get().Warn("DX12 ordinary draw capacity exceeded; command skipped safely"); return; }
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
    ++m_SubmissionStatistics.drawCalls; m_SubmissionStatistics.uploadBytes+=sizeof(CB);

    if (m_DrawCbIndex < MaxDrawsPerFrame)
        ++m_DrawCbIndex;
}

void Dx12RenderAdapter::DrawTestCube()
{
    if(m_DrawCbIndex>=MaxDrawsPerFrame) { Logger::Get().Warn("DX12 ordinary draw capacity exceeded; command skipped safely"); return; }
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
    ++m_SubmissionStatistics.drawCalls; m_SubmissionStatistics.uploadBytes+=sizeof(CB);

    if (m_DrawCbIndex < MaxDrawsPerFrame)
        ++m_DrawCbIndex;
}

void Dx12RenderAdapter::EndFrame()
{
    if(m_TimeHeap) {
        m_CmdList->EndQuery(m_TimeHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,m_FrameIndex*TimingQueries+1);
        // Resolve only initialized queries, including optional UI/custom stages.
        m_TimeSlotExtras[m_FrameIndex]=m_TimeExtras;
        for(UINT pass=0;pass<m_TimePassCount;++pass)for(UINT phase=6;phase<TimingStages;++phase)
            if(!(m_TimeExtras[pass]&(1u<<(phase-6))))m_CmdList->EndQuery(m_TimeHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,m_FrameIndex*TimingQueries+2+TimingStages*pass+phase);
        m_TimePending[m_FrameIndex]=2+TimingStages*m_TimePassCount;
        m_TimeComplete[m_FrameIndex]=!m_TimeOverflow;
        m_CmdList->ResolveQueryData(m_TimeHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,m_FrameIndex*TimingQueries,
            m_TimePending[m_FrameIndex],m_TimeReadback[m_FrameIndex].Get(),0);
    }
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_Rt[m_FrameIndex].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT
    );
    m_CmdList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(m_CmdList->Close(), "CmdList Close failed");
    ID3D12CommandList* lists[] = { m_CmdList.Get() };
    m_Queue->ExecuteCommandLists(1, lists);
    m_FrameRecording=false;
}

void Dx12RenderAdapter::Present()
{
    ZoneScopedN("DX12Present");
    const auto presentStart=std::chrono::steady_clock::now();
    ThrowIfFailed(m_Swapchain->Present(0, 0), "Present failed");
    m_CpuWaitMs[1]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-presentStart).count();
    const UINT submitted=m_FrameIndex;
    MoveToNextFrame();
    CollectGpuTiming(submitted);
}

void Dx12RenderAdapter::MoveToNextFrame()
{
    const UINT64 fenceToWait = m_FenceValue;
    ThrowIfFailed(m_Queue->Signal(m_Fence.Get(), fenceToWait), "Queue Signal failed");
    m_FenceValue++;

    m_InstanceFrameFence[m_FrameIndex]=fenceToWait;
    m_FrameIndex = m_Swapchain->GetCurrentBackBufferIndex();

    const auto frameWaitStart=std::chrono::steady_clock::now();
    if (m_Fence->GetCompletedValue() < fenceToWait)
    {
        ThrowIfFailed(m_Fence->SetEventOnCompletion(fenceToWait, m_FenceEvent), "Fence SetEventOnCompletion failed");
        ZoneScopedN("DX12FrameFenceWait");
        WaitForSingleObject(m_FenceEvent, INFINITE);
    }
    m_CpuWaitMs[2]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frameWaitStart).count();
}

void Dx12RenderAdapter::WaitForGpu()
{
    ZoneScopedN("DX12WaitForGpu");
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
    (void)gpuHandle;
    auto* self = static_cast<Dx12RenderAdapter*>(info->UserData);
    if(self&&self->m_SrvHeap){
        const auto start=self->m_SrvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
        if(cpuHandle.ptr<start)return;
        const auto index=UINT((cpuHandle.ptr-start)/self->m_SrvDescriptorSize);
        const UINT extraBase=MaxTextures+FrameCount*MaterialTablesPerFrame*4;
        if(index==0||(index>=extraBase&&index<extraBase+ExtraImGuiDescriptors))
            self->m_FreeImGuiDescriptorIndices.push_back(index);
    }
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

    if(m_Device&&GetValidationErrorCount())Logger::Get().Error("DX12 validation failed at shutdown");
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

    m_FrameRecording=false;
    ReleaseSceneLights();
    m_RuntimeUIPso.Reset();m_RuntimeUIRoot.Reset();for(auto& buffer:m_RuntimeUIVertices)buffer.Reset();std::fill(std::begin(m_RuntimeUICapacity),std::end(m_RuntimeUICapacity),0);
    ReleaseCustomPostFx();ReleaseFog();ReleaseEditorVisuals();ReleaseEnvironment();m_CachedBrdfGpu.Reset();ReleaseHdrTargets();m_OutputPso.Reset();m_OutputRoot.Reset();
    ReleaseShadowResources();m_ShadowVs.Reset();m_ShadowInstanceVs.Reset();m_ShadowPso.Reset();m_ShadowInstancePso.Reset();m_ShadowDoublePso.Reset();m_ShadowDoubleInstancePso.Reset();m_ShadowMirrorPso.Reset();m_ShadowMirrorInstancePso.Reset();m_ShadowRoot.Reset();
    m_TimeHeap.Reset();for(auto& resource:m_TimeReadback)resource.Reset();m_TimeFrequency=0;
    std::fill(std::begin(m_TimePending),std::end(m_TimePending),0);
    m_GpuTimings={};m_CachedInstances.clear();m_PreparedScene=false;m_ShadowActive=false;
    m_NullShadowDescriptor=UINT_MAX;m_NullShadowSrv={};
    for(auto& retired:m_RetiredResources)retired.clear();
    for(auto& descriptors:m_RetiredTextureDescriptors)descriptors.clear();
    for(auto& pages:m_InstancePages) {
        for(auto& page:pages)if(page.resource&&page.mapped)page.resource->Unmap(0,nullptr);
        pages.clear();
    }
    for(auto& pages:m_FrameConstantPages) {
        for(auto& page:pages)if(page.resource&&page.mapped)page.resource->Unmap(0,nullptr);
        pages.clear();
    }
    m_FrameConstantAddress=0;
    std::fill(std::begin(m_InstanceFrameFence),std::end(m_InstanceFrameFence),0);
    ReleaseMaterialPreview();m_MaterialPreviewTime.Reset();m_MaterialPreviewReadback.Reset();m_MaterialPreviewTimed={};m_MaterialPreviewMetrics={};
    m_UploadedMeshes.clear();
    m_UploadedTextures.clear();
    m_BoundMaps={};m_TextureParameters={1,1,0,1};m_MaterialTables.clear();m_LastMaterialTable={};m_MaterialTableCount=0;m_EnvironmentTable={};m_FrameConstantsDirty=true;
    m_UploadedShaders.clear();
    ReleaseCameraPreview();
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
    for(auto& depth:m_ViewmodelDepth)depth={};
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
