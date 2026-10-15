#define NOMINMAX
#include "Dx12RenderAdapter.h"
#ifdef _WIN32
#include "../../../core/AssetPaths.h"
#include "../../../core/Logger.h"
#include "../../../external/d3dx12.h"
#include "../../../resources/EnvironmentResource.h"
#include <d3dcompiler.h>
#include <chrono>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
namespace {
void Check(HRESULT result,const char* message){if(FAILED(result))throw std::runtime_error(message);}
}
bool Dx12RenderAdapter::EnsureEditorVisualPipeline(){
    if(m_EditorVisualRoot)return true;if(m_EditorVisualAttempted)return false;m_EditorVisualAttempted=true;
    try{
        auto path=AssetPaths::ResolveShaderPath("dx12/editor_visuals.hlsl");
        auto compile=[&](const char* entry,const char* target){ComPtr<ID3DBlob> code,error;HRESULT result=D3DCompileFromFile(path.c_str(),nullptr,nullptr,entry,target,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);if(FAILED(result))throw std::runtime_error(error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"Editor visual shader unavailable");return code;};
        auto vs=compile("VS","vs_5_1"),sky=compile("SkyPS","ps_5_1"),grid=compile("GridPS","ps_5_1");
        CD3DX12_DESCRIPTOR_RANGE range;range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0);
        CD3DX12_ROOT_PARAMETER roots[2];roots[0].InitAsConstants(60,0);roots[1].InitAsDescriptorTable(1,&range,D3D12_SHADER_VISIBILITY_PIXEL);
        CD3DX12_STATIC_SAMPLER_DESC sampler(0,D3D12_FILTER_MIN_MAG_MIP_LINEAR,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        CD3DX12_ROOT_SIGNATURE_DESC root(2,roots,1,&sampler);ComPtr<ID3DBlob> serialized,error;
        Check(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error),"Editor visual root serialization failed");
        ComPtr<ID3D12RootSignature> readyRoot;Check(m_Device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&readyRoot)),"Editor visual root allocation failed");
        for(int hdr=0;hdr<2;++hdr)for(int isGrid=0;isGrid<2;++isGrid){
            D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};p.pRootSignature=readyRoot.Get();p.VS={vs->GetBufferPointer(),vs->GetBufferSize()};auto& ps=isGrid?grid:sky;p.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
            p.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
            p.DepthStencilState=CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);p.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;p.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
            p.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            if(isGrid){auto& b=p.BlendState.RenderTarget[0];b.BlendEnable=TRUE;b.SrcBlend=D3D12_BLEND_SRC_ALPHA;b.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;b.BlendOp=D3D12_BLEND_OP_ADD;b.SrcBlendAlpha=D3D12_BLEND_ONE;b.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;b.BlendOpAlpha=D3D12_BLEND_OP_ADD;}
            p.SampleMask=UINT_MAX;p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;p.NumRenderTargets=1;p.RTVFormats[0]=hdr?DXGI_FORMAT_R32G32B32A32_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;p.DSVFormat=DXGI_FORMAT_D32_FLOAT;p.SampleDesc.Count=1;
            Check(m_Device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&m_EditorVisualPso[hdr][isGrid])),"Editor visual PSO allocation failed");
        }
        m_EditorVisualRoot=std::move(readyRoot);return true;
    }catch(const std::exception& error){m_EditorVisualError=error.what();Logger::Get().Error("Editor visuals disabled: "+m_EditorVisualError);for(auto& row:m_EditorVisualPso)for(auto& p:row)p.Reset();return false;}
}
bool Dx12RenderAdapter::UploadEditorSky(const EnvironmentResource& resource,std::uint64_t version){
    if(!m_FrameRecording)return false;
    if(m_EditorSky&&m_EditorSkyKey==resource.sourcePath&&m_EditorSkyVersion==version)return true;
    try{
        const auto& cube=resource.sky.resolution?resource.sky:resource.environment;if(!cube.resolution||cube.resolution>1024||!cube.mipCount||cube.subresources.size()!=6*cube.mipCount)throw std::runtime_error("Invalid editor cubemap");
        ComPtr<ID3D12Resource> ready,upload;ComPtr<ID3D12DescriptorHeap> heap;
        auto desc=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT,cube.resolution,cube.resolution,6,1);
        auto props=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        Check(m_Device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&ready)),"Editor cubemap allocation failed");
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[6];UINT rows[6];UINT64 bytesPerRow[6],total=0;m_Device->GetCopyableFootprints(&desc,0,6,0,footprints,rows,bytesPerRow,&total);
        props=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);auto buffer=CD3DX12_RESOURCE_DESC::Buffer(total);
        Check(m_Device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)),"Editor cubemap upload allocation failed");
        // Validate the entire payload before mapping/recording commands.
        for(unsigned face=0;face<6;++face){const auto& data=cube.subresources[face*cube.mipCount];if(data.size()!=std::size_t(cube.resolution)*cube.resolution*4)throw std::runtime_error("Invalid editor cubemap face");for(float v:data)if(!std::isfinite(v)||v<0)throw std::runtime_error("Invalid editor cubemap radiance");}
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=1;Check(m_Device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"Editor cubemap SRV allocation failed");
        void* mapped=nullptr;CD3DX12_RANGE empty(0,0);Check(upload->Map(0,&empty,&mapped),"Editor cubemap map failed");
        for(unsigned face=0;face<6;++face){const auto& data=cube.subresources[face*cube.mipCount];for(unsigned y=0;y<rows[face];++y)std::memcpy(static_cast<char*>(mapped)+footprints[face].Offset+y*footprints[face].Footprint.RowPitch,data.data()+y*cube.resolution*4,std::size_t(bytesPerRow[face]));}
        upload->Unmap(0,nullptr);
        for(unsigned face=0;face<6;++face){CD3DX12_TEXTURE_COPY_LOCATION dst(ready.Get(),face),src(upload.Get(),footprints[face]);m_CmdList->CopyTextureRegion(&dst,0,0,0,&src,nullptr);}
        auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(ready.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);m_CmdList->ResourceBarrier(1,&barrier);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=desc.Format;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURECUBE;srv.TextureCube.MipLevels=1;m_Device->CreateShaderResourceView(ready.Get(),&srv,heap->GetCPUDescriptorHandleForHeapStart());
        if(m_EditorSky)m_RetiredResources[m_FrameIndex].emplace_back(m_EditorSky.Get());if(m_EditorSkySrv)m_RetiredResources[m_FrameIndex].emplace_back(m_EditorSkySrv.Get());m_RetiredResources[m_FrameIndex].emplace_back(upload.Get());
        m_EditorSky=std::move(ready);m_EditorSkySrv=std::move(heap);m_EditorSkyKey=resource.sourcePath;m_EditorSkyVersion=version;return true;
    }catch(const std::exception& error){m_EditorVisualDiagnostics.error=error.what();Logger::Get().Warn("Editor sky upload: "+m_EditorVisualDiagnostics.error);return false;}
}
void Dx12RenderAdapter::DrawEditorVisual(bool grid){
    const auto& f=m_EditorVisualFrame;const auto& s=f.settings;
    if((grid&&!m_ViewportTarget.rendering)||!f.active||(!grid&&!f.backgroundEnabled)||(grid&&!EditorGridVisible(f)))return;
    if(!ValidEditorVisualSettings(s)||!std::isfinite(f.aspect)||f.aspect<=0||f.nearPlane<=0||f.farPlane<=f.nearPlane)return;
    const auto start=std::chrono::steady_clock::now();
    if(!EnsureEditorVisualPipeline()){m_EditorVisualDiagnostics.error=m_EditorVisualError;return;}
    if(m_MaterialTableCount>=MaterialTablesPerFrame)return;
    UINT first=MaxTextures+(m_FrameIndex*MaterialTablesPerFrame+m_MaterialTableCount++)*4;
    auto cpu=m_SrvHeap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(first)*m_SrvDescriptorSize;
    bool skyReady=m_EditorSky&&m_EditorSkyKey==s.cubemap;
    if(skyReady)m_Device->CopyDescriptorsSimple(1,cpu,m_EditorSkySrv->GetCPUDescriptorHandleForHeapStart(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    else {D3D12_SHADER_RESOURCE_VIEW_DESC null{};null.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;null.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;null.ViewDimension=D3D12_SRV_DIMENSION_TEXTURECUBE;null.TextureCube.MipLevels=1;m_Device->CreateShaderResourceView(nullptr,&null,cpu);}
    std::array<float,60> constants{};auto put=[&](int slot,const auto& v,float w){for(int i=0;i<3;++i)constants[slot*4+i]=v[i];constants[slot*4+3]=w;};
    put(0,f.camera,0);put(1,f.right,f.tanHalfFov*f.aspect);put(2,f.up,f.tanHalfFov);put(3,f.forward,f.nearPlane);
    constants[16]=f.farPlane;constants[17]=float(m_ViewportTarget.rendering?m_ViewportTarget.width:m_Width);constants[18]=float(m_ViewportTarget.rendering?m_ViewportTarget.height:m_Height);constants[19]=m_HdrActive?1.f:0.f;
    constants[20]=s.minorSpacing;constants[21]=s.majorSpacing;constants[22]=s.gridOpacity;constants[23]=s.fadeDistance;
    put(6,s.top,m_HdrNative?1.f:0.f);put(7,s.horizon,0);put(8,s.bottom,0);
    constants[36]=s.background==EditorBackground::Cubemap&&!skyReady?1.f:float(s.background);constants[37]=s.skyIntensity;constants[38]=s.gradientBlend;constants[39]=s.skyRotation*.01745329252f;put(10,s.solid,0);put(11,f.sunDirection,f.sunVisible?std::cos(s.sunRadius*.5f*.01745329252f):2.f);put(12,f.sunColor,s.sunIntensity);put(13,s.nightColor,s.nightIntensity);constants[56]=s.horizonGlow;constants[57]=s.sunsetWarmth;constants[58]=s.sunGlowRadius*.01745329252f;constants[59]=s.sunGlowIntensity;
    m_CmdList->SetGraphicsRootSignature(m_EditorVisualRoot.Get());m_CmdList->SetPipelineState(m_EditorVisualPso[m_HdrActive?1:0][grid?1:0].Get());
    ID3D12DescriptorHeap* heaps[]{m_SrvHeap.Get()};m_CmdList->SetDescriptorHeaps(1,heaps);m_CmdList->SetGraphicsRoot32BitConstants(0,60,constants.data(),0);
    auto gpu=m_SrvHeap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=UINT64(first)*m_SrvDescriptorSize;m_CmdList->SetGraphicsRootDescriptorTable(1,gpu);
    m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);m_CmdList->DrawInstanced(3,1,0,0);
    if(grid)++m_EditorVisualDiagnostics.gridDraws;else ++m_EditorVisualDiagnostics.backgroundDraws;m_EditorVisualDiagnostics.skyReady=skyReady;
    m_EditorVisualDiagnostics.cpuMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
void Dx12RenderAdapter::ReleaseEditorVisuals(){
    m_EditorSky.Reset();m_EditorSkySrv.Reset();m_EditorSkyKey.clear();m_EditorSkyVersion=0;m_EditorVisualRoot.Reset();for(auto& row:m_EditorVisualPso)for(auto& p:row)p.Reset();m_EditorVisualAttempted=false;m_EditorVisualError.clear();
}
#endif
