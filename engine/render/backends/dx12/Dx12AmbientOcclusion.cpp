#define NOMINMAX
#include "Dx12RenderAdapter.h"
#ifdef _WIN32
#include "../../../core/AssetPaths.h"
#include "../../../core/Logger.h"
#include "../../../external/d3dx12.h"
#include <d3dcompiler.h>
#include <chrono>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
namespace {void Check(HRESULT hr,const char* message){if(FAILED(hr))throw std::runtime_error(message);}}

void Dx12RenderAdapter::SwapAoPipeline(){
    std::swap(m_ShadowPso,m_AoDepthPso[0]);std::swap(m_ShadowInstancePso,m_AoDepthPso[1]);
    std::swap(m_ShadowDoublePso,m_AoDepthPso[2]);std::swap(m_ShadowDoubleInstancePso,m_AoDepthPso[3]);
    std::swap(m_ShadowMirrorPso,m_AoDepthPso[4]);std::swap(m_ShadowMirrorInstancePso,m_AoDepthPso[5]);
    std::swap(m_ShadowDepthBias,m_AoDepthBias);std::swap(m_ShadowSlopeBias,m_AoSlopeBias);
}
bool Dx12RenderAdapter::EnsureAoPipeline(){
    if(m_AoRawPso&&m_AoFilterPso&&m_AoDebugRawPso&&m_AoDebugFilteredPso)return true;
    try{
        const auto path=AssetPaths::ResolveShaderPath("dx12/ambient_occlusion.hlsl");
        auto compile=[&](const char* entry,const char* target){ComPtr<ID3DBlob> code,error;
            auto hr=D3DCompileFromFile(path.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,target,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
            if(FAILED(hr))throw std::runtime_error(error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"AO shader unavailable");return code;};
        auto vs=compile("VS","vs_5_1"),raw=compile("RawPS","ps_5_1"),filter=compile("FilterPS","ps_5_1"),debugRaw=compile("DebugRawPS","ps_5_1"),debugFiltered=compile("DebugFilteredPS","ps_5_1");
        CD3DX12_DESCRIPTOR_RANGE range;range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,3,0);
        CD3DX12_ROOT_PARAMETER roots[2];roots[0].InitAsConstantBufferView(0);roots[1].InitAsDescriptorTable(1,&range,D3D12_SHADER_VISIBILITY_PIXEL);
        CD3DX12_ROOT_SIGNATURE_DESC root(2,roots);ComPtr<ID3DBlob> blob,error;
        Check(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"AO root serialization failed");
        Check(m_Device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&m_AoRoot)),"AO root creation failed");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};p.pRootSignature=m_AoRoot.Get();p.VS={vs->GetBufferPointer(),vs->GetBufferSize()};p.PS={raw->GetBufferPointer(),raw->GetBufferSize()};
        p.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);p.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        p.DepthStencilState.DepthEnable=FALSE;p.DepthStencilState.StencilEnable=FALSE;p.SampleMask=UINT_MAX;p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        p.NumRenderTargets=1;p.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;p.SampleDesc.Count=1;
        Check(m_Device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&m_AoRawPso)),"AO raw PSO failed");
        p.PS={filter->GetBufferPointer(),filter->GetBufferSize()};p.RTVFormats[0]=DXGI_FORMAT_R32_FLOAT;
        Check(m_Device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&m_AoFilterPso)),"AO filter PSO failed");
        p.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;p.PS={debugRaw->GetBufferPointer(),debugRaw->GetBufferSize()};
        Check(m_Device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&m_AoDebugRawPso)),"AO debug raw PSO failed");
        p.PS={debugFiltered->GetBufferPointer(),debugFiltered->GetBufferSize()};
        Check(m_Device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&m_AoDebugFilteredPso)),"AO debug filter PSO failed");
        D3D12_QUERY_HEAP_DESC query{};query.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;query.Count=FrameCount*32*4;
        m_Device->CreateQueryHeap(&query,IID_PPV_ARGS(&m_AoTime));return true;
    }catch(const std::exception& e){m_AoDiagnostics.error=e.what();Logger::Get().Error("AO unavailable: "+m_AoDiagnostics.error);return false;}
}
void Dx12RenderAdapter::EnsureAoTarget(HdrTarget& target,UINT width,UINT height,DXGI_FORMAT format){
    if(target.width==width&&target.height==height)return;
    for(auto* r:{static_cast<IUnknown*>(target.color.Get()),static_cast<IUnknown*>(target.descriptors.Get()),static_cast<IUnknown*>(target.rtvs.Get())})if(r)m_RetiredResources[m_FrameIndex].emplace_back(r);
    target={};auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto desc=CD3DX12_RESOURCE_DESC::Tex2D(format,width,height,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    Check(m_Device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&target.color)),"AO target allocation failed");
    D3D12_DESCRIPTOR_HEAP_DESC h{};h.NumDescriptors=1;h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    Check(m_Device->CreateDescriptorHeap(&h,IID_PPV_ARGS(&target.rtvs)),"AO RTV failed");h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    Check(m_Device->CreateDescriptorHeap(&h,IID_PPV_ARGS(&target.descriptors)),"AO SRV failed");
    target.rtv=target.rtvs->GetCPUDescriptorHandleForHeapStart();m_Device->CreateRenderTargetView(target.color.Get(),nullptr,target.rtv);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    m_Device->CreateShaderResourceView(target.color.Get(),&srv,target.descriptors->GetCPUDescriptorHandleForHeapStart());
    target.width=width;target.height=height;target.allocationBytes=m_Device->GetResourceAllocationInfo(0,1,&desc).SizeInBytes;
}
bool Dx12RenderAdapter::BeginAmbientOcclusion(const std::array<float,16>& viewProjection,const std::array<float,16>& projection){
    const auto start=std::chrono::steady_clock::now();
    m_ActiveAo=UINT_MAX;m_AoDiagnostics.active=false;m_AoDiagnostics.depthDraws=0;
    if(!m_EnvironmentSettings.ambientOcclusion.enabled){m_AoDiagnostics.depthMs=m_AoDiagnostics.rawMs=m_AoDiagnostics.filterMs=m_AoDiagnostics.cpuMs=0;return false;}
    if(!m_FrameRecording||m_AoCursor>=32){m_AoDiagnostics.error="AO camera pass limit (32)";return false;}
    if(!EnsureAoPipeline())return false;
    try{
        const auto area=RenderAreaSize();const UINT width=area[0],height=area[1];
        if(!width||!height)return false;
        const UINT pass=m_AoCursor++;auto& pages=m_AoPasses[m_FrameIndex];if(pages.size()<=pass)pages.resize(pass+1);auto& f=pages[pass];f.ready=false;
        EnsureAoTarget(f.raw,width,height,DXGI_FORMAT_R16G16B16A16_FLOAT);EnsureAoTarget(f.filtered,width,height,DXGI_FORMAT_R32_FLOAT);
        if(f.width!=width||f.height!=height){
            if(f.depth)m_RetiredResources[m_FrameIndex].emplace_back(f.depth.Get());if(f.dsvHeap)m_RetiredResources[m_FrameIndex].emplace_back(f.dsvHeap.Get());f.depth.Reset();f.dsvHeap.Reset();
            auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);auto desc=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_TYPELESS,width,height,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
            D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT;clear.DepthStencil.Depth=1;
            Check(m_Device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,&clear,IID_PPV_ARGS(&f.depth)),"AO depth allocation failed");
            D3D12_DESCRIPTOR_HEAP_DESC h{};h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;h.NumDescriptors=1;Check(m_Device->CreateDescriptorHeap(&h,IID_PPV_ARGS(&f.dsvHeap)),"AO DSV failed");
            D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};dsv.Format=DXGI_FORMAT_D32_FLOAT;dsv.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
            m_Device->CreateDepthStencilView(f.depth.Get(),&dsv,f.dsvHeap->GetCPUDescriptorHandleForHeapStart());
            f.depthBytes=m_Device->GetResourceAllocationInfo(0,1,&desc).SizeInBytes;f.width=width;f.height=height;
        }
        auto buffer=[&](ComPtr<ID3D12Resource>& resource,D3D12_HEAP_TYPE type,UINT64 bytes){if(resource)return;auto heap=CD3DX12_HEAP_PROPERTIES(type);auto desc=CD3DX12_RESOURCE_DESC::Buffer(bytes);Check(m_Device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&resource)),"AO buffer allocation failed");};
        buffer(f.constants,D3D12_HEAP_TYPE_UPLOAD,256);buffer(f.readback,D3D12_HEAP_TYPE_READBACK,32);
        const auto& s=m_EnvironmentSettings.ambientOcclusion;
        const float values[]={projection[0],projection[5],projection[10],projection[14],float(width),float(height),projection[11],projection[15],s.radius,s.intensity,s.bias,float(12<<s.quality)};
        void* mapped=nullptr;D3D12_RANGE empty{};Check(f.constants->Map(0,&empty,&mapped),"AO constants map failed");std::memcpy(mapped,values,sizeof(values));f.constants->Unmap(0,nullptr);
        if(m_MaterialTableCount>=MaterialTablesPerFrame)throw std::runtime_error("AO descriptor snapshot limit");
        UINT first=MaxTextures+(m_FrameIndex*MaterialTablesPerFrame+m_MaterialTableCount++)*4;
        auto cpu=m_SrvHeap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(first)*m_SrvDescriptorSize;f.table=m_SrvHeap->GetGPUDescriptorHandleForHeapStart();f.table.ptr+=UINT64(first)*m_SrvDescriptorSize;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32_FLOAT;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        m_Device->CreateShaderResourceView(f.depth.Get(),&srv,cpu);cpu.ptr+=m_SrvDescriptorSize;
        m_Device->CopyDescriptorsSimple(1,cpu,f.raw.descriptors->GetCPUDescriptorHandleForHeapStart(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);cpu.ptr+=m_SrvDescriptorSize;
        m_Device->CopyDescriptorsSimple(1,cpu,f.filtered.descriptors->GetCPUDescriptorHandleForHeapStart(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        ShadowSettings settings;settings.depthBias=0;settings.slopeBias=0;
        SwapAoPipeline();if(!EnsureShadowPipeline(settings)){SwapAoPipeline();return false;}
        m_ActiveAo=pass;m_LastAoFrame=m_FrameIndex;m_LastAoPass=pass;m_AoSavedShadow=m_ShadowConstants;m_AoSavedStatistics=m_SubmissionStatistics;
        m_ShadowConstants.lightVP=viewProjection;m_ShadowActive=true;
        const auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(f.depth.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);m_CmdList->ResourceBarrier(1,&barrier);
        D3D12_VIEWPORT vp{0,0,float(width),float(height),0,1};D3D12_RECT rect{0,0,LONG(width),LONG(height)};m_CmdList->RSSetViewports(1,&vp);m_CmdList->RSSetScissorRects(1,&rect);
        auto dsv=f.dsvHeap->GetCPUDescriptorHandleForHeapStart();m_CmdList->OMSetRenderTargets(0,nullptr,FALSE,&dsv);m_CmdList->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
        if(m_AoTime)m_CmdList->EndQuery(m_AoTime.Get(),D3D12_QUERY_TYPE_TIMESTAMP,(m_FrameIndex*32+pass)*4);
        m_AoDiagnostics.cpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return true;
    }catch(const std::exception& e){m_AoDiagnostics.error=e.what();Logger::Get().Error("AO unavailable: "+m_AoDiagnostics.error);return false;}
}
void Dx12RenderAdapter::EndAmbientOcclusion(){
    if(m_ActiveAo==UINT_MAX||!m_ShadowActive)return;
    auto& f=m_AoPasses[m_FrameIndex][m_ActiveAo];UINT query=(m_FrameIndex*32+m_ActiveAo)*4;
    auto stamp=[&](UINT phase){if(m_AoTime)m_CmdList->EndQuery(m_AoTime.Get(),D3D12_QUERY_TYPE_TIMESTAMP,query+phase);};stamp(1);
    m_AoDiagnostics.depthDraws=unsigned(m_SubmissionStatistics.shadowDrawCalls-m_AoSavedStatistics.shadowDrawCalls);
    m_SubmissionStatistics.shadowDrawCalls=m_AoSavedStatistics.shadowDrawCalls;m_SubmissionStatistics.shadowInstancedDrawCalls=m_AoSavedStatistics.shadowInstancedDrawCalls;m_SubmissionStatistics.shadowCasters=m_AoSavedStatistics.shadowCasters;
    m_ShadowActive=false;m_ShadowConstants=m_AoSavedShadow;m_FrameConstantsDirty=true;SwapAoPipeline();
    auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(f.depth.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);m_CmdList->ResourceBarrier(1,&barrier);
    ID3D12DescriptorHeap* heaps[]{m_SrvHeap.Get()};m_CmdList->SetDescriptorHeaps(1,heaps);m_CmdList->SetGraphicsRootSignature(m_AoRoot.Get());m_CmdList->SetGraphicsRootConstantBufferView(0,f.constants->GetGPUVirtualAddress());m_CmdList->SetGraphicsRootDescriptorTable(1,f.table);m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    auto draw=[&](HdrTarget& target,ID3D12PipelineState* pso){auto before=CD3DX12_RESOURCE_BARRIER::Transition(target.color.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);m_CmdList->ResourceBarrier(1,&before);m_CmdList->OMSetRenderTargets(1,&target.rtv,FALSE,nullptr);m_CmdList->SetPipelineState(pso);m_CmdList->DrawInstanced(3,1,0,0);auto after=CD3DX12_RESOURCE_BARRIER::Transition(target.color.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);m_CmdList->ResourceBarrier(1,&after);};
    draw(f.raw,m_AoRawPso.Get());stamp(2);draw(f.filtered,m_AoFilterPso.Get());stamp(3);
    if(m_AoTime){m_CmdList->ResolveQueryData(m_AoTime.Get(),D3D12_QUERY_TYPE_TIMESTAMP,query,4,f.readback.Get(),0);f.pending=true;}
    f.ready=true;m_AoDiagnostics.active=true;m_AoDiagnostics.error.clear();m_AoDiagnostics.gpuBytes=0;
    for(auto& pages:m_AoPasses)for(auto& page:pages)m_AoDiagnostics.gpuBytes+=page.depthBytes+page.raw.allocationBytes+page.filtered.allocationBytes+(page.constants?256:0)+(page.readback?32:0);
    // Main pass begins immediately after this call; restore camera viewport/DSV
    // even when HDR is not used (AO must not force tone mapping or a new target).
    const bool viewport=m_ViewportTarget.rendering;auto rtv=viewport?m_ViewportTarget.rtv:m_RtvHeap->GetCPUDescriptorHandleForHeapStart();if(!viewport)rtv.ptr+=SIZE_T(m_FrameIndex)*m_RtvSize;
    auto dsv=viewport?m_ViewportTarget.dsv:m_DsvHeap->GetCPUDescriptorHandleForHeapStart();m_CmdList->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
}
void Dx12RenderAdapter::BindAmbientOcclusion(){
    UINT config[4]{};auto table=m_LightFrames[m_FrameIndex][m_ActiveLightPass].spotTable;
    if(m_PreparedScene&&!m_InViewmodelPass&&m_ActiveAo!=UINT_MAX&&m_EnvironmentSettings.ambientOcclusion.enabled){auto& f=m_AoPasses[m_FrameIndex][m_ActiveAo];if(f.ready){config[0]=1;config[2]=f.width;config[3]=f.height;table=f.table;table.ptr+=UINT64(2)*m_SrvDescriptorSize;}}
    m_CmdList->SetGraphicsRootDescriptorTable(16,table);m_CmdList->SetGraphicsRoot32BitConstants(17,4,config,0);
}
void Dx12RenderAdapter::DrawAmbientOcclusionDebug(){
    const auto& s=m_EnvironmentSettings.ambientOcclusion;
    if(!s.enabled||!s.debug||m_ActiveAo==UINT_MAX)return;
    auto& f=m_AoPasses[m_FrameIndex][m_ActiveAo];if(!f.ready)return;
    const bool viewport=m_ViewportTarget.rendering;auto rtv=viewport?m_ViewportTarget.rtv:m_RtvHeap->GetCPUDescriptorHandleForHeapStart();if(!viewport)rtv.ptr+=SIZE_T(m_FrameIndex)*m_RtvSize;
    m_CmdList->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
    ID3D12DescriptorHeap* heaps[]{m_SrvHeap.Get()};m_CmdList->SetDescriptorHeaps(1,heaps);m_CmdList->SetGraphicsRootSignature(m_AoRoot.Get());m_CmdList->SetGraphicsRootConstantBufferView(0,f.constants->GetGPUVirtualAddress());m_CmdList->SetGraphicsRootDescriptorTable(1,f.table);m_CmdList->SetPipelineState(s.debug==1?m_AoDebugRawPso.Get():m_AoDebugFilteredPso.Get());m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);m_CmdList->DrawInstanced(3,1,0,0);
}
void Dx12RenderAdapter::CollectAoTiming(UINT slot){
    double depth=0,raw=0,filter=0;bool ready=false;
    for(auto& f:m_AoPasses[slot])if(f.pending){void* data=nullptr;D3D12_RANGE range{0,32};if(SUCCEEDED(f.readback->Map(0,&range,&data))){const auto* t=static_cast<const UINT64*>(data);if(m_TimeFrequency){depth+=(t[1]-t[0])*1000./m_TimeFrequency;raw+=(t[2]-t[1])*1000./m_TimeFrequency;filter+=(t[3]-t[2])*1000./m_TimeFrequency;}D3D12_RANGE empty{};f.readback->Unmap(0,&empty);ready=true;}f.pending=false;}
    if(ready){m_AoDiagnostics.depthMs=depth;m_AoDiagnostics.rawMs=raw;m_AoDiagnostics.filterMs=filter;}
}
std::vector<float> Dx12RenderAdapter::ReadBackAmbientOcclusion(unsigned input){
    if(m_AoPasses[m_LastAoFrame].size()<=m_LastAoPass)return {};auto& f=m_AoPasses[m_LastAoFrame][m_LastAoPass];
    return ReadBackFloatTexture(input==0?f.depth.Get():input==1?f.raw.color.Get():f.filtered.color.Get(),0,input==1?4:1);
}
void Dx12RenderAdapter::ReleaseAmbientOcclusion(){
    for(auto& pages:m_AoPasses)pages.clear();for(auto& pso:m_AoDepthPso)pso.Reset();m_AoRawPso.Reset();m_AoFilterPso.Reset();m_AoDebugRawPso.Reset();m_AoDebugFilteredPso.Reset();m_AoRoot.Reset();m_AoTime.Reset();m_AoCursor=0;m_ActiveAo=UINT_MAX;m_AoDepthBias=-1;m_AoSlopeBias=-1;m_AoDiagnostics={true};
}
#endif
