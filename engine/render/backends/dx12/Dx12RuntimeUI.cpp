#define NOMINMAX
#include "Dx12RenderAdapter.h"
#ifdef _WIN32
#include "../../../ui/RuntimeUI.h"
#include "../../../external/d3dx12.h"
#include <d3dcompiler.h>
#include <cmath>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
namespace {void Check(HRESULT h,const char* text){if(FAILED(h))throw std::runtime_error(text);}}
void Dx12RenderAdapter::DrawRuntimeUI(const ui::DrawList& draw){
 if(draw.vertices.empty()||draw.width<1||draw.height<1)return;
 if(!m_RuntimeUIPso){
  const char* source=R"(
Texture2D<float4> Image:register(t0);SamplerState ImageSampler:register(s0);cbuffer Canvas:register(b0){float2 Size;float2 Padding;};
struct Vertex{float2 position:POSITION;float2 uv:TEXCOORD;float4 color:COLOR;};struct Pixel{float4 position:SV_Position;float2 uv:TEXCOORD;float4 color:COLOR;};
Pixel VS(Vertex v){Pixel p;p.position=float4(v.position/Size*float2(2,-2)+float2(-1,1),0,1);p.uv=v.uv;p.color=v.color;return p;}
float4 PS(Pixel p):SV_Target{return Image.Sample(ImageSampler,p.uv)*p.color;}
)";
  auto compile=[&](const char* entry,const char* profile){ComPtr<ID3DBlob> blob,error;auto result=D3DCompile(source,std::strlen(source),"Runtime UI",nullptr,nullptr,entry,profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error);if(FAILED(result))throw std::runtime_error(error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"Runtime UI shader failed");return blob;};auto vs=compile("VS","vs_5_1"),ps=compile("PS","ps_5_1");
  CD3DX12_DESCRIPTOR_RANGE range;range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0);CD3DX12_ROOT_PARAMETER roots[2];roots[0].InitAsConstants(4,0);roots[1].InitAsDescriptorTable(1,&range,D3D12_SHADER_VISIBILITY_PIXEL);CD3DX12_STATIC_SAMPLER_DESC sampler(0,D3D12_FILTER_MIN_MAG_MIP_LINEAR,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP);CD3DX12_ROOT_SIGNATURE_DESC root(2,roots,1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);ComPtr<ID3DBlob> blob,error;Check(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"Runtime UI root serialization failed");Check(m_Device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&m_RuntimeUIRoot)),"Runtime UI root failed");
  D3D12_INPUT_ELEMENT_DESC layout[]={{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
  D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};p.pRootSignature=m_RuntimeUIRoot.Get();p.VS={vs->GetBufferPointer(),vs->GetBufferSize()};p.PS={ps->GetBufferPointer(),ps->GetBufferSize()};p.InputLayout={layout,3};p.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;p.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);auto& blend=p.BlendState.RenderTarget[0];blend.BlendEnable=TRUE;blend.SrcBlend=D3D12_BLEND_SRC_ALPHA;blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;blend.BlendOp=D3D12_BLEND_OP_ADD;blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;p.DepthStencilState.DepthEnable=FALSE;p.DepthStencilState.StencilEnable=FALSE;p.SampleMask=UINT_MAX;p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;p.NumRenderTargets=1;p.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;p.SampleDesc.Count=1;Check(m_Device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&m_RuntimeUIPso)),"Runtime UI PSO failed");
 }
 const UINT64 bytes=draw.vertices.size()*sizeof(ui::Vertex);auto& buffer=m_RuntimeUIVertices[m_FrameIndex];auto& capacity=m_RuntimeUICapacity[m_FrameIndex];if(capacity<bytes){if(buffer)m_RetiredResources[m_FrameIndex].emplace_back(buffer.Get());buffer.Reset();capacity=65536;while(capacity<bytes)capacity*=2;auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);auto desc=CD3DX12_RESOURCE_DESC::Buffer(capacity);Check(m_Device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&buffer)),"Runtime UI vertex allocation failed");}
 void* mapped=nullptr;D3D12_RANGE empty{0,0};Check(buffer->Map(0,&empty,&mapped),"Runtime UI vertex mapping failed");std::memcpy(mapped,draw.vertices.data(),size_t(bytes));buffer->Unmap(0,nullptr);
 auto rtv=m_ViewportTarget.rendering?m_ViewportTarget.rtv:m_RtvHeap->GetCPUDescriptorHandleForHeapStart();if(!m_ViewportTarget.rendering)rtv.ptr+=SIZE_T(m_FrameIndex)*m_RtvSize;m_CmdList->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
 D3D12_VIEWPORT viewport{0,0,draw.width,draw.height,0,1};m_CmdList->RSSetViewports(1,&viewport);ID3D12DescriptorHeap* heaps[]{m_SrvHeap.Get()};m_CmdList->SetDescriptorHeaps(1,heaps);m_CmdList->SetGraphicsRootSignature(m_RuntimeUIRoot.Get());m_CmdList->SetPipelineState(m_RuntimeUIPso.Get());float size[]{draw.width,draw.height,0,0};m_CmdList->SetGraphicsRoot32BitConstants(0,4,size,0);D3D12_VERTEX_BUFFER_VIEW view{buffer->GetGPUVirtualAddress(),UINT(bytes),sizeof(ui::Vertex)};m_CmdList->IASetVertexBuffers(0,1,&view);m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 for(auto& command:draw.commands){auto texture=m_UploadedTextures.find(command.texture.value);if(texture==m_UploadedTextures.end()||command.first+command.count>draw.vertices.size())continue;D3D12_RECT clip{LONG(std::max(0.f,std::floor(command.clip.x))),LONG(std::max(0.f,std::floor(command.clip.y))),LONG(std::min(draw.width,std::ceil(command.clip.x+command.clip.w))),LONG(std::min(draw.height,std::ceil(command.clip.y+command.clip.h)))};if(clip.right<=clip.left||clip.bottom<=clip.top)continue;m_CmdList->RSSetScissorRects(1,&clip);m_CmdList->SetGraphicsRootDescriptorTable(1,texture->second.gpuSrvHandle);m_CmdList->DrawInstanced(command.count,1,command.first,0);}
 D3D12_RECT full{0,0,LONG(draw.width),LONG(draw.height)};m_CmdList->RSSetScissorRects(1,&full);
}
#endif
