#include "Dx12RenderAdapter.h"
#ifdef _WIN32
#include "../../../platform/IWindow.h"
#include "../../../core/Logger.h"
#include <stdexcept>
#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
namespace {
void CheckOutput(HRESULT hr,const char* what){if(FAILED(hr))throw std::runtime_error(what);}
D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};return b;
}
}
bool Dx12RenderAdapter::OpenGameOutput(IWindow* window){
    if(!window||!m_Device||m_FrameRecording||m_GameSwapchain)return false;
    try{
        DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=1280;desc.Height=720;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;
        desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;desc.Scaling=DXGI_SCALING_STRETCH;
        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain;
        auto hwnd=static_cast<HWND>(window->GetNativeHandle());
        CheckOutput(m_Factory->CreateSwapChainForHwnd(m_Queue.Get(),hwnd,&desc,nullptr,nullptr,&swapchain),"Game swapchain creation failed");
        CheckOutput(m_Factory->MakeWindowAssociation(hwnd,DXGI_MWA_NO_ALT_ENTER),"Game window association failed");
        CheckOutput(swapchain.As(&m_GameSwapchain),"Game swapchain interface failed");
        m_GameSize={1280,720};
        for(unsigned i=0;i<2;++i)CheckOutput(m_GameSwapchain->GetBuffer(i,IID_PPV_ARGS(&m_GameBuffers[i])),"Game backbuffer acquisition failed");
        m_GameVisible=true;return true;
    }catch(const std::exception& e){Logger::Get().Error(e.what());CloseGameOutput();return false;}
}
void Dx12RenderAdapter::CloseGameOutput(){
    if(m_GameSwapchain&&m_Device)try{WaitForGpu();}catch(...){Logger::Get().Warn("Game output fence failed during teardown");}
    for(auto& b:m_GameBuffers)b.Reset();m_GameSwapchain.Reset();m_GameSize={};m_GameVisible=m_GameCopied=false;
}
bool Dx12RenderAdapter::ResizeGameOutput(unsigned width,unsigned height){
    if(!m_GameSwapchain||m_FrameRecording)return false;
    m_GameVisible=width>0&&height>0;if(!m_GameVisible)return true;
    if(m_GameSize==std::array<unsigned,2>{width,height})return true;
    try{
        WaitForGpu();for(auto& b:m_GameBuffers)b.Reset();m_GameCopied=false;
        CheckOutput(m_GameSwapchain->ResizeBuffers(2,width,height,DXGI_FORMAT_R8G8B8A8_UNORM,0),"Game ResizeBuffers failed");
        for(unsigned i=0;i<2;++i)CheckOutput(m_GameSwapchain->GetBuffer(i,IID_PPV_ARGS(&m_GameBuffers[i])),"Game resized buffer failed");
        m_GameSize={width,height};return true;
    }catch(const std::exception& e){Logger::Get().Error(e.what());CloseGameOutput();return false;}
}
void Dx12RenderAdapter::CopyGameOutput(){
    m_GameCopied=false;
    if(!m_GameSwapchain||!m_GameVisible||!m_ViewportTarget.color||m_ViewportTarget.rendering)return;
    if(m_ViewportTarget.width!=m_GameSize[0]||m_ViewportTarget.height!=m_GameSize[1])return;
    m_GameLastBuffer=m_GameSwapchain->GetCurrentBackBufferIndex();auto* target=m_GameBuffers[m_GameLastBuffer].Get();
    D3D12_RESOURCE_BARRIER barriers[]={Transition(m_ViewportTarget.color.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE),Transition(target,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_DEST)};
    m_CmdList->ResourceBarrier(2,barriers);m_CmdList->CopyResource(target,m_ViewportTarget.color.Get());
    for(auto& b:barriers)std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
    m_CmdList->ResourceBarrier(2,barriers);m_GameCopied=true;
}
std::vector<std::uint8_t> Dx12RenderAdapter::ReadBackGameOutputPixels(){
    return m_GameCopied?ReadBackColorPixels(m_GameBuffers[m_GameLastBuffer].Get(),D3D12_RESOURCE_STATE_PRESENT):std::vector<std::uint8_t>{};
}
std::vector<std::uint8_t> Dx12RenderAdapter::ReadBackPlatformPixels(unsigned id){
    if(!ImGui::GetCurrentContext())return {};
    return ReadBackColorPixels(ImGui_ImplDX12_ViewportBackBuffer(ImGui::FindViewportByID(id)),D3D12_RESOURCE_STATE_PRESENT);
}
#endif
