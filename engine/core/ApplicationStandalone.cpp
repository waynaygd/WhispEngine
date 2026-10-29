#include "Application.h"
#include "../platform/GlfwWindow.h"
#include "Logger.h"
#include <GLFW/glfw3.h>

bool Application::RequestPlayInNewWindow(){
    if(IsEditorPlayMode()||IsPrefabEditing()||m_ProjectBrowserActive||m_RequestStandalone)return false;
    if(!SupportsPlayInNewWindow()){m_PlayError="Play In New Window is unavailable on this render backend";return false;}
    if(!GetProject()){m_PlayError="Play In New Window requires an open project and Game Startup settings";return false;}
    m_RequestStandalone=true;return true;
}
std::array<unsigned,2> Application::GameplayViewportSize()const{
    if(m_GameWindow)return m_Windows.front().renderer->GameOutputSize();
    return {unsigned(std::max(0,m_EditorLayer.GetViewportPixelWidth())),unsigned(std::max(0,m_EditorLayer.GetViewportPixelHeight()))};
}
void Application::CloseStandaloneWindow(){
    m_RequestStandalone=false;
    if(!m_GameWindow)return;
    ReleasePlayerMouse();
    if(auto* game=dynamic_cast<GlfwWindow*>(m_GameWindow.get())){
        int w=0,h=0;glfwGetWindowSize(game->GetGlfwHandle(),&w,&h);if(w>0&&h>0)SetStandaloneWindowSize(w,h);
    }
    // Finish GPU use BEFORE releasing HWND. No secondary command list or World.
    if(auto* renderer=GetRenderAdapter())renderer->CloseGameOutput();
    m_GameWindow.reset();
    if(auto* editor=dynamic_cast<GlfwWindow*>(GetWindow()))m_InputManager.SetWindow(editor->GetGlfwHandle());
    m_InputManager.ResetMouseDelta();m_PlayerAltWasDown=false;GetRuntimeUI().CancelPointer();
}
void Application::ServiceStandaloneWindow(){
    if(m_GameWindow){
        auto* game=dynamic_cast<GlfwWindow*>(m_GameWindow.get());int width=0,height=0;
        glfwGetFramebufferSize(game->GetGlfwHandle(),&width,&height);
        if(game->ShouldClose()||!GetRenderAdapter()->ResizeGameOutput(unsigned(width),unsigned(height))){SetEditorPlayMode(false);CloseStandaloneWindow();}
    }
    if(!m_RequestStandalone)return;
    m_RequestStandalone=false;
    auto window=std::make_unique<GlfwWindow>();
    if(!window->Create(m_GameWindowSize[0],m_GameWindowSize[1],"WhispEngine - Game")){m_PlayError="Could not create Game window";return;}
    if(!GetRenderAdapter()->OpenGameOutput(window.get())){m_PlayError="Could not create Game output swapchain";return;}
    m_GameWindow=std::move(window);
    auto* game=static_cast<GlfwWindow*>(m_GameWindow.get());m_InputManager.SetWindow(game->GetGlfwHandle());m_InputManager.ResetMouseDelta();
    int w=0,h=0;glfwGetFramebufferSize(game->GetGlfwHandle(),&w,&h);
    if(!GetRenderAdapter()->ResizeGameOutput(unsigned(w),unsigned(h))){CloseStandaloneWindow();m_PlayError="Could not resize Game output";return;}
    if(!PlayFromGameStartupMap()){CloseStandaloneWindow();return;}
    glfwFocusWindow(game->GetGlfwHandle());
}
