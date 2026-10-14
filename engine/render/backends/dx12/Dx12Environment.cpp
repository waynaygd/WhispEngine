#define NOMINMAX
#include "Dx12RenderAdapter.h"
#ifdef _WIN32
#include "../../../external/d3dx12.h"
#include "../../../resources/EnvironmentResource.h"
#include <chrono>
#include <cmath>
#include <d3dcompiler.h>
#include <stdexcept>
#include <tracy/Tracy.hpp>
using Microsoft::WRL::ComPtr;
namespace
{
void Check(HRESULT h, const char *message)
{
    if (FAILED(h))
        throw std::runtime_error(message);
}
ComPtr<ID3DBlob> Compile(const char *source, const char *entry, const char *target)
{
    ComPtr<ID3DBlob> code, error;
    HRESULT h = D3DCompile(source, std::strlen(source), "IBL output", nullptr, nullptr, entry,
                           target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error);
    if (FAILED(h))
        throw std::runtime_error(error ? std::string(static_cast<char *>(error->GetBufferPointer()),
                                                     error->GetBufferSize())
                                       : "Output shader failed");
    return code;
}
float Decode(float v) { return v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f); }
float Half(unsigned h)
{
    unsigned e = (h >> 10) & 31, m = h & 1023;
    float f = e ? std::ldexp(1 + m / 1024.f, int(e) - 15) : std::ldexp(float(m), -24);
    return (h & 32768) ? -f : f;
}
} // namespace
void Dx12RenderAdapter::InitializeEnvironmentFallback()
{
    if (m_EnvironmentGpu.descriptors)
        return;
    D3D12_DESCRIPTOR_HEAP_DESC h{};
    h.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    h.NumDescriptors = 4;
    Check(m_Device->CreateDescriptorHeap(&h, IID_PPV_ARGS(&m_EnvironmentGpu.descriptors)),
          "Environment descriptors failed");
    auto cpu = m_EnvironmentGpu.descriptors->GetCPUDescriptorHandleForHeapStart();
    for (unsigned i = 0; i < 4; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC s{};
        s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Format = i == 3 ? DXGI_FORMAT_R32G32_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT;
        s.ViewDimension = i == 3 ? D3D12_SRV_DIMENSION_TEXTURE2D : D3D12_SRV_DIMENSION_TEXTURECUBE;
        if (i == 3)
            s.Texture2D.MipLevels = 1;
        else
            s.TextureCube.MipLevels = 1;
        m_Device->CreateShaderResourceView(nullptr, &s, cpu);
        cpu.ptr += m_SrvDescriptorSize;
    }
}
void Dx12RenderAdapter::ReleaseEnvironment()
{
    if (!m_Device)
    {
        m_EnvironmentGpu = {};
        return;
    }
    if (!m_FrameRecording)
        WaitForGpu();
    for (auto &r : m_EnvironmentGpu.textures)
        if (r && m_FrameRecording)
            m_RetiredResources[m_FrameIndex].emplace_back(r.Get());
    if (m_EnvironmentGpu.descriptors && m_FrameRecording)
        m_RetiredResources[m_FrameIndex].emplace_back(m_EnvironmentGpu.descriptors.Get());
    m_EnvironmentGpu = {};
    m_EnvironmentTable = {};
    m_EnvironmentDiagnostics = {};
}
bool Dx12RenderAdapter::UploadEnvironment(const EnvironmentResource &e, std::uint64_t generation)
{
    ZoneScopedN("IblUploadGPU");
    if (e.placeholder || !e.brdfLut || e.brdfLut->size() != e.lutResolution * e.lutResolution * 2)
        return false;
    auto start = std::chrono::steady_clock::now();
    EnvironmentGpu ready;
    std::vector<ComPtr<ID3D12Resource>> staging;
    try
    {
        D3D12_DESCRIPTOR_HEAP_DESC dh{};
        dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        dh.NumDescriptors = 4;
        Check(m_Device->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&ready.descriptors)),
              "Environment SRV allocation failed");
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> command;
        Check(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                               IID_PPV_ARGS(&allocator)),
              "IBL allocator failed");
        Check(m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
                                          nullptr, IID_PPV_ARGS(&command)),
              "IBL command failed");
        std::uint64_t memory = 0;
        auto cpu = ready.descriptors->GetCPUDescriptorHandleForHeapStart();
        for (unsigned i = 0; i < 4; ++i)
        {
            if (i == 3 && m_CachedBrdfGpu && e.lutResolution == 64)
            {
                ready.textures[i] = m_CachedBrdfGpu;
                auto desc = m_CachedBrdfGpu->GetDesc();
                memory += m_Device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
                D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                srv.Format = desc.Format;
                srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srv.Texture2D.MipLevels = 1;
                m_Device->CreateShaderResourceView(m_CachedBrdfGpu.Get(), &srv, cpu);
                cpu.ptr += m_SrvDescriptorSize;
                continue;
            }
            const EnvironmentCube *cube = i == 0   ? &e.environment
                                          : i == 1 ? &e.irradiance
                                          : i == 2 ? &e.prefiltered
                                                   : nullptr;
            unsigned size = cube ? cube->resolution : e.lutResolution,
                     mips = cube ? cube->mipCount : 1, n = cube ? 6 * mips : 1;
            if (!size || size > 1024 || !mips || mips > 11 ||
                (cube && cube->subresources.size() != n))
                throw std::runtime_error("Invalid environment subresources");
            auto desc = CD3DX12_RESOURCE_DESC::Tex2D(i == 3 ? DXGI_FORMAT_R32G32_FLOAT
                                                            : DXGI_FORMAT_R32G32B32A32_FLOAT,
                                                     size, size, cube ? 6 : 1, UINT16(mips));
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            Check(m_Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                    IID_PPV_ARGS(&ready.textures[i])),
                  "IBL texture allocation failed");
            memory += m_Device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
            std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp(n);
            std::vector<UINT> rows(n);
            std::vector<UINT64> rowBytes(n);
            UINT64 bytes = 0;
            m_Device->GetCopyableFootprints(&desc, 0, n, 0, fp.data(), rows.data(), rowBytes.data(),
                                            &bytes);
            ComPtr<ID3D12Resource> upload;
            heap.Type = D3D12_HEAP_TYPE_UPLOAD;
            auto buffer = CD3DX12_RESOURCE_DESC::Buffer(bytes);
            Check(m_Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&upload)),
                  "IBL upload allocation failed");
            void *mapped = nullptr;
            CD3DX12_RANGE empty(0, 0);
            Check(upload->Map(0, &empty, &mapped), "IBL map failed");
            for (unsigned sub = 0; sub < n; ++sub)
            {
                const auto &data = cube ? cube->subresources[sub] : *e.brdfLut;
                unsigned width = std::max(1u, size >> (sub % mips)), channels = i == 3 ? 2 : 4;
                if (data.size() != std::size_t(width) * width * channels)
                {
                    upload->Unmap(0, nullptr);
                    throw std::runtime_error("Invalid IBL payload size");
                }
                for (float value : data)
                    if (!std::isfinite(value) || value < 0)
                    {
                        upload->Unmap(0, nullptr);
                        throw std::runtime_error("Nonfinite IBL payload");
                    }
                for (unsigned y = 0; y < rows[sub]; ++y)
                    std::memcpy(static_cast<char *>(mapped) + fp[sub].Offset +
                                    y * fp[sub].Footprint.RowPitch,
                                data.data() + y * width * channels, std::size_t(rowBytes[sub]));
                D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
                src.pResource = upload.Get();
                src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                src.PlacedFootprint = fp[sub];
                dst.pResource = ready.textures[i].Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dst.SubresourceIndex = sub;
                command->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            }
            upload->Unmap(0, nullptr);
            staging.push_back(upload);
            auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
                ready.textures[i].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            command->ResourceBarrier(1, &transition);
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Format = desc.Format;
            srv.ViewDimension =
                cube ? D3D12_SRV_DIMENSION_TEXTURECUBE : D3D12_SRV_DIMENSION_TEXTURE2D;
            if (cube)
                srv.TextureCube.MipLevels = mips;
            else
                srv.Texture2D.MipLevels = 1;
            m_Device->CreateShaderResourceView(ready.textures[i].Get(), &srv, cpu);
            cpu.ptr += m_SrvDescriptorSize;
        }
        Check(command->Close(), "IBL upload close failed");
        ID3D12CommandList *lists[]{command.Get()};
        m_Queue->ExecuteCommandLists(1, lists);
        WaitForGpu();
        ReleaseEnvironment();
        m_EnvironmentGpu = std::move(ready);
        m_CachedBrdfGpu = m_EnvironmentGpu.textures[3];
        m_EnvironmentDiagnostics.status = "Ready";
        m_EnvironmentDiagnostics.generation = generation;
        m_EnvironmentDiagnostics.gpuBytes = memory;
        m_EnvironmentDiagnostics.cubeResolution = e.environment.resolution;
        m_EnvironmentDiagnostics.irradianceResolution = e.irradiance.resolution;
        m_EnvironmentDiagnostics.mipCount = e.prefiltered.mipCount;
        m_EnvironmentDiagnostics.lutResolution = e.lutResolution;
        m_EnvironmentDiagnostics.preprocessMs = e.preprocessMs;
        m_EnvironmentDiagnostics.uploadMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        return true;
    }
    catch (const std::exception &error)
    {
        m_EnvironmentDiagnostics.error = error.what();
        m_EnvironmentDiagnostics.status =
            m_EnvironmentGpu.textures[0] ? "Ready (upload failed; retained)" : "Failed";
        return false;
    }
}
bool Dx12RenderAdapter::BindEnvironment()
{
    InitializeEnvironmentFallback();
    if (!m_EnvironmentTable.ptr)
    {
        if (m_MaterialTableCount >= MaterialTablesPerFrame)
            return false;
        UINT first =
            MaxTextures + (m_FrameIndex * MaterialTablesPerFrame + m_MaterialTableCount++) * 4;
        auto dst = m_SrvHeap->GetCPUDescriptorHandleForHeapStart();
        dst.ptr += SIZE_T(first) * m_SrvDescriptorSize;
        m_Device->CopyDescriptorsSimple(
            4, dst, m_EnvironmentGpu.descriptors->GetCPUDescriptorHandleForHeapStart(),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_EnvironmentTable = m_SrvHeap->GetGPUDescriptorHandleForHeapStart();
        m_EnvironmentTable.ptr += UINT64(first) * m_SrvDescriptorSize;
    }
    m_CmdList->SetGraphicsRootDescriptorTable(8, m_EnvironmentTable);
    float angle = m_EnvironmentSettings.rotationDegrees * 3.14159265358979323846f / 180;
    float params[8] = {
        m_EnvironmentSettings.enabled && m_EnvironmentGpu.textures[0] ? 1.f : 0.f,
        m_EnvironmentSettings.diffuse ? 1.f : 0.f,
        m_EnvironmentSettings.specular ? 1.f : 0.f,
        m_EnvironmentSettings.intensity,
        std::cos(angle),
        std::sin(angle),
        float(m_EnvironmentDiagnostics.mipCount ? m_EnvironmentDiagnostics.mipCount - 1 : 0),
        0};
    m_CmdList->SetGraphicsRoot32BitConstants(9, 8, params, 0);
    return true;
}
void Dx12RenderAdapter::ReleaseHdrTargets()
{
    for (auto &target : m_HdrTargets)
    {
        if (m_FrameRecording)
        {
            if (target.color)
                m_RetiredResources[m_FrameIndex].emplace_back(target.color.Get());
            if (target.descriptors)
                m_RetiredResources[m_FrameIndex].emplace_back(target.descriptors.Get());
            if (target.rtvs)
                m_RetiredResources[m_FrameIndex].emplace_back(target.rtvs.Get());
        }
        target = {};
    }
    m_HdrActive = false;
}
void Dx12RenderAdapter::BeginHdrScene()
{
    const bool hdrSky=m_EditorVisualFrame.active&&m_EditorVisualFrame.backgroundEnabled&&m_EditorVisualFrame.settings.background==EditorBackground::Cubemap;
    if (!m_EnvironmentSettings.enabled&&!hdrSky)
        return;
    ZoneScopedN("HdrSceneBegin");
    UINT w = m_ViewportTarget.rendering ? m_ViewportTarget.width : m_Width,
         h = m_ViewportTarget.rendering ? m_ViewportTarget.height : m_Height;
    auto &target = m_HdrTargets[m_FrameIndex];
    if (target.width != w || target.height != h)
    {
        if (target.color)
            m_RetiredResources[m_FrameIndex].emplace_back(target.color.Get());
        if (target.descriptors)
            m_RetiredResources[m_FrameIndex].emplace_back(target.descriptors.Get());
        if (target.rtvs)
            m_RetiredResources[m_FrameIndex].emplace_back(target.rtvs.Get());
        target = {};
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heap.NumDescriptors = 1;
        Check(m_Device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&target.rtvs)), "HDR RTV failed");
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        Check(m_Device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&target.descriptors)),
              "HDR SRV failed");
        D3D12_HEAP_PROPERTIES props{};
        props.Type = D3D12_HEAP_TYPE_DEFAULT;
        auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, w, h, 1, 1, 1, 0,
                                                 D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        D3D12_CLEAR_VALUE clear{};
        clear.Format = desc.Format;
        Check(m_Device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
                                                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
                                                IID_PPV_ARGS(&target.color)),
              "HDR target failed");
        target.rtv = target.rtvs->GetCPUDescriptorHandleForHeapStart();
        m_Device->CreateRenderTargetView(target.color.Get(), nullptr, target.rtv);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = desc.Format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        m_Device->CreateShaderResourceView(
            target.color.Get(), &srv, target.descriptors->GetCPUDescriptorHandleForHeapStart());
        target.width = w;
        target.height = h;
    }
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(target.color.Get(),
                                                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                        D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_CmdList->ResourceBarrier(1, &barrier);
    float clear[4] = {Decode(m_SceneClear[0]), Decode(m_SceneClear[1]), Decode(m_SceneClear[2]), 1};
    m_CmdList->ClearRenderTargetView(target.rtv, clear, 0, nullptr);
    auto dsv = m_ViewportTarget.rendering ? m_ViewportTarget.dsv
                                          : m_DsvHeap->GetCPUDescriptorHandleForHeapStart();
    m_CmdList->OMSetRenderTargets(1, &target.rtv, FALSE, &dsv);
    m_HdrActive = true;
}
void Dx12RenderAdapter::EndHdrScene()
{
    if (!m_HdrActive)
        return;
    ZoneScopedN("HdrOutput");
    auto &target = m_HdrTargets[m_FrameIndex];
    auto barrier =
        CD3DX12_RESOURCE_BARRIER::Transition(target.color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_CmdList->ResourceBarrier(1, &barrier);
    if (!m_OutputPso)
    {
        const char *source = R"(
Texture2D<float4> scene:register(t0);SamplerState clampSampler:register(s0);cbuffer Output:register(b0){float exposure;float tone;float2 pad;};
struct Out{float4 pos:SV_Position;float2 uv:TEXCOORD;};Out VS(uint id:SV_VertexID){Out o;o.uv=float2((id<<1)&2,id&2);o.pos=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}
float Encode(float c){return c<=.0031308?c*12.92:1.055*pow(c,1/2.4)-.055;}
float4 PS(Out i):SV_Target{float scale=exp2(exposure);float3 c=min(max(scene.SampleLevel(clampSampler,i.uv,0).rgb,0),3.402823e38/max(scale,1))*scale;if(tone>.5)c=c/(1+c);c=saturate(c);return float4(Encode(c.r),Encode(c.g),Encode(c.b),1);})";
        auto vs = Compile(source, "VS", "vs_5_1"), ps = Compile(source, "PS", "ps_5_1");
        CD3DX12_DESCRIPTOR_RANGE range;
        range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        D3D12_ROOT_PARAMETER roots[2]{};
        roots[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        roots[0].DescriptorTable = {1, &range};
        roots[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        roots[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        roots[1].Constants = {0, 0, 4};
        roots[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        CD3DX12_STATIC_SAMPLER_DESC sampler(
            0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        D3D12_ROOT_SIGNATURE_DESC desc{
            2, roots, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        ComPtr<ID3DBlob> serialized, error;
        Check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error),
              "Output root failed");
        Check(m_Device->CreateRootSignature(0, serialized->GetBufferPointer(),
                                            serialized->GetBufferSize(),
                                            IID_PPV_ARGS(&m_OutputRoot)),
              "Output root allocation failed");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
        p.pRootSignature = m_OutputRoot.Get();
        p.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        p.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        p.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        p.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        p.DepthStencilState.DepthEnable = FALSE;
        p.DepthStencilState.StencilEnable = FALSE;
        p.SampleMask = UINT_MAX;
        p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        p.NumRenderTargets = 1;
        p.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        p.SampleDesc.Count = 1;
        Check(m_Device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&m_OutputPso)),
              "Output PSO failed");
    }
    if (m_MaterialTableCount >= MaterialTablesPerFrame)
    {
        m_HdrActive = false;
        return;
    }
    UINT first = MaxTextures + (m_FrameIndex * MaterialTablesPerFrame + m_MaterialTableCount++) * 4;
    auto cpu = m_SrvHeap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += SIZE_T(first) * m_SrvDescriptorSize;
    m_Device->CopyDescriptorsSimple(1, cpu,
                                    target.descriptors->GetCPUDescriptorHandleForHeapStart(),
                                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto gpu = m_SrvHeap->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += UINT64(first) * m_SrvDescriptorSize;
    auto rtv = m_ViewportTarget.rendering ? m_ViewportTarget.rtv
                                          : m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
    if (!m_ViewportTarget.rendering)
        rtv.ptr += SIZE_T(m_FrameIndex) * m_RtvSize;
    m_CmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    ID3D12DescriptorHeap *heaps[]{m_SrvHeap.Get()};
    m_CmdList->SetDescriptorHeaps(1, heaps);
    m_CmdList->SetGraphicsRootSignature(m_OutputRoot.Get());
    m_CmdList->SetPipelineState(m_OutputPso.Get());
    m_CmdList->SetGraphicsRootDescriptorTable(0, gpu);
    float params[4] = {m_EnvironmentSettings.exposure,
                       m_EnvironmentSettings.toneMapping ? 1.f : 0.f, 0, 0};
    if (m_ShadowConstants.parameters[3] > .5)
    {
        params[0] = 0;
        params[1] = 0;
    }
    m_CmdList->SetGraphicsRoot32BitConstants(1, 4, params, 0);
    m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_CmdList->DrawInstanced(3, 1, 0, 0);
    ++m_SubmissionStatistics.outputDrawCalls;
    m_HdrActive = false;
}
std::vector<float> Dx12RenderAdapter::ReadBackFloatTexture(ID3D12Resource *texture, unsigned sub,
                                                           unsigned channels)
{
    if (!texture || m_FrameRecording)
        return {};
    WaitForGpu();
    auto desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT rows = 0;
    UINT64 rowBytes = 0, bytes = 0;
    m_Device->GetCopyableFootprints(&desc, sub, 1, 0, &fp, &rows, &rowBytes, &bytes);
    ComPtr<ID3D12Resource> readback;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    auto buffer = CD3DX12_RESOURCE_DESC::Buffer(bytes);
    Check(m_Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                            IID_PPV_ARGS(&readback)),
          "HDR readback failed");
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> cmd;
    Check(
        m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
        "Readback allocator failed");
    Check(m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                      IID_PPV_ARGS(&cmd)),
          "Readback command failed");
    auto before = CD3DX12_RESOURCE_BARRIER::Transition(
        texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE, sub);
    cmd->ResourceBarrier(1, &before);
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = texture;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = sub;
    dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = fp;
    cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    auto after = CD3DX12_RESOURCE_BARRIER::Transition(
        texture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, sub);
    cmd->ResourceBarrier(1, &after);
    Check(cmd->Close(), "Readback close failed");
    ID3D12CommandList *commands[]{cmd.Get()};
    m_Queue->ExecuteCommandLists(1, commands);
    WaitForGpu();
    void *data = nullptr;
    D3D12_RANGE range{0, SIZE_T(bytes)};
    Check(readback->Map(0, &range, &data), "Readback map failed");
    std::vector<float> out(std::size_t(fp.Footprint.Width) * rows * channels);
    for (unsigned y = 0; y < rows; ++y)
    {
        auto row = static_cast<char *>(data) + fp.Offset + y * fp.Footprint.RowPitch;
        if (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
        {
            auto half = reinterpret_cast<const std::uint16_t *>(row);
            for (unsigned x = 0; x < fp.Footprint.Width * channels; ++x)
                out[std::size_t(y) * fp.Footprint.Width * channels + x] = Half(half[x]);
        }
        else
            std::memcpy(out.data() + std::size_t(y) * fp.Footprint.Width * channels, row,
                        std::size_t(rowBytes));
    }
    D3D12_RANGE empty{0, 0};
    readback->Unmap(0, &empty);
    return out;
}
std::vector<float> Dx12RenderAdapter::ReadBackHdrPixels()
{
    return ReadBackFloatTexture(m_HdrTargets[m_FrameIndex].color.Get(), 0, 4);
}
std::vector<float> Dx12RenderAdapter::ReadBackEnvironment(unsigned resource, unsigned sub)
{
    return resource < 4 ? ReadBackFloatTexture(m_EnvironmentGpu.textures[resource].Get(), sub,
                                               resource == 3 ? 2 : 4)
                        : std::vector<float>{};
}
#endif
