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

bool Dx12RenderAdapter::EnsureFogPipeline(){
    if(m_FogPso&&m_FogVolumePso&&m_FogCompositePso)return true;
    try{
        auto path=AssetPaths::ResolveShaderPath("dx12/fog.hlsl");
        auto compile=[&](const char* entry,const char* target){ComPtr<ID3DBlob> code,error;auto hr=D3DCompileFromFile(path.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,target,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);if(FAILED(hr))throw std::runtime_error(error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"Fog shader unavailable");return code;};
        auto vs=compile("VS","vs_5_1"),ps=compile("PS","ps_5_1"),integrate=compile("VolumePS","ps_5_1"),composite=compile("CompositePS","ps_5_1");
        CD3DX12_DESCRIPTOR_RANGE ranges[2];ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,3,0);ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,4);
        CD3DX12_ROOT_PARAMETER roots[3];roots[0].InitAsConstantBufferView(0);roots[1].InitAsDescriptorTable(2,ranges,D3D12_SHADER_VISIBILITY_PIXEL);roots[2].InitAsShaderResourceView(3);
        CD3DX12_ROOT_SIGNATURE_DESC root(3,roots);ComPtr<ID3DBlob> serialized,error;
        Check(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error),"Fog root serialization failed");
        Check(m_Device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&m_FogRoot)),"Fog root allocation failed");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};p.pRootSignature=m_FogRoot.Get();p.VS={vs->GetBufferPointer(),vs->GetBufferSize()};p.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        p.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);p.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;p.DepthStencilState.DepthEnable=FALSE;p.DepthStencilState.StencilEnable=FALSE;p.SampleMask=UINT_MAX;p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;p.NumRenderTargets=1;p.RTVFormats[0]=DXGI_FORMAT_R32G32B32A32_FLOAT;p.SampleDesc.Count=1;
        Check(m_Device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&m_FogPso)),"Fog PSO allocation failed");
        p.PS={integrate->GetBufferPointer(),integrate->GetBufferSize()};Check(m_Device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&m_FogVolumePso)),"Fog integration PSO failed");
        p.PS={composite->GetBufferPointer(),composite->GetBufferSize()};Check(m_Device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&m_FogCompositePso)),"Fog reconstruction PSO failed");
        D3D12_QUERY_HEAP_DESC query{};query.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;query.Count=FrameCount*64;
        m_Device->CreateQueryHeap(&query,IID_PPV_ARGS(&m_FogTime));return true;
    }catch(const std::exception& error){m_FogDiagnostics.error=error.what();Logger::Get().Error("Fog disabled: "+m_FogDiagnostics.error);return false;}
}
void Dx12RenderAdapter::EnsureFogTarget(HdrTarget& target,UINT width,UINT height){
    if(target.width!=width||target.height!=height){
            for(auto* resource:{static_cast<IUnknown*>(target.color.Get()),static_cast<IUnknown*>(target.descriptors.Get()),static_cast<IUnknown*>(target.rtvs.Get())})if(resource)m_RetiredResources[m_FrameIndex].emplace_back(resource);target={};
            auto props=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);auto desc=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT,width,height,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
            Check(m_Device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&target.color)),"Fog color allocation failed");
            D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.NumDescriptors=1;Check(m_Device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&target.rtvs)),"Fog RTV heap failed");heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;Check(m_Device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&target.descriptors)),"Fog SRV heap failed");
            target.rtv=target.rtvs->GetCPUDescriptorHandleForHeapStart();m_Device->CreateRenderTargetView(target.color.Get(),nullptr,target.rtv);
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=desc.Format;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;m_Device->CreateShaderResourceView(target.color.Get(),&srv,target.descriptors->GetCPUDescriptorHandleForHeapStart());
            target.width=width;target.height=height;target.allocationBytes=m_Device->GetResourceAllocationInfo(0,1,&desc).SizeInBytes;
        }
}
void Dx12RenderAdapter::ApplyFog(){
    const auto start=std::chrono::steady_clock::now();m_FogDiagnostics={true};m_FogDiagnostics.error.clear();
    m_FogDiagnostics.activeCount=m_Fog.activeCount;
    if(m_Fog.activeCount>1)m_FogDiagnostics.error="Multiple enabled Fog components: lowest Entity index wins";
    if(m_Fog.invalidCount)m_FogDiagnostics.error="Invalid Fog components were ignored";
    for(auto& frames:m_FogPasses)for(auto& f:frames)m_FogDiagnostics.gpuBytes+=f.target.allocationBytes+f.volume.allocationBytes+(f.constants?512:0);
    if(!m_Fog.active)return;
    if(!ValidFog(m_Fog.settings)||!m_HdrActive||m_ActiveLightPass==UINT_MAX||m_FogPassCursor>=32){m_FogDiagnostics.error="Fog pass unavailable: invalid parameters, missing HDR/lights, or pass limit";return;}
    if(!EnsureFogPipeline())return;
    try{
        const UINT pass=m_FogPassCursor++;auto& frames=m_FogPasses[m_FrameIndex];if(frames.size()<=pass)frames.resize(pass+1);auto& f=frames[pass];auto& source=m_HdrTargets[m_FrameIndex];auto& target=f.target;
        const bool volumetric=m_Fog.settings.mode==FogMode::Volumetric;
        EnsureFogTarget(target,source.width,source.height);
        if(volumetric)EnsureFogTarget(f.volume,(source.width+1)/2,(source.height+1)/2);
        if(!f.constants){auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);auto desc=CD3DX12_RESOURCE_DESC::Buffer(512);Check(m_Device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&f.constants)),"Fog constants failed");}
        if(m_FogTime&&!f.readback){auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);auto desc=CD3DX12_RESOURCE_DESC::Buffer(16);Check(m_Device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&f.readback)),"Fog timing failed");}
        if(m_MaterialTableCount>=MaterialTablesPerFrame)throw std::runtime_error("Fog descriptor table limit");
        const UINT first=MaxTextures+(m_FrameIndex*MaterialTablesPerFrame+m_MaterialTableCount++)*4;
        f.shadowSrvIndex=first+2;
        auto cpu=m_SrvHeap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(first)*m_SrvDescriptorSize;
        m_Device->CopyDescriptorsSimple(1,cpu,source.descriptors->GetCPUDescriptorHandleForHeapStart(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);cpu.ptr+=m_SrvDescriptorSize;
        auto* depth=m_ViewportTarget.rendering?m_ViewportTarget.depth.Get():m_Depth.Get();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32_FLOAT;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;m_Device->CreateShaderResourceView(depth,&srv,cpu);cpu.ptr+=m_SrvDescriptorSize;
        const bool shadow=m_ShadowConstants.parameters[0]>.5f&&m_ShadowTargets[m_FrameIndex].depth;
        m_Device->CreateShaderResourceView(shadow?m_ShadowTargets[m_FrameIndex].depth.Get():nullptr,&srv,cpu);
        cpu.ptr+=m_SrvDescriptorSize;
        if(volumetric)m_Device->CopyDescriptorsSimple(1,cpu,f.volume.descriptors->GetCPUDescriptorHandleForHeapStart(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        else{srv.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;m_Device->CreateShaderResourceView(nullptr,&srv,cpu);}
        const auto& s=m_Fog.settings;const auto& camera=m_Fog.camera;const auto& view=camera.view;
        std::array<float,76> constants{};auto put=[&](int slot,const auto& values){std::copy(values.begin(),values.end(),constants.begin()+slot*4);};
        put(0,m_Camera);put(1,std::array<float,4>{view[0],view[4],view[8],0});put(2,std::array<float,4>{view[1],view[5],view[9],0});put(3,std::array<float,4>{view[2],view[6],view[10],camera.projection[15]});
        put(4,std::array<float,4>{camera.projection[0],camera.projection[5],camera.projection[10],camera.projection[14]});put(5,s.color);put(6,std::array<float,4>{s.startDistance,s.endDistance,s.density,float(s.mode)});put(7,std::array<float,4>{s.baseHeight,s.heightFalloff,s.heightDensity,s.maxDistance});put(8,s.scatteringColor);constants[35]=s.anisotropy;
        const auto& lights=m_LightFrames[m_FrameIndex][m_ActiveLightPass];
        put(18,std::array<float,4>{float(source.width),float(source.height),0,0});
        put(9,std::array<float,4>{float(FogSamples(s.quality)),s.sceneLighting?1.f:0.f,s.extinction,float(std::min(lights.count,64u))});put(10,m_Lighting.directionEnabled);put(11,m_Lighting.colorIntensity);put(12,m_Lighting.ambientIntensity);put(13,m_ShadowConstants.lightVP);put(17,std::array<float,4>{shadow?1.f:0.f,m_ShadowConstants.sampling[1],m_ShadowConstants.parameters[1],m_ShadowConstants.coverage[0]});
        void* mapped=nullptr;D3D12_RANGE empty{0,0};Check(f.constants->Map(0,&empty,&mapped),"Fog constants map failed");std::memcpy(mapped,constants.data(),sizeof(constants));f.constants->Unmap(0,nullptr);
        D3D12_RESOURCE_BARRIER barriers[]={CD3DX12_RESOURCE_BARRIER::Transition(source.color.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),CD3DX12_RESOURCE_BARRIER::Transition(target.color.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET),CD3DX12_RESOURCE_BARRIER::Transition(depth,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)};
        m_CmdList->ResourceBarrier(3,barriers);m_CmdList->OMSetRenderTargets(1,&target.rtv,FALSE,nullptr);
        ID3D12DescriptorHeap* heaps[]{m_SrvHeap.Get()};m_CmdList->SetDescriptorHeaps(1,heaps);m_CmdList->SetGraphicsRootSignature(m_FogRoot.Get());m_CmdList->SetPipelineState(m_FogPso.Get());m_CmdList->SetGraphicsRootConstantBufferView(0,f.constants->GetGPUVirtualAddress());
        auto gpu=m_SrvHeap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=UINT64(first)*m_SrvDescriptorSize;m_CmdList->SetGraphicsRootDescriptorTable(1,gpu);m_CmdList->SetGraphicsRootShaderResourceView(2,lights.lights->GetGPUVirtualAddress());
        UINT query=m_FrameIndex*64+pass*2;if(m_FogTime)m_CmdList->EndQuery(m_FogTime.Get(),D3D12_QUERY_TYPE_TIMESTAMP,query);
        m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        if(volumetric){
            auto before=CD3DX12_RESOURCE_BARRIER::Transition(f.volume.color.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);m_CmdList->ResourceBarrier(1,&before);
            D3D12_VIEWPORT vp{0,0,float(f.volume.width),float(f.volume.height),0,1};D3D12_RECT rect{0,0,LONG(f.volume.width),LONG(f.volume.height)};
            m_CmdList->RSSetViewports(1,&vp);m_CmdList->RSSetScissorRects(1,&rect);m_CmdList->OMSetRenderTargets(1,&f.volume.rtv,FALSE,nullptr);m_CmdList->SetPipelineState(m_FogVolumePso.Get());m_CmdList->DrawInstanced(3,1,0,0);
            auto ready=CD3DX12_RESOURCE_BARRIER::Transition(f.volume.color.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);m_CmdList->ResourceBarrier(1,&ready);
            vp.Width=float(source.width);vp.Height=float(source.height);rect.right=LONG(source.width);rect.bottom=LONG(source.height);
            m_CmdList->RSSetViewports(1,&vp);m_CmdList->RSSetScissorRects(1,&rect);m_CmdList->OMSetRenderTargets(1,&target.rtv,FALSE,nullptr);m_CmdList->SetPipelineState(m_FogCompositePso.Get());
        }
        m_CmdList->DrawInstanced(3,1,0,0);
        if(m_FogTime){m_CmdList->EndQuery(m_FogTime.Get(),D3D12_QUERY_TYPE_TIMESTAMP,query+1);m_CmdList->ResolveQueryData(m_FogTime.Get(),D3D12_QUERY_TYPE_TIMESTAMP,query,2,f.readback.Get(),0);f.pending=true;}
        auto after=CD3DX12_RESOURCE_BARRIER::Transition(depth,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);m_CmdList->ResourceBarrier(1,&after);
        // The new scene target remains RTV for editor grid; old scene remains SRV.
        std::swap(source,target);auto dsv=m_ViewportTarget.rendering?m_ViewportTarget.dsv:m_DsvHeap->GetCPUDescriptorHandleForHeapStart();m_CmdList->OMSetRenderTargets(1,&source.rtv,FALSE,&dsv);
        m_FogDiagnostics.active=true;m_FogDiagnostics.samples=s.mode==FogMode::Volumetric?FogSamples(s.quality):0;m_FogDiagnostics.localLights=std::min(lights.count,64u);m_FogDiagnostics.gpuMs=m_FogGpuMs;
        m_TransparentFogPass=pass;
        m_FogDiagnostics.gpuBytes=0;for(auto& pages:m_FogPasses)for(auto& page:pages)m_FogDiagnostics.gpuBytes+=page.target.allocationBytes+page.volume.allocationBytes+512;
    }catch(const std::exception& error){m_FogDiagnostics.error=error.what();Logger::Get().Error("Fog pass: "+m_FogDiagnostics.error);}
    m_FogDiagnostics.cpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
void Dx12RenderAdapter::CollectFogTiming(UINT slot){
    double total=0;bool ready=false;for(auto& pass:m_FogPasses[slot])if(pass.pending){void* mapped=nullptr;D3D12_RANGE read{0,16};if(SUCCEEDED(pass.readback->Map(0,&read,&mapped))){auto* ticks=static_cast<UINT64*>(mapped);if(m_TimeFrequency)total+=(ticks[1]-ticks[0])*1000.0/m_TimeFrequency;D3D12_RANGE empty{0,0};pass.readback->Unmap(0,&empty);ready=true;}pass.pending=false;}if(ready)m_FogGpuMs=total;
}
void Dx12RenderAdapter::ReleaseFog(){for(auto& pages:m_FogPasses)pages.clear();m_FogPso.Reset();m_FogVolumePso.Reset();m_FogCompositePso.Reset();m_FogRoot.Reset();m_FogTime.Reset();m_Fog={};m_FogDiagnostics={true};m_FogDiagnostics.error.clear();}
#endif
