#define NOMINMAX
#include "Dx12RenderAdapter.h"
#ifdef _WIN32
#include "../../../external/d3dx12.h"
#include "../../../core/Logger.h"
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <chrono>
#include <cstring>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
namespace {
void Check(HRESULT hr,const char* text){if(FAILED(hr))throw std::runtime_error(text);}
// Only this virtual include is visible to project shaders; no filesystem include access.
struct ContractInclude : ID3DInclude {
    std::string header;
    HRESULT __stdcall Open(D3D_INCLUDE_TYPE,LPCSTR name,LPCVOID,LPCVOID* data,UINT* bytes)override{
        if(std::strcmp(name,"WhispPostFX.hlsli"))return E_FAIL;*data=header.data();*bytes=UINT(header.size());return S_OK;
    }
    HRESULT __stdcall Close(LPCVOID)override{return S_OK;}
};
const char* contract=R"(
#ifndef WHISP_POSTFX_CONTRACT
#define WHISP_POSTFX_CONTRACT
Texture2D<float4> SceneColor:register(t0);SamplerState SceneSampler:register(s0);
cbuffer PostFxConstants:register(b0){float4 ScreenInfo;float4 EffectInfo;float4 Parameters[16];};
#define ScreenSize (ScreenInfo.xy)
#define Time (EffectInfo.x)
#define DeltaTime (EffectInfo.y)
#define EffectWeight (EffectInfo.z)
float4 SampleSceneColor(float2 uv){return SceneColor.SampleLevel(SceneSampler,clamp(uv,.5*ScreenInfo.zw,1-.5*ScreenInfo.zw),0);}
)";
const char* wrapper=R"(
struct WhispVertex {float4 position:SV_Position;float2 uv:TEXCOORD;};
WhispVertex WhispVS(uint id:SV_VertexID){WhispVertex v;v.uv=float2((id<<1)&2,id&2);v.position=float4(v.uv*float2(2,-2)+float2(-1,1),0,1);return v;}
float4 WhispPS(WhispVertex WhispInput):SV_Target0{
 // WHISP_PARAMETERS
 float4 WhispOriginal=SampleSceneColor(WhispInput.uv);float4 WhispResult=PostProcess(WhispInput.uv);
 if(!all(isfinite(WhispResult)))return WhispOriginal;
 return float4(saturate(lerp(WhispOriginal.rgb,WhispResult.rgb,EffectWeight)),WhispOriginal.a);
}
)";
}
std::string Dx12RenderAdapter::CustomPostFxStatus(const std::string& key)const{
 auto it=m_PostFxGpu.find(key);if(it==m_PostFxGpu.end())return "Not requested";const auto& gpu=it->second;
 if(!gpu.error.empty())return gpu.error+(gpu.pso?" (last working shader retained)":"");
 if(gpu.pending.valid())return "Compiling";return gpu.pso?"Ready":"Not requested";
}
std::vector<const PostFxPass*> Dx12RenderAdapter::PrepareCustomPostFx(){
 std::vector<const PostFxPass*> ready;
 if(!m_CustomPostFx.passes.empty()&&!m_PostFxRoot){
    CD3DX12_DESCRIPTOR_RANGE range;range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0);
    CD3DX12_ROOT_PARAMETER roots[2];roots[0].InitAsDescriptorTable(1,&range,D3D12_SHADER_VISIBILITY_PIXEL);roots[1].InitAsConstantBufferView(0,0,D3D12_SHADER_VISIBILITY_PIXEL);
    CD3DX12_STATIC_SAMPLER_DESC sampler(0,D3D12_FILTER_MIN_MAG_MIP_LINEAR,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    CD3DX12_ROOT_SIGNATURE_DESC root(2,roots,1,&sampler);ComPtr<ID3DBlob> blob,error;Check(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"PostFX root serialization failed");Check(m_Device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&m_PostFxRoot)),"PostFX root allocation failed");
 }
 for(auto& pass:m_CustomPostFx.passes){
    auto definition=pass.definition;if(!definition||pass.weight<=0)continue;
    auto& gpu=m_PostFxGpu[definition->key];
    if(gpu.pending.valid()&&gpu.pending.wait_for(std::chrono::seconds(0))==std::future_status::ready){
        auto compiled=gpu.pending.get();gpu.error=compiled.error;
        if(gpu.error.empty()){
            D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};p.pRootSignature=m_PostFxRoot.Get();p.VS={compiled.vs.data(),compiled.vs.size()};p.PS={compiled.ps.data(),compiled.ps.size()};p.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;p.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);p.DepthStencilState.DepthEnable=FALSE;p.DepthStencilState.StencilEnable=FALSE;p.SampleMask=UINT_MAX;p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;p.NumRenderTargets=1;p.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;p.SampleDesc.Count=1;
            ComPtr<ID3D12PipelineState> replacement;auto result=m_Device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&replacement));if(SUCCEEDED(result)){if(gpu.pso)m_RetiredResources[m_FrameIndex].emplace_back(gpu.pso.Get());gpu.pso=std::move(replacement);gpu.active=compiled.definition;}else gpu.error="Unsupported shader contract / PSO creation failed";
        }
        if(!gpu.error.empty())Logger::Get().Error("Custom PostFX "+definition->key+": "+gpu.error);
    }
    if(!gpu.pending.valid()&&gpu.requested!=definition){
        gpu.requested=definition;gpu.error.clear();
        gpu.pending=std::async(std::launch::async,[definition](){
            PostFxCompile result;result.definition=definition;
            try{
                ContractInclude includes;includes.header=contract;
                std::string assignments;
                for(size_t i=0;i<definition->parameters.size();++i){auto& p=definition->parameters[i];std::string swizzle=p.Components()==1?"x":p.Components()==2?"xy":p.Components()==3?"xyz":"xyzw";
                    includes.header+="\nstatic "+p.type+" "+p.name+";\n";
                    assignments+=p.name+"="+p.type+"(Parameters["+std::to_string(i)+"]."+swizzle+");\n";
                }
                includes.header+="\n#endif\n";
                // Typed per-invocation globals avoid preprocessor collisions with swizzles such as rgb.
                std::string entry=wrapper;auto at=entry.find("// WHISP_PARAMETERS");entry.replace(at,std::strlen("// WHISP_PARAMETERS"),assignments);
                std::string source=includes.header+"\n"+definition->source+"\n"+entry;
                auto compile=[&](const char* entry,const char* profile,std::vector<unsigned char>& bytes){
                    ComPtr<ID3DBlob> blob,error;HRESULT hr=D3DCompile(source.data(),source.size(),definition->shader.c_str(),nullptr,&includes,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error);
                    if(FAILED(hr))throw std::runtime_error(error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"Shader compile error");
                    if(std::strcmp(profile,"ps_5_1")==0){
                        ComPtr<ID3D12ShaderReflection> reflection;Check(D3DReflect(blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&reflection)),"Shader contract inspection failed");D3D12_SHADER_DESC desc{};reflection->GetDesc(&desc);
                        for(UINT i=0;i<desc.BoundResources;++i){D3D12_SHADER_INPUT_BIND_DESC binding{};reflection->GetResourceBindingDesc(i,&binding);bool allowed=binding.BindPoint==0&&binding.Space==0&&binding.BindCount==1&&((binding.Type==D3D_SIT_TEXTURE&&std::strcmp(binding.Name,"SceneColor")==0&&binding.Dimension==D3D_SRV_DIMENSION_TEXTURE2D)||(binding.Type==D3D_SIT_SAMPLER&&std::strcmp(binding.Name,"SceneSampler")==0)||(binding.Type==D3D_SIT_CBUFFER&&std::strcmp(binding.Name,"PostFxConstants")==0));if(!allowed)throw std::runtime_error("Unsupported shader contract: only SceneColor, SceneSampler and PostFxConstants are allowed");}
                    }
                    auto* begin=static_cast<const unsigned char*>(blob->GetBufferPointer());bytes.assign(begin,begin+blob->GetBufferSize());
                };
                compile("WhispVS","vs_5_1",result.vs);compile("WhispPS","ps_5_1",result.ps);
            }catch(const std::exception& e){result.error=e.what();}return result;
        });
    }
    if(gpu.pso&&gpu.active&&gpu.active->id==definition->id&&gpu.active->origin==definition->origin)ready.push_back(&pass);
 }
 // Do not retain GPU programs for assets removed from the current effect stack.
 for(auto it=m_PostFxGpu.begin();it!=m_PostFxGpu.end();){bool used=std::any_of(m_CustomPostFx.passes.begin(),m_CustomPostFx.passes.end(),[&](auto& p){return p.definition&&p.definition->key==it->first;});if(!used&&it->second.pending.valid()&&it->second.pending.wait_for(std::chrono::seconds(0))==std::future_status::ready)it->second.pending.get();if(!used&&!it->second.pending.valid()){if(it->second.pso)m_RetiredResources[m_FrameIndex].emplace_back(it->second.pso.Get());it=m_PostFxGpu.erase(it);}else ++it;}
 return ready;
}
void Dx12RenderAdapter::EnsurePostFxTarget(HdrTarget& target,UINT width,UINT height){
 if(target.width==width&&target.height==height)return;
 for(auto* old:{static_cast<IUnknown*>(target.color.Get()),static_cast<IUnknown*>(target.descriptors.Get()),static_cast<IUnknown*>(target.rtvs.Get())})if(old)m_RetiredResources[m_FrameIndex].emplace_back(old);target={};
 auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);auto desc=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM,width,height,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
 Check(m_Device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&target.color)),"PostFX target allocation failed");
 D3D12_DESCRIPTOR_HEAP_DESC descriptors{};descriptors.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;descriptors.NumDescriptors=1;Check(m_Device->CreateDescriptorHeap(&descriptors,IID_PPV_ARGS(&target.rtvs)),"PostFX RTV failed");descriptors.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;Check(m_Device->CreateDescriptorHeap(&descriptors,IID_PPV_ARGS(&target.descriptors)),"PostFX SRV failed");target.rtv=target.rtvs->GetCPUDescriptorHandleForHeapStart();m_Device->CreateRenderTargetView(target.color.Get(),nullptr,target.rtv);
 D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=desc.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;m_Device->CreateShaderResourceView(target.color.Get(),&srv,target.descriptors->GetCPUDescriptorHandleForHeapStart());target.width=width;target.height=height;target.allocationBytes=m_Device->GetResourceAllocationInfo(0,1,&desc).SizeInBytes;
}
void Dx12RenderAdapter::DrawCustomPostFx(const std::vector<const PostFxPass*>& passes,D3D12_CPU_DESCRIPTOR_HANDLE finalRtv){
 auto& constants=m_PostFxConstants[m_FrameIndex];if(!constants){auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);auto desc=CD3DX12_RESOURCE_DESC::Buffer(512*32);Check(m_Device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&constants)),"PostFX constants failed");}
 float* mapped=nullptr;D3D12_RANGE empty{0,0};Check(constants->Map(0,&empty,reinterpret_cast<void**>(&mapped)),"PostFX constants mapping failed");
 ShadowTimestamp(6);
 for(size_t index=0;index<passes.size();++index){
    auto& pass=*passes[index];auto& program=m_PostFxGpu.at(pass.definition->key);auto& source=m_PostFxTargets[m_FrameIndex][index%2];auto destination=finalRtv;
    HdrTarget* target=nullptr;if(index+1<passes.size()){target=&m_PostFxTargets[m_FrameIndex][(index+1)%2];EnsurePostFxTarget(*target,source.width,source.height);auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(target->color.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);m_CmdList->ResourceBarrier(1,&barrier);destination=target->rtv;}
    float values[72]{};values[0]=float(source.width);values[1]=float(source.height);values[2]=1.f/source.width;values[3]=1.f/source.height;values[4]=m_CustomPostFx.time;values[5]=m_CustomPostFx.delta;values[6]=std::clamp(pass.weight,0.f,1.f);
    // A failed schema reload uses the last working PSO AND its matching parameter layout.
    for(size_t i=0;i<program.active->parameters.size();++i){auto& p=program.active->parameters[i];auto it=pass.parameters.find(p.name);auto value=it!=pass.parameters.end()&&p.Valid(it->second)?it->second:p.initial;for(unsigned c=0;c<p.Components();++c)values[8+i*4+c]=p.type=="bool"?(value.get<bool>()?1.f:0.f):(p.Components()>1?value[c].get<float>():value.get<float>());}
    std::memcpy(reinterpret_cast<char*>(mapped)+index*512,values,sizeof(values));
    if(m_MaterialTableCount>=MaterialTablesPerFrame)break;
    UINT first=MaxTextures+(m_FrameIndex*MaterialTablesPerFrame+m_MaterialTableCount++)*4;auto cpu=m_SrvHeap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(first)*m_SrvDescriptorSize;m_Device->CopyDescriptorsSimple(1,cpu,source.descriptors->GetCPUDescriptorHandleForHeapStart(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);auto gpu=m_SrvHeap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=UINT64(first)*m_SrvDescriptorSize;
    m_CmdList->OMSetRenderTargets(1,&destination,FALSE,nullptr);m_CmdList->SetGraphicsRootSignature(m_PostFxRoot.Get());m_CmdList->SetPipelineState(program.pso.Get());m_CmdList->SetGraphicsRootDescriptorTable(0,gpu);m_CmdList->SetGraphicsRootConstantBufferView(1,constants->GetGPUVirtualAddress()+index*512);m_CmdList->DrawInstanced(3,1,0,0);++m_SubmissionStatistics.outputDrawCalls;
    if(target){auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(target->color.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);m_CmdList->ResourceBarrier(1,&barrier);}
 }
 constants->Unmap(0,nullptr);ShadowTimestamp(7);
}
void Dx12RenderAdapter::ReleaseCustomPostFx(){
 m_PostFxGpu.clear();m_CustomPostFx={};for(auto& frame:m_PostFxTargets)for(auto& target:frame)target={};for(auto& buffer:m_PostFxConstants)buffer.Reset();m_PostFxRoot.Reset();
}
#endif

