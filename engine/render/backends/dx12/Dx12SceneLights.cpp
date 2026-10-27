#define NOMINMAX
#include "Dx12RenderAdapter.h"
#ifdef _WIN32
#include "../../../core/AssetPaths.h"
#include "../../../core/Logger.h"
#include "../../../external/d3dx12.h"
#include <chrono>
#include <d3dcompiler.h>
#include <fstream>
#include <stdexcept>
#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#else
#define ZoneScopedN(n) ((void)0)
#endif
using Microsoft::WRL::ComPtr;
namespace
{
void Check(HRESULT hr,const char* what){if(FAILED(hr))throw std::runtime_error(what);}
ComPtr<ID3D12Resource> Buffer(ID3D12Device* d,UINT64 size,D3D12_HEAP_TYPE type,
                            D3D12_RESOURCE_STATES state,bool uav=false)
{
    D3D12_HEAP_PROPERTIES h{};h.Type=type;
    auto desc=CD3DX12_RESOURCE_DESC::Buffer(size,uav?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE);
    ComPtr<ID3D12Resource> r;Check(d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&r)),"Scene light buffer allocation failed");return r;
}
}
void Dx12RenderAdapter::SetSceneLights(std::span<const GpuSceneLight> lights,LightRenderingMode mode,const SceneLightDiagnostics& d)
{
    // Copy snapshot: neither component pointers nor caller spans survive this call.
    auto old=m_SceneLightDiagnostics;m_SceneLightDiagnostics=d;
    m_SceneLightDiagnostics.tiles=old.tiles;m_SceneLightDiagnostics.indices=old.indices;
    m_SceneLightDiagnostics.maxPerTile=old.maxPerTile;m_SceneLightDiagnostics.overflow=old.overflow;
    m_SceneLightDiagnostics.averagePerTile=old.averagePerTile;m_SceneLightDiagnostics.cullingGpuMs=old.cullingGpuMs;
    m_LightMode=mode;m_PendingLights.clear();
    if(lights.size()>MaxSceneLights){m_SceneLightDiagnostics.error="Scene light limit exceeded (16384): local pass disabled, no truncation";Logger::Get().Warn(m_SceneLightDiagnostics.error);return;}
    m_PendingLights.assign(lights.begin(),lights.end());
}
bool Dx12RenderAdapter::EnsureLightCulling()
{
    if(m_LightCullPso)return true;if(m_LightCullAttempted){m_SceneLightDiagnostics.error=m_LightCullError;return false;}m_LightCullAttempted=true;
    try{
        std::ifstream stream(AssetPaths::ResolveShaderPath("dx12/light_cull.hlsl"),std::ios::binary);
        if(!stream)throw std::runtime_error("light_cull.hlsl unavailable");
        std::string source{std::istreambuf_iterator<char>(stream),{}};ComPtr<ID3DBlob> code,error;
        auto hr=D3DCompile(source.data(),source.size(),"light_cull.hlsl",nullptr,nullptr,"CSMain","cs_5_1",
                           D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
        if(FAILED(hr))throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"Light cull compile failed");
        D3D12_ROOT_PARAMETER p[5]{};
        p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p[0].Descriptor={0,0};
        for(UINT i=0;i<3;++i){p[1+i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;p[1+i].Descriptor={i,0};}
        p[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[4].Constants={0,0,8};
        D3D12_ROOT_SIGNATURE_DESC desc{5,p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> root;
        Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&root,&error),"Light root serialization failed");
        Check(m_Device->CreateRootSignature(0,root->GetBufferPointer(),root->GetBufferSize(),IID_PPV_ARGS(&m_LightCullRoot)),"Light root creation failed");
        D3D12_COMPUTE_PIPELINE_STATE_DESC cs{};cs.pRootSignature=m_LightCullRoot.Get();cs.CS={code->GetBufferPointer(),code->GetBufferSize()};
        Check(m_Device->CreateComputePipelineState(&cs,IID_PPV_ARGS(&m_LightCullPso)),"Light compute PSO failed");
        D3D12_QUERY_HEAP_DESC q{};q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;q.Count=FrameCount*MaxLightPasses*2;
        m_Device->CreateQueryHeap(&q,IID_PPV_ARGS(&m_LightTimeHeap));return true;
    }catch(const std::exception& e){m_LightCullError=e.what();m_SceneLightDiagnostics.error=m_LightCullError;Logger::Get().Warn(std::string("Forward+ fallback: ")+e.what());return false;}
}
void Dx12RenderAdapter::PrepareSceneLights()
{
    ZoneScopedN("LightCullPrepare");auto start=std::chrono::steady_clock::now();
    if(m_LightPassCursor>=MaxLightPasses)throw std::runtime_error("Scene light pass capacity exceeded");
    const UINT pass=m_LightPassCursor++;
    auto& pages=m_LightFrames[m_FrameIndex];if(pages.size()<=pass)pages.resize(pass+1);
    auto& f=pages[pass];m_ActiveLightPass=pass;
    const UINT w=m_ViewportTarget.rendering?m_ViewportTarget.width:m_Width,h=m_ViewportTarget.rendering?m_ViewportTarget.height:m_Height;
    const UINT tx=(w+15)/16,ty=(h+15)/16;const UINT64 tiles=UINT64(tx)*ty;
    if(!tiles||tiles>1048576)throw std::runtime_error("Forward+ viewport tile limit exceeded");
    UINT64 lightCapacity=16;while(lightCapacity<m_PendingLights.size())lightCapacity*=2;
    const bool tiled=m_LightMode==LightRenderingMode::Tiled&&!m_PendingLights.empty()&&EnsureLightCulling();
    // Every pass has its own immutable snapshot. Fence wait precedes reuse of this frame slot.
    if(f.lightCapacity<lightCapacity){
        f.lights=Buffer(m_Device.Get(),lightCapacity*sizeof(GpuSceneLight)+256,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        f.lightCapacity=lightCapacity;
    }
    if(f.tileCapacity<tiles){
        UINT64 cap=1;while(cap<tiles)cap*=2;
        f.counts=Buffer(m_Device.Get(),cap*8,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,true);
        f.indices=Buffer(m_Device.Get(),cap*LightTileCapacity*4,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,true);
        f.tileCapacity=cap;
    }
    if(!f.stats){f.stats=Buffer(m_Device.Get(),16,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,true);
        f.readback=Buffer(m_Device.Get(),64,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);}
    void* data=nullptr;D3D12_RANGE empty{0,0};Check(f.lights->Map(0,&empty,&data),"Light upload map failed");
    std::memset(data,0,sizeof(GpuSceneLight));
    if(!m_PendingLights.empty())std::memcpy(data,m_PendingLights.data(),m_PendingLights.size()*sizeof(GpuSceneLight));
    UINT64 zeros=f.lightCapacity*sizeof(GpuSceneLight);std::memset(static_cast<char*>(data)+zeros,0,16);f.lights->Unmap(0,nullptr);
    if(f.pending){auto b=CD3DX12_RESOURCE_BARRIER::Transition(f.stats.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);m_CmdList->ResourceBarrier(1,&b);}
    m_CmdList->CopyBufferRegion(f.stats.Get(),0,f.lights.Get(),zeros,16);
    f.count=UINT(m_PendingLights.size());f.tilesX=tx;f.tilesY=ty;f.width=w;f.height=h;f.optimized=tiled;
    PrepareSpotBindings(f);
    if(tiled){
        D3D12_RESOURCE_BARRIER barriers[]={
            CD3DX12_RESOURCE_BARRIER::Transition(f.counts.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            CD3DX12_RESOURCE_BARRIER::Transition(f.indices.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            CD3DX12_RESOURCE_BARRIER::Transition(f.stats.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
        m_CmdList->ResourceBarrier(3,barriers);m_CmdList->SetPipelineState(m_LightCullPso.Get());m_CmdList->SetComputeRootSignature(m_LightCullRoot.Get());
        m_CmdList->SetComputeRootShaderResourceView(0,f.lights->GetGPUVirtualAddress());
        m_CmdList->SetComputeRootUnorderedAccessView(1,f.counts->GetGPUVirtualAddress());
        m_CmdList->SetComputeRootUnorderedAccessView(2,f.indices->GetGPUVirtualAddress());
        m_CmdList->SetComputeRootUnorderedAccessView(3,f.stats->GetGPUVirtualAddress());
        UINT constants[8]={f.count,tx,ty,LightTileCapacity,w,h,0,0};m_CmdList->SetComputeRoot32BitConstants(4,8,constants,0);
        UINT query=(m_FrameIndex*MaxLightPasses+pass)*2;
        if(m_LightTimeHeap)m_CmdList->EndQuery(m_LightTimeHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,query);
        m_CmdList->Dispatch(tx,ty,1);
        if(m_LightTimeHeap){m_CmdList->EndQuery(m_LightTimeHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,query+1);
            m_CmdList->ResolveQueryData(m_LightTimeHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,query,2,f.readback.Get(),32);}
        D3D12_RESOURCE_BARRIER after[]={
            CD3DX12_RESOURCE_BARRIER::Transition(f.counts.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(f.indices.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(f.stats.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE)};
        m_CmdList->ResourceBarrier(3,after);
    }else{auto b=CD3DX12_RESOURCE_BARRIER::Transition(f.stats.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);m_CmdList->ResourceBarrier(1,&b);}
    m_CmdList->CopyBufferRegion(f.readback.Get(),0,f.stats.Get(),0,16);f.pending=true;
    m_SceneLightDiagnostics.optimized=tiled;m_SceneLightDiagnostics.capacity=UINT(f.lightCapacity);
    m_SceneLightDiagnostics.uploadBytes=f.count*sizeof(GpuSceneLight);
    m_SceneLightDiagnostics.gpuBytes=f.lightCapacity*sizeof(GpuSceneLight)+256+f.tileCapacity*(8+LightTileCapacity*4)+80;
    m_SceneLightDiagnostics.cullingCpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
void Dx12RenderAdapter::BindSceneLights()
{
    if(m_ActiveLightPass==UINT_MAX)PrepareSceneLights();
    const auto& f=m_LightFrames[m_FrameIndex][m_ActiveLightPass];
    m_CmdList->SetGraphicsRootShaderResourceView(10,f.lights->GetGPUVirtualAddress());
    m_CmdList->SetGraphicsRootShaderResourceView(11,f.counts->GetGPUVirtualAddress());
    m_CmdList->SetGraphicsRootShaderResourceView(12,f.indices->GetGPUVirtualAddress());
    UINT c[8]={f.count,f.tilesX,f.tilesY,f.optimized?1u:0u,f.width,f.height,LightTileCapacity,f.spotCount};
    m_CmdList->SetGraphicsRoot32BitConstants(13,8,c,0);
    m_CmdList->SetGraphicsRootShaderResourceView(14,f.spotConstants->GetGPUVirtualAddress());
    m_CmdList->SetGraphicsRootDescriptorTable(15,f.spotTable);
}
void Dx12RenderAdapter::CollectSceneLights(UINT slot)
{
    for(auto& f:m_LightFrames[slot])if(f.pending){
        void* data=nullptr;D3D12_RANGE range{0,64};if(FAILED(f.readback->Map(0,&range,&data)))continue;
        auto* stats=static_cast<UINT*>(data);auto* ticks=reinterpret_cast<UINT64*>(static_cast<char*>(data)+32);
        m_SceneLightDiagnostics.tiles=f.optimized?f.tilesX*f.tilesY:0;
        m_SceneLightDiagnostics.indices=stats[0];m_SceneLightDiagnostics.maxPerTile=stats[2];m_SceneLightDiagnostics.overflow=stats[3];
        m_SceneLightDiagnostics.averagePerTile=m_SceneLightDiagnostics.tiles?double(stats[1])/m_SceneLightDiagnostics.tiles:0;
        m_SceneLightDiagnostics.cullingGpuMs=f.optimized&&m_LightTimeHeap&&m_TimeFrequency?(ticks[1]-ticks[0])*1000.0/m_TimeFrequency:0;
        f.readback->Unmap(0,nullptr);
        // Keep pending true to describe stats' COPY_SOURCE state; fence guards subsequent writes.
    }
}
void Dx12RenderAdapter::ReleaseSceneLights()
{
    for(auto& pages:m_LightFrames)pages.clear();m_PendingLights.clear();
    m_LightTimeHeap.Reset();m_LightCullPso.Reset();m_LightCullRoot.Reset();m_LightCullAttempted=false;m_LightCullError.clear();
    m_ActiveLightPass=UINT_MAX;m_LightPassCursor=0;m_SceneLightDiagnostics={};
}
std::vector<std::uint32_t> Dx12RenderAdapter::ReadBackLightTiles()
{
    if(m_FrameRecording||m_ActiveLightPass==UINT_MAX)return {};WaitForGpu();
    auto& f=m_LightFrames[m_FrameIndex][m_ActiveLightPass];if(!f.optimized)return {};
    UINT64 count=UINT64(f.tilesX)*f.tilesY;
    auto read=Buffer(m_Device.Get(),count*8,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> command;
    Check(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Tile readback allocator");
    Check(m_Device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&command)),"Tile readback command");
    auto a=CD3DX12_RESOURCE_BARRIER::Transition(f.counts.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);command->ResourceBarrier(1,&a);
    command->CopyBufferRegion(read.Get(),0,f.counts.Get(),0,count*8);
    a=CD3DX12_RESOURCE_BARRIER::Transition(f.counts.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);command->ResourceBarrier(1,&a);
    Check(command->Close(),"Tile readback close");ID3D12CommandList* lists[]{command.Get()};m_Queue->ExecuteCommandLists(1,lists);WaitForGpu();
    void* p=nullptr;D3D12_RANGE range{0,SIZE_T(count*8)};Check(read->Map(0,&range,&p),"Tile readback map");
    std::vector<UINT> result(static_cast<UINT*>(p),static_cast<UINT*>(p)+count*2);read->Unmap(0,nullptr);CollectSceneLights(m_FrameIndex);return result;
}
#endif
