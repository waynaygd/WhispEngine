#define NOMINMAX
#include "Dx12RenderAdapter.h"
#ifdef _WIN32
#include "../../../external/d3dx12.h"
#include <stdexcept>

bool Dx12RenderAdapter::BeginViewmodelPass(){
 if(!m_FrameRecording)return false;
 const UINT width=m_ViewportTarget.rendering?m_ViewportTarget.width:m_Width;
 const UINT height=m_ViewportTarget.rendering?m_ViewportTarget.height:m_Height;
 auto& target=m_ViewmodelDepth[m_FrameIndex];
 if(target.width!=width||target.height!=height){
  if(target.depth)m_RetiredResources[m_FrameIndex].emplace_back(target.depth.Get());
  if(target.heap)m_RetiredResources[m_FrameIndex].emplace_back(target.heap.Get());
  target={};auto check=[](HRESULT result){if(FAILED(result))throw std::runtime_error("Viewmodel depth allocation failed: "+std::to_string(result));};
  D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;heap.NumDescriptors=1;
  check(m_Device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&target.heap)));
  auto properties=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
  auto description=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT,width,height,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
  D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT;clear.DepthStencil.Depth=1;
  check(m_Device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&description,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&target.depth)));
  m_Device->CreateDepthStencilView(target.depth.Get(),nullptr,target.heap->GetCPUDescriptorHandleForHeapStart());target.width=width;target.height=height;
 }
 // World fog/selection use the untouched World depth before the overlay. HDR
 // output, AA and screen effects still process the complete color image once.
 BeginWorldTransparency();m_InViewmodelPass=true;
 auto color=m_HdrActive?m_HdrTargets[m_FrameIndex].rtv:m_ViewportTarget.rendering?m_ViewportTarget.rtv:m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
 if(!m_HdrActive&&!m_ViewportTarget.rendering)color.ptr+=SIZE_T(m_FrameIndex)*m_RtvSize;
 auto depth=target.heap->GetCPUDescriptorHandleForHeapStart();m_CmdList->ClearDepthStencilView(depth,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);m_CmdList->OMSetRenderTargets(1,&color,FALSE,&depth);
 return true;
}
void Dx12RenderAdapter::EndViewmodelPass(){
 m_InViewmodelPass=false;
 auto color=m_HdrActive?m_HdrTargets[m_FrameIndex].rtv:m_ViewportTarget.rendering?m_ViewportTarget.rtv:m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
 if(!m_HdrActive&&!m_ViewportTarget.rendering)color.ptr+=SIZE_T(m_FrameIndex)*m_RtvSize;
 auto depth=m_ViewportTarget.rendering?m_ViewportTarget.dsv:m_DsvHeap->GetCPUDescriptorHandleForHeapStart();m_CmdList->OMSetRenderTargets(1,&color,FALSE,&depth);
}
#endif
