#define NOMINMAX
#include "Dx12RenderAdapter.h"
#ifdef _WIN32
#include "../../../core/Logger.h"
#include "../../../external/d3dx12.h"
#include <stdexcept>

void Dx12RenderAdapter::SwapSpotPipeline(){
    std::swap(m_ShadowPso,m_SpotPso[0]);std::swap(m_ShadowInstancePso,m_SpotPso[1]);
    std::swap(m_ShadowDoublePso,m_SpotPso[2]);std::swap(m_ShadowDoubleInstancePso,m_SpotPso[3]);
    std::swap(m_ShadowMirrorPso,m_SpotPso[4]);std::swap(m_ShadowMirrorInstancePso,m_SpotPso[5]);
    std::swap(m_ShadowDepthBias,m_SpotDepthBias);std::swap(m_ShadowSlopeBias,m_SpotSlopeBias);
}
void Dx12RenderAdapter::SetSpotShadowFrames(std::span<const SpotShadowFrame> frames,unsigned requested){
    if(m_ActiveSpot!=UINT_MAX)throw std::runtime_error("Cannot replace active Spot shadow pass");
    m_SpotFrames.clear();if(!frames.empty())m_SpotFrames.assign(frames.begin(),frames.begin()+std::min<size_t>(frames.size(),MaxSpotShadows));
    m_SpotGpu={};m_SpotDiagnostics.requested=requested;m_SpotDiagnostics.active=0;m_SpotDiagnostics.drawCalls=0;
    const auto previousError=m_SpotDiagnostics.error;
    const unsigned expected=std::min(requested,MaxSpotShadows);
    m_SpotDiagnostics.failed=expected>m_SpotFrames.size()?expected-unsigned(m_SpotFrames.size()):0;m_SpotDiagnostics.error.clear();m_SpotDiagnostics.overBudget=requested>MaxSpotShadows?requested-MaxSpotShadows:0;
    if(m_SpotDiagnostics.failed){m_SpotDiagnostics.error="Spot shadow projection rejected; check light transform/range. Light remains unshadowed";if(m_SpotDiagnostics.error!=previousError)Logger::Get().Warn(m_SpotDiagnostics.error);}
    if(m_SpotDiagnostics.overBudget&&m_SpotDiagnostics.overBudget!=m_LastSpotOverflow)
        Logger::Get().Warn("Spot shadow budget exceeded: "+std::to_string(requested)+" visible requests, 4 slots; remaining lights stay unshadowed");
    m_LastSpotOverflow=m_SpotDiagnostics.overBudget;
}
bool Dx12RenderAdapter::BeginSpotShadowPass(unsigned slot){
    if(!m_FrameRecording||m_ShadowActive||slot>=m_SpotFrames.size())return false;
    const auto& f=m_SpotFrames[slot];
    SwapSpotPipeline();
    const bool ready=EnsureShadowPipeline(f.shadow.settings)&&EnsureShadowTarget(f.shadow.settings.resolution,m_SpotTargets[m_FrameIndex][slot]);
    if(!ready){SwapSpotPipeline();++m_SpotDiagnostics.failed;m_SpotDiagnostics.error="Spot shadow resource/pipeline unavailable; light remains unshadowed";Logger::Get().Warn(m_SpotDiagnostics.error);return false;}
    m_ActiveSpot=slot;m_LastSpotFrame=m_FrameIndex;m_SpotSavedDirectional=m_ShadowConstants;
    m_ShadowConstants=f.shadow.constants;m_ShadowActive=true;m_FrameConstantsDirty=true;
    auto& target=m_SpotTargets[m_FrameIndex][slot];
    auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(target.depth.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);m_CmdList->ResourceBarrier(1,&barrier);
    D3D12_VIEWPORT vp{0,0,float(target.resolution),float(target.resolution),0,1};D3D12_RECT rect{0,0,LONG(target.resolution),LONG(target.resolution)};
    m_CmdList->RSSetViewports(1,&vp);m_CmdList->RSSetScissorRects(1,&rect);m_CmdList->OMSetRenderTargets(0,nullptr,FALSE,&target.dsv);m_CmdList->ClearDepthStencilView(target.dsv,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
    m_SpotGpu[slot]={f.shadow.constants.lightVP,{1.f/target.resolution,f.receiverBias,f.strength,1},{float(f.lightIndex+1),f.pcf?1.f:0.f,std::abs(f.shadow.projection[14]),0}};
    ++m_SpotDiagnostics.active;
    m_SpotDiagnostics.mapBytes=0;for(auto& page:m_SpotTargets)for(auto& map:page)if(map.depth)m_SpotDiagnostics.mapBytes+=std::uint64_t(map.resolution)*map.resolution*4;
    return true;
}
void Dx12RenderAdapter::PrepareSpotBindings(LightFrameResources& frame){
    frame.spotCount=0;for(auto& s:m_SpotGpu)frame.spotCount+=s.config[0]>0;
    if(!frame.spotConstants){auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);auto desc=CD3DX12_RESOURCE_DESC::Buffer(512);
        if(FAILED(m_Device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&frame.spotConstants))))throw std::runtime_error("Spot shadow constant allocation failed");}
    void* data=nullptr;D3D12_RANGE empty{};if(FAILED(frame.spotConstants->Map(0,&empty,&data)))throw std::runtime_error("Spot shadow constant map failed");
    std::memcpy(data,m_SpotGpu.data(),sizeof(m_SpotGpu));frame.spotConstants->Unmap(0,nullptr);
    if(m_MaterialTableCount>=MaterialTablesPerFrame)throw std::runtime_error("Spot shadow descriptor snapshot limit");
    UINT first=MaxTextures+(m_FrameIndex*MaterialTablesPerFrame+m_MaterialTableCount++)*4;
    frame.spotTable=m_SrvHeap->GetGPUDescriptorHandleForHeapStart();frame.spotTable.ptr+=UINT64(first)*m_SrvDescriptorSize;
    auto cpu=m_SrvHeap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(first)*m_SrvDescriptorSize;
    for(unsigned slot=0;slot<MaxSpotShadows;++slot){auto& map=m_SpotTargets[m_FrameIndex][slot];
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32_FLOAT;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        m_Device->CreateShaderResourceView(m_SpotGpu[slot].config[0]>0?map.depth.Get():nullptr,&srv,cpu);
        cpu.ptr+=m_SrvDescriptorSize;
    }
}
void Dx12RenderAdapter::ReleaseSpotResources(){
    if(m_ActiveSpot!=UINT_MAX&&m_FrameRecording)EndShadowPass();
    if(!m_FrameRecording&&m_Device)WaitForGpu();
    for(auto& page:m_SpotTargets)for(auto& map:page)if(map.depth){
        if(m_FrameRecording){m_RetiredResources[m_FrameIndex].emplace_back(map.depth.Get());m_RetiredResources[m_FrameIndex].emplace_back(map.heap.Get());m_RetiredTextureDescriptors[m_FrameIndex].push_back(map.descriptor);}
        else m_FreeTextureDescriptorIndices.push_back(map.descriptor);
        map={};
    }
    m_SpotFrames.clear();m_SpotGpu={};m_SpotDiagnostics={};m_LastSpotOverflow=0;
}
std::vector<float> Dx12RenderAdapter::ReadBackSpotShadowDepth(unsigned slot){
    if(m_FrameRecording||slot>=MaxSpotShadows)return {};
    auto& map=m_SpotTargets[m_LastSpotFrame][slot];return map.depth?ReadBackFloatTexture(map.depth.Get(),0,1):std::vector<float>{};
}
#endif
