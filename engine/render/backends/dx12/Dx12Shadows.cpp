#include "Dx12RenderAdapter.h"
#ifdef _WIN32
#include "../../../core/AssetPaths.h"
#include "../../../core/Logger.h"
#include "../../../external/d3dx12.h"
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <fstream>
#include <iterator>
#include <cstring>
#include <stdexcept>
#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#else
#define ZoneScopedN(name) ((void)0)
#endif
using Microsoft::WRL::ComPtr;
namespace {
void CheckShadow(HRESULT hr,const char* message) { if(FAILED(hr))throw std::runtime_error(message); }
ComPtr<ID3DBlob> CompileShadow(const std::string& source,const char* entry) {
    ComPtr<ID3DBlob> code,error;
    const auto hr=D3DCompile(source.data(),source.size(),"shadow_depth.hlsl",nullptr,nullptr,entry,"vs_5_1",
        D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
    if(FAILED(hr))throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"Shadow VS compile failed");
    return code;
}
}
bool Dx12RenderAdapter::EnsureShadowPipeline(const ShadowSettings& s) {
    if(m_ShadowPso&&m_ShadowDepthBias==s.depthBias&&m_ShadowSlopeBias==s.slopeBias)return true;
    try {
        if(!m_ShadowVs||!m_ShadowInstanceVs) {
            std::ifstream stream(AssetPaths::ResolveShaderPath("dx12/shadow_depth.hlsl"),std::ios::binary);
            if(!stream)throw std::runtime_error("shadow_depth.hlsl unavailable");
            const std::string source{std::istreambuf_iterator<char>(stream),{}};
            auto ordinary=CompileShadow(source,"VSMain"),instanced=CompileShadow(source,"VSInstancedMain");
            m_ShadowVs=ordinary;m_ShadowInstanceVs=instanced;
        }
        const auto& vs=m_ShadowVs;const auto& instanceVs=m_ShadowInstanceVs;
        ComPtr<ID3D12RootSignature> root=m_ShadowRoot;
        if(!root) {
            D3D12_ROOT_PARAMETER parameters[2]{};
            for(UINT i=0;i<2;++i) { parameters[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                parameters[i].Constants={i,0,16};parameters[i].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX; }
            D3D12_ROOT_SIGNATURE_DESC desc{2,parameters,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
            ComPtr<ID3DBlob> blob,error;
            CheckShadow(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"Shadow root serialization failed");
            CheckShadow(m_Device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"Shadow root creation failed");
        }
        D3D12_INPUT_ELEMENT_DESC layout[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"INSTANCE_MODEL",0,DXGI_FORMAT_R32G32B32A32_FLOAT,1,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
            {"INSTANCE_MODEL",1,DXGI_FORMAT_R32G32B32A32_FLOAT,1,16,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
            {"INSTANCE_MODEL",2,DXGI_FORMAT_R32G32B32A32_FLOAT,1,32,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
            {"INSTANCE_MODEL",3,DXGI_FORMAT_R32G32B32A32_FLOAT,1,48,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature=root.Get();desc.VS={vs->GetBufferPointer(),vs->GetBufferSize()};desc.InputLayout={layout,1};
        desc.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.DepthBias=s.depthBias;desc.RasterizerState.SlopeScaledDepthBias=s.slopeBias;
        desc.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState=CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.SampleMask=UINT_MAX;desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.DSVFormat=DXGI_FORMAT_D32_FLOAT;desc.SampleDesc.Count=1; // depth only: no PS, no RTV
        ComPtr<ID3D12PipelineState> ordinary,instanced,doubleOrdinary,doubleInstanced,mirrorOrdinary,mirrorInstanced;
        CheckShadow(m_Device->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&ordinary)),"Shadow PSO creation failed");
        desc.VS={instanceVs->GetBufferPointer(),instanceVs->GetBufferSize()};desc.InputLayout={layout,5};
        CheckShadow(m_Device->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&instanced)),"Shadow instanced PSO creation failed");
        desc.RasterizerState.FrontCounterClockwise=TRUE;
        CheckShadow(m_Device->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&mirrorInstanced)),"Shadow mirror instanced PSO failed");
        desc.VS={vs->GetBufferPointer(),vs->GetBufferSize()};desc.InputLayout={layout,1};
        CheckShadow(m_Device->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&mirrorOrdinary)),"Shadow mirror PSO failed");
        desc.VS={instanceVs->GetBufferPointer(),instanceVs->GetBufferSize()};desc.InputLayout={layout,5};
        desc.RasterizerState.FrontCounterClockwise=FALSE;
        desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        CheckShadow(m_Device->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&doubleInstanced)),"Shadow double instanced PSO failed");
        desc.VS={vs->GetBufferPointer(),vs->GetBufferSize()};desc.InputLayout={layout,1};
        CheckShadow(m_Device->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&doubleOrdinary)),"Shadow double PSO failed");
        if(m_FrameRecording) {
            if(m_ShadowMirrorPso)m_RetiredResources[m_FrameIndex].emplace_back(m_ShadowMirrorPso.Get());
            if(m_ShadowMirrorInstancePso)m_RetiredResources[m_FrameIndex].emplace_back(m_ShadowMirrorInstancePso.Get());
            if(m_ShadowDoublePso)m_RetiredResources[m_FrameIndex].emplace_back(m_ShadowDoublePso.Get());
            if(m_ShadowDoubleInstancePso)m_RetiredResources[m_FrameIndex].emplace_back(m_ShadowDoubleInstancePso.Get());
            if(m_ShadowPso)m_RetiredResources[m_FrameIndex].emplace_back(m_ShadowPso.Get());
            if(m_ShadowInstancePso)m_RetiredResources[m_FrameIndex].emplace_back(m_ShadowInstancePso.Get());
        }
        m_ShadowMirrorPso=mirrorOrdinary;m_ShadowMirrorInstancePso=mirrorInstanced;
        m_ShadowRoot=root;m_ShadowPso=ordinary;m_ShadowInstancePso=instanced;m_ShadowDoublePso=doubleOrdinary;m_ShadowDoubleInstancePso=doubleInstanced;
        m_ShadowDepthBias=s.depthBias;m_ShadowSlopeBias=s.slopeBias;m_LastShadowError.clear();return true;
    } catch(const std::exception& e) {
        if(m_LastShadowError!=e.what())Logger::Get().Warn(std::string("Shadow pass unavailable: ")+e.what());
        m_LastShadowError=e.what();return false;
    }
}
bool Dx12RenderAdapter::EnsureShadowTarget(UINT resolution) {
    auto& target=m_ShadowTargets[m_FrameIndex];
    if(target.depth&&target.resolution==resolution)return true;
    if(m_FreeTextureDescriptorIndices.empty())return false;
    ShadowTarget next;next.resolution=resolution;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    const auto desc=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_TYPELESS,resolution,resolution,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT;clear.DepthStencil.Depth=1;
    if(FAILED(m_Device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,&clear,IID_PPV_ARGS(&next.depth))))return false;
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeap{};dsvHeap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;dsvHeap.NumDescriptors=1;
    if(FAILED(m_Device->CreateDescriptorHeap(&dsvHeap,IID_PPV_ARGS(&next.heap))))return false;
    next.dsv=next.heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};dsv.Format=DXGI_FORMAT_D32_FLOAT;dsv.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
    m_Device->CreateDepthStencilView(next.depth.Get(),&dsv,next.dsv);
    next.descriptor=m_FreeTextureDescriptorIndices.back();m_FreeTextureDescriptorIndices.pop_back();
    auto cpu=m_SrvHeap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(next.descriptor)*m_SrvDescriptorSize;
    next.srv=m_SrvHeap->GetGPUDescriptorHandleForHeapStart();next.srv.ptr+=UINT64(next.descriptor)*m_SrvDescriptorSize;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32_FLOAT;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;
    srv.Shader4ComponentMapping=D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0,0,0,D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);
    m_Device->CreateShaderResourceView(next.depth.Get(),&srv,cpu);
    if(target.depth) { // the editor may already have referenced the old SRV in this frame
        m_RetiredResources[m_FrameIndex].emplace_back(target.depth.Get());
        m_RetiredResources[m_FrameIndex].emplace_back(target.heap.Get());
        m_RetiredTextureDescriptors[m_FrameIndex].push_back(target.descriptor);
    }
    target=std::move(next);return true;
}
void Dx12RenderAdapter::BindShadows() {
    ZoneScopedN("ShadowBind");
    if(m_NullShadowDescriptor==UINT_MAX&&!m_FreeTextureDescriptorIndices.empty()) {
        m_NullShadowDescriptor=m_FreeTextureDescriptorIndices.back();m_FreeTextureDescriptorIndices.pop_back();
        auto cpu=m_SrvHeap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(m_NullShadowDescriptor)*m_SrvDescriptorSize;
        m_NullShadowSrv=m_SrvHeap->GetGPUDescriptorHandleForHeapStart();m_NullShadowSrv.ptr+=UINT64(m_NullShadowDescriptor)*m_SrvDescriptorSize;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32_FLOAT;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels=1;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        m_Device->CreateShaderResourceView(nullptr,&srv,cpu);
    }
    // b3 is bound by the immutable frame CBV snapshot in the main pass.
    // Disabled shaders never sample t1. Even descriptor exhaustion leaves a valid heap address.
    auto srv=m_ShadowConstants.parameters[0]>.5f?m_ShadowTargets[m_FrameIndex].srv:m_NullShadowSrv;
    if(!srv.ptr)srv=m_SrvHeap->GetGPUDescriptorHandleForHeapStart();
    m_CmdList->SetGraphicsRootDescriptorTable(5,srv);
}
bool Dx12RenderAdapter::BeginShadowPass(const ShadowFrame& frame) {
    m_PreparedScene=true;m_CachedInstances.clear();m_ShadowConstants={};m_FrameConstantsDirty=true;m_ShadowActive=false;
    if(m_TimeHeap&&m_TimePassCount>=16)m_TimeOverflow=true;
    m_TimeCurrentPass=m_TimeHeap&&m_TimePassCount<16?m_TimePassCount++:UINT_MAX;
    ShadowTimestamp(0);
    if(frame.constants.parameters[0]<.5f||!EnsureShadowPipeline(frame.settings)) {ShadowTimestamp(1);return false;}
    if(!EnsureShadowTarget(frame.settings.resolution)) {
        if(!m_ShadowResourceWarning)Logger::Get().Warn("Shadow resource allocation unavailable; Lambert fallback");
        m_ShadowResourceWarning=true;ShadowTimestamp(1);return false;
    }
    m_ShadowResourceWarning=false;
    m_ShadowConstants=frame.constants;m_FrameConstantsDirty=true;m_ShadowActive=true;m_LastShadowSlot=m_FrameIndex;
    auto& target=m_ShadowTargets[m_FrameIndex];
    { ZoneScopedN("ShadowResourceTransition");
      const auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(target.depth.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);
      m_CmdList->ResourceBarrier(1,&barrier); }
    D3D12_VIEWPORT vp{0,0,float(target.resolution),float(target.resolution),0,1};
    D3D12_RECT rect{0,0,LONG(target.resolution),LONG(target.resolution)};
    m_CmdList->RSSetViewports(1,&vp);m_CmdList->RSSetScissorRects(1,&rect);
    m_CmdList->OMSetRenderTargets(0,nullptr,FALSE,&target.dsv);
    m_CmdList->ClearDepthStencilView(target.dsv,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
    m_SubmissionStatistics.shadowResolution=target.resolution;
    m_SubmissionStatistics.shadowCoverage=frame.settings.coverage;
    m_SubmissionStatistics.shadowDepthRange=frame.settings.depthRange;return true;
}
void Dx12RenderAdapter::DrawShadowMesh(RenderMeshHandle handle,const RenderInstanceData& instance) {
    ZoneScopedN("ShadowDepthDraw");
    const auto it=m_UploadedMeshes.find(handle.value);if(!m_ShadowActive||it==m_UploadedMeshes.end())return;
    if(!MaterialIndexCount(it->second.indexCount))return;
    m_CmdList->SetGraphicsRootSignature(m_ShadowRoot.Get());m_CmdList->SetPipelineState(m_MaterialState.doubleSided?m_ShadowDoublePso.Get():(m_MaterialState.reverseWinding?m_ShadowMirrorPso.Get():m_ShadowPso.Get()));
    m_CmdList->SetGraphicsRoot32BitConstants(0,16,m_ShadowConstants.lightVP.data(),0);
    m_CmdList->SetGraphicsRoot32BitConstants(1,16,instance.model.data(),0);
    m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_CmdList->IASetVertexBuffers(0,1,&it->second.vertexView);m_CmdList->IASetIndexBuffer(&it->second.indexView);
    m_CmdList->DrawIndexedInstanced(MaterialIndexCount(it->second.indexCount),1,m_MeshRange.firstIndex,0,0);
    ++m_SubmissionStatistics.shadowDrawCalls;++m_SubmissionStatistics.shadowCasters;
}
bool Dx12RenderAdapter::DrawShadowMeshInstanced(RenderMeshHandle handle,std::span<const RenderInstanceData> instances) {
    ZoneScopedN("ShadowInstancedDraw");
    const auto it=m_UploadedMeshes.find(handle.value);D3D12_VERTEX_BUFFER_VIEW view{};
    if(!m_ShadowActive||it==m_UploadedMeshes.end()||!MaterialIndexCount(it->second.indexCount)||!UploadInstances(instances,view))return false;
    m_CmdList->SetGraphicsRootSignature(m_ShadowRoot.Get());m_CmdList->SetPipelineState(m_MaterialState.doubleSided?m_ShadowDoubleInstancePso.Get():(m_MaterialState.reverseWinding?m_ShadowMirrorInstancePso.Get():m_ShadowInstancePso.Get()));
    m_CmdList->SetGraphicsRoot32BitConstants(0,16,m_ShadowConstants.lightVP.data(),0);
    m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_VERTEX_BUFFER_VIEW views[]{it->second.vertexView,view};m_CmdList->IASetVertexBuffers(0,2,views);
    m_CmdList->IASetIndexBuffer(&it->second.indexView);m_CmdList->DrawIndexedInstanced(MaterialIndexCount(it->second.indexCount),UINT(instances.size()),m_MeshRange.firstIndex,0,0);
    ++m_SubmissionStatistics.shadowDrawCalls;++m_SubmissionStatistics.shadowInstancedDrawCalls;
    m_SubmissionStatistics.shadowCasters+=instances.size();return true;
}
void Dx12RenderAdapter::EndShadowPass() {
    if(!m_ShadowActive)return;
    { ZoneScopedN("ShadowResourceTransition");
      const auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(m_ShadowTargets[m_FrameIndex].depth.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      m_CmdList->ResourceBarrier(1,&barrier); }
    m_ShadowActive=false;
    const bool viewport=m_ViewportTarget.rendering;
    auto rtv=viewport?m_ViewportTarget.rtv:m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
    if(!viewport)rtv.ptr+=SIZE_T(m_FrameIndex)*m_RtvSize;
    const auto dsv=viewport?m_ViewportTarget.dsv:m_DsvHeap->GetCPUDescriptorHandleForHeapStart();
    const UINT width=viewport?m_ViewportTarget.width:m_Width,height=viewport?m_ViewportTarget.height:m_Height;
    D3D12_VIEWPORT vp{0,0,float(width),float(height),0,1};D3D12_RECT rect{0,0,LONG(width),LONG(height)};
    m_CmdList->RSSetViewports(1,&vp);m_CmdList->RSSetScissorRects(1,&rect);m_CmdList->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
    ShadowTimestamp(1);
}
void Dx12RenderAdapter::BeginMainPass() {m_WorldEffectsDone=false;BeginHdrScene();PrepareSceneLights();ShadowTimestamp(2);DrawEditorVisual(false);}
void Dx12RenderAdapter::EndMainPass() {if(!m_WorldEffectsDone){ApplyFog();DrawEditorVisual(true);}ShadowTimestamp(3);ShadowTimestamp(4);EndHdrScene();ShadowTimestamp(5);m_PreparedScene=false;m_CachedInstances.clear();}
std::uint64_t Dx12RenderAdapter::GetShadowPreviewTextureId() const {return m_ShadowTargets[m_LastShadowSlot].srv.ptr;}
void Dx12RenderAdapter::ReleaseShadowResources() {
    if(m_ShadowActive&&m_FrameRecording)EndShadowPass();
    m_ShadowActive=false;
    for(auto& target:m_ShadowTargets)if(target.depth) {
        if(m_FrameRecording) {
            m_RetiredResources[m_FrameIndex].emplace_back(target.depth.Get());m_RetiredResources[m_FrameIndex].emplace_back(target.heap.Get());
            m_RetiredTextureDescriptors[m_FrameIndex].push_back(target.descriptor);
        } else {if(m_Device)WaitForGpu();m_FreeTextureDescriptorIndices.push_back(target.descriptor);}
        target={};
    }
    m_ShadowConstants={};m_FrameConstantsDirty=true;
}
void Dx12RenderAdapter::InitializeGpuTiming() {
    if(FAILED(m_Queue->GetTimestampFrequency(&m_TimeFrequency))||!m_TimeFrequency)return;
    D3D12_QUERY_HEAP_DESC desc{};desc.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;desc.Count=FrameCount*TimingQueries;
    if(FAILED(m_Device->CreateQueryHeap(&desc,IID_PPV_ARGS(&m_TimeHeap))))return;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    auto buffer=CD3DX12_RESOURCE_DESC::Buffer(TimingQueries*sizeof(UINT64));
    for(auto& resource:m_TimeReadback)if(FAILED(m_Device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&resource)))) {
        m_TimeHeap.Reset();return;
    }
}
void Dx12RenderAdapter::ShadowTimestamp(UINT phase) {
    if(m_TimeCurrentPass!=UINT_MAX){
        m_CmdList->EndQuery(m_TimeHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,m_FrameIndex*TimingQueries+2+TimingStages*m_TimeCurrentPass+phase);
        if(phase>=6)m_TimeExtras[m_TimeCurrentPass]|=1u<<(phase-6);
    }
}
void Dx12RenderAdapter::CollectGpuTiming(UINT slot) {
    ZoneScopedN("ShadowResolve");
    if(!m_TimePending[slot]||!m_TimeFrequency||m_Fence->GetCompletedValue()<m_InstanceFrameFence[slot])return;
    void* mapped=nullptr;D3D12_RANGE range{0,m_TimePending[slot]*sizeof(UINT64)};
    if(FAILED(m_TimeReadback[slot]->Map(0,&range,&mapped)))return;
    auto* ticks=static_cast<UINT64*>(mapped);RenderGpuTimings result{};result.valid=m_TimeComplete[slot];
    const double milliseconds=1000.0/m_TimeFrequency;result.frameMs=(ticks[1]-ticks[0])*milliseconds;
    for(UINT q=2;q<m_TimePending[slot];q+=TimingStages) {
        result.shadowMs+=(ticks[q+1]-ticks[q])*milliseconds;result.mainMs+=(ticks[q+3]-ticks[q+2])*milliseconds;result.outputMs+=(ticks[q+5]-ticks[q+4])*milliseconds;
        const auto extras=m_TimeSlotExtras[slot][(q-2)/TimingStages];
        if((extras&3)==3)result.customPostFxMs+=(ticks[q+7]-ticks[q+6])*milliseconds;
        if((extras&12)==12)result.runtimeUIMs+=(ticks[q+9]-ticks[q+8])*milliseconds;
    }
    D3D12_RANGE noWrites{0,0};m_TimeReadback[slot]->Unmap(0,&noWrites);m_GpuTimings=result;m_TimePending[slot]=0;
}
unsigned Dx12RenderAdapter::GetValidationErrorCount() const {
    ComPtr<ID3D12InfoQueue> info;if(FAILED(m_Device.As(&info)))return 0;
    unsigned count=0;
    for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i) {
        SIZE_T bytes=0;info->GetMessage(i,nullptr,&bytes);std::vector<char> buffer(bytes);
        auto* message=reinterpret_cast<D3D12_MESSAGE*>(buffer.data());info->GetMessage(i,message,&bytes);
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR) {++count;Logger::Get().Error(message->pDescription);}
    }
    return count;
}
std::vector<float> Dx12RenderAdapter::ReadBackShadowDepth() {
    auto& target=m_ShadowTargets[m_LastShadowSlot];if(!target.depth||m_FrameRecording)return {};
    WaitForGpu();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 rowBytes=0,bytes=0;
    const auto desc=target.depth->GetDesc();m_Device->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowBytes,&bytes);
    ComPtr<ID3D12Resource> staging;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    const auto buffer=CD3DX12_RESOURCE_DESC::Buffer(bytes);
    CheckShadow(m_Device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&staging)),"Depth readback allocation failed");
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> command;
    CheckShadow(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Depth readback allocator failed");
    CheckShadow(m_Device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&command)),"Depth readback command failed");
    auto before=CD3DX12_RESOURCE_BARRIER::Transition(target.depth.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);command->ResourceBarrier(1,&before);
    D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=target.depth.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource=staging.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;command->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    auto after=CD3DX12_RESOURCE_BARRIER::Transition(target.depth.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);command->ResourceBarrier(1,&after);
    CheckShadow(command->Close(),"Depth readback close failed");ID3D12CommandList* lists[]{command.Get()};m_Queue->ExecuteCommandLists(1,lists);WaitForGpu();
    void* mapped=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)};CheckShadow(staging->Map(0,&range,&mapped),"Depth readback map failed");
    std::vector<float> result(std::size_t(rows)*target.resolution);
    for(UINT y=0;y<rows;++y)std::memcpy(result.data()+y*target.resolution,static_cast<const char*>(mapped)+footprint.Offset+y*footprint.Footprint.RowPitch,target.resolution*sizeof(float));
    D3D12_RANGE noWrites{0,0};staging->Unmap(0,&noWrites);return result;
}
bool Dx12RenderAdapter::SaveFrameImage(const std::string& path) {
    if(m_FrameRecording)return false;
    auto pixels=ReadBackFramePixels();std::ofstream out(path,std::ios::binary);
    out<<"P6\n"<<m_Width<<' '<<m_Height<<"\n255\n";
    for(std::size_t i=0;i<pixels.size();i+=4)out.write(reinterpret_cast<const char*>(pixels.data()+i),3);
    return out.good();
}
#endif
