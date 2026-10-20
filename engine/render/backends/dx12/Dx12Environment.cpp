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
    m_HdrNative=m_EnvironmentSettings.enabled||hdrSky;
    if (!m_HdrNative&&m_AntiAliasing==AntiAliasingMode::Off&&!m_Fog.active&&!m_ScreenEffects.Active())
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
        target.allocationBytes=m_Device->GetResourceAllocationInfo(0,1,&desc).SizeInBytes;
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
Texture2D<float4> scene:register(t0);SamplerState clampSampler:register(s0);cbuffer Output:register(b0){float exposure;float tone;float aa;float frameSeed;float4 grading;float4 tint;float4 vignette;float4 grainCA;};
struct Out{float4 pos:SV_Position;float2 uv:TEXCOORD;};Out VS(uint id:SV_VertexID){Out o;o.uv=float2((id<<1)&2,id&2);o.pos=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}
float Encode(float c){return c<=.0031308?c*12.92:1.055*pow(c,1/2.4)-.055;}
float3 DisplayColor(float3 radiance){float scale=exp2(exposure);float3 c=min(max(radiance,0),3.402823e38/max(scale,1))*scale;if(tone>.5)c=c/(1+c);c=saturate(c);return float3(Encode(c.r),Encode(c.g),Encode(c.b));}
float3 Color(float2 uv){
 [branch] if(aa<.5)return DisplayColor(scene.SampleLevel(clampSampler,uv,0).rgb);
 // FXAA interpolates display-referred colors AFTER tone mapping, not HDR radiance.
 // Four point loads avoid another target while preserving that nonlinear order.
 uint w,h;scene.GetDimensions(w,h);float2 p=uv*float2(w,h)-.5;int2 at=int2(floor(p)),last=int2(w,h)-1;float2 f=frac(p);
 float3 a=DisplayColor(scene.Load(int3(clamp(at,int2(0,0),last),0)).rgb);
 float3 b=DisplayColor(scene.Load(int3(clamp(at+int2(1,0),int2(0,0),last),0)).rgb);
 float3 c=DisplayColor(scene.Load(int3(clamp(at+int2(0,1),int2(0,0),last),0)).rgb);
 float3 d=DisplayColor(scene.Load(int3(clamp(at+int2(1,1),int2(0,0),last),0)).rgb);
 return lerp(lerp(a,b,f.x),lerp(c,d,f.x),f.y);
}
float Luma(float3 c){return dot(c,float3(.299,.587,.114));}
float3 ResolveDisplay(float2 uv){
 float3 center=Color(uv);if(aa<.5)return center;
 uint w,h;scene.GetDimensions(w,h);float2 pixel=1.0/float2(w,h);
 float3 nw=Color(uv+float2(-1,-1)*pixel),ne=Color(uv+float2(1,-1)*pixel),sw=Color(uv+float2(-1,1)*pixel),se=Color(uv+pixel);
 float lnw=Luma(nw),lne=Luma(ne),lsw=Luma(sw),lse=Luma(se),lm=Luma(center);
 float low=min(lm,min(min(lnw,lne),min(lsw,lse))),high=max(lm,max(max(lnw,lne),max(lsw,lse)));
 if(high-low<max(aa>1.5?.015625:.03125,high*(aa>1.5?.083333:.166667)))return center;
 float2 dir=float2(-((lnw+lne)-(lsw+lse)),(lnw+lsw)-(lne+lse));
 float reduce=max((lnw+lne+lsw+lse)*.03125,.0078125);float reciprocal=1/(min(abs(dir.x),abs(dir.y))+reduce);float span=aa>1.5?8:4;dir=clamp(dir*reciprocal,-span,span)*pixel;
 float3 a=.5*(Color(uv+dir*(1.0/3-.5))+Color(uv+dir*(2.0/3-.5)));
 float3 b=a*.5+.25*(Color(uv-dir*.5)+Color(uv+dir*.5));float lb=Luma(b);
 return lb<low||lb>high?a:b;
}
// Built-in stage: source samples resolve tone mapping and FXAA exactly once per
// sample. Later ordered fullscreen effects can consume this display-referred
// result in a separate ping-pong target at EndHdrScene, before UI composition.
float Hash(uint2 cell,uint seed){
 uint h=cell.x*1597334677u+cell.y*3812015801u+seed*2798796415u;
 h=(h^(h>>16))*2246822519u;h=(h^(h>>13))*3266489917u;
 return float(h^(h>>16))*(1.0/4294967296.0)-.5;
}
float3 BuiltInPostFX(float2 uv,float2 size){
 float2 p=(uv-.5)*2;float aspect=size.x/size.y;
 float2 radial=p*float2(aspect,1);float radius=saturate(length(radial)/length(float2(aspect,1)));
 float3 color;
 // Channel offsets precede all color operations; UVs stay inside texel centers.
 if(grainCA.z>0){
  float2 offset=radial/float2(aspect,1)*(.015*grainCA.z*pow(radius,grainCA.w));
  float2 edge=.5/size;
  float3 red=ResolveDisplay(clamp(uv+offset,edge,1-edge));
  float3 green=ResolveDisplay(uv);
  float3 blue=ResolveDisplay(clamp(uv-offset,edge,1-edge));
  color=float3(red.r,green.g,blue.b);
 }else color=ResolveDisplay(uv);
 float gray=dot(color,float3(.2126,.7152,.0722));
 color=(lerp(gray.xxx,color,grading.x)-.5)*grading.y+.5+grading.z;
 color*=tint.rgb;
 if(vignette.x>0){
  float shape=lerp(max(abs(p.x),abs(p.y)),radius,vignette.z);
  color*=1-vignette.x*smoothstep(1-vignette.y,1,shape);
 }
 if(grainCA.x>0){
  uint2 cell=uint2(floor(uv*size/grainCA.y));
  float phase=frameSeed*.25;uint seed=uint(floor(phase));float t=smoothstep(0,1,frac(phase));
  float noise=lerp(Hash(cell,seed),Hash(cell,(seed+1)&255u),t);
  color+=noise*(.06*grainCA.x);
 }
 return saturate(color);
}
float4 PS(Out i):SV_Target{
 if(grading.w<.5)return float4(ResolveDisplay(i.uv),1);
 uint w,h;scene.GetDimensions(w,h);
 return float4(BuiltInPostFX(i.uv,float2(w,h)),1);
})";
        auto vs = Compile(source, "VS", "vs_5_1"), ps = Compile(source, "PS", "ps_5_1");
        CD3DX12_DESCRIPTOR_RANGE range;
        range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        D3D12_ROOT_PARAMETER roots[2]{};
        roots[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        roots[0].DescriptorTable = {1, &range};
        roots[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        roots[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        roots[1].Constants = {0, 0, 20};
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
    const auto& fx=m_ScreenEffects;
    float params[20] = {m_HdrNative?m_EnvironmentSettings.exposure:0,
                       m_HdrNative&&m_EnvironmentSettings.toneMapping ? 1.f : 0.f, float(m_AntiAliasing), float(m_PostFxFrame&1023u),
                       fx.saturation,fx.contrast,fx.brightness,fx.Active()?1.f:0.f,
                       fx.tint[0],fx.tint[1],fx.tint[2],1,
                       fx.vignetteIntensity,fx.vignetteSmoothness,fx.vignetteRoundness,0,
                       fx.grainIntensity,fx.grainScale,fx.aberrationIntensity,fx.aberrationFalloff};
    if (m_ShadowConstants.parameters[3] > .5)
    {
        params[0] = 0;
        params[1] = 0;
    }
    m_CmdList->SetGraphicsRoot32BitConstants(1, 20, params, 0);
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
