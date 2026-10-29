#include "Application.h"
#include "../platform/GlfwWindow.h"
#include <imgui_internal.h>
#include <GLFW/glfw3.h>
#include <cmath>
GLFWwindow* Application::EditorViewportNativeWindow()const{
    if(auto* handle=m_EditorLayer.ViewportPlatformHandle())return static_cast<GLFWwindow*>(handle);
    auto* main=dynamic_cast<const GlfwWindow*>(GetWindow());return main?main->GetGlfwHandle():nullptr;
}
GLFWwindow* Application::GameplayNativeWindow()const{
    if(auto* game=dynamic_cast<GlfwWindow*>(m_GameWindow.get()))return game->GetGlfwHandle();
    return EditorViewportNativeWindow();
}
static bool DesktopCoordinates(){return ImGui::GetCurrentContext()&&(ImGui::GetIO().ConfigFlags&ImGuiConfigFlags_ViewportsEnable);}
std::array<double,2> Application::EditorPointerPosition()const{
    std::array<double,2> p{};auto* window=EditorViewportNativeWindow();if(!window)return p;
    if(!DesktopCoordinates()){if(auto* main=dynamic_cast<const GlfwWindow*>(GetWindow()))return main->PointerPosition();}
    // Picking/widgets already consume ImGui desktop positions. Camera entry and
    // PIE UI must use that same event stream, without adding the OS origin twice.
    // Captured navigation still needs GLFW's virtual client deltas.
    if(DesktopCoordinates()&&!m_Camera.controlsActive&&!m_PlayerMouseCaptured){
        auto* context=ImGui::GetCurrentContext();
        for(int i=context->InputEventsQueue.Size-1;i>=0;--i)if(context->InputEventsQueue[i].Type==ImGuiInputEventType_MousePos){ImVec2 pos{context->InputEventsQueue[i].MousePos.PosX,context->InputEventsQueue[i].MousePos.PosY};if(ImGui::IsMousePosValid(&pos))return {pos.x,pos.y};}
        auto pos=ImGui::GetIO().MousePos;if(ImGui::IsMousePosValid(&pos))return {pos.x,pos.y};
    }
    glfwGetCursorPos(window,&p[0],&p[1]);int x=0,y=0;if(DesktopCoordinates())glfwGetWindowPos(window,&x,&y);p[0]+=x;p[1]+=y;return p;
}
std::array<double,2> Application::GameplayPointerPosition()const{
    if(auto* game=dynamic_cast<GlfwWindow*>(m_GameWindow.get()))return game->PointerPosition();
    return EditorPointerPosition();
}
void Application::ReleaseEditorCameraCapture(){
    if(m_Camera.controlsActive&&m_Camera.captureWindow){auto* window=m_Camera.captureWindow;glfwSetInputMode(window,GLFW_CURSOR,GLFW_CURSOR_NORMAL);if(glfwRawMouseMotionSupported())glfwSetInputMode(window,GLFW_RAW_MOUSE_MOTION,GLFW_FALSE);glfwSetCursorPos(window,m_Camera.cursorXBeforeCapture,m_Camera.cursorYBeforeCapture);}
    m_Camera.controlsActive=false;m_Camera.captureWindow=nullptr;m_Camera.editorGesture=0;
    if(m_PlayerMouseCaptured&&!IsStandalonePlay())ReleasePlayerMouse();
}
double Application::EditorScrollDelta(){
    if(!DesktopCoordinates()){auto* main=dynamic_cast<GlfwWindow*>(GetWindow());return main?main->ConsumeScrollDeltaY():0;}
    double result=0;for(const auto& event:ImGui::GetCurrentContext()->InputEventsQueue)if(event.Type==ImGuiInputEventType_MouseWheel&&event.EventId>m_Camera.lastWheelEvent){result+=event.MouseWheel.WheelY;m_Camera.lastWheelEvent=event.EventId;}
    return result;
}
bool Application::UpdateEditorCameraGestures(float){
    if(IsEditorPlayMode())return false;
    auto* window=EditorViewportNativeWindow();if(!window)return false;
    const bool focused=glfwGetWindowAttrib(window,GLFW_FOCUSED)!=0;
    const bool pan=focused&&glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_MIDDLE)==GLFW_PRESS;
    const bool orbit=focused&&glfwGetKey(window,GLFW_KEY_LEFT_ALT)==GLFW_PRESS&&glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;
    if(m_Camera.controlsActive&&m_Camera.editorGesture==0)return false;
    auto pointer=EditorPointerPosition();const bool over=m_EditorLayer.IsPointInsideViewport(pointer[0],pointer[1])&&m_EditorLayer.IsViewportHovered()&&!m_EditorLayer.BlocksCamera();
    auto forward=[&]{float c=std::cos(m_Camera.pitch);return ecs::Vec3{std::sin(m_Camera.yaw)*c,std::sin(m_Camera.pitch),std::cos(m_Camera.yaw)*c};};
    if(!m_Camera.controlsActive){
        if(!over||!focused||glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_RIGHT)==GLFW_PRESS)return false;
        double wheel=EditorScrollDelta();if(wheel){auto f=forward();float amount=float(wheel)*m_Camera.moveSpeed*.3f;m_Camera.position.x+=f.x*amount;m_Camera.position.y+=f.y*amount;m_Camera.position.z+=f.z*amount;}
        if(!pan&&!orbit)return false;
        m_Camera.editorGesture=pan?1:2;m_Camera.captureWindow=window;m_Camera.controlsActive=true;
        glfwGetCursorPos(window,&m_Camera.cursorXBeforeCapture,&m_Camera.cursorYBeforeCapture);int w=0,h=0;glfwGetWindowSize(window,&w,&h);m_Camera.lastMouseX=w*.5;m_Camera.lastMouseY=h*.5;
        glfwSetInputMode(window,GLFW_CURSOR,GLFW_CURSOR_DISABLED);glfwSetCursorPos(window,m_Camera.lastMouseX,m_Camera.lastMouseY);
        auto f=forward();m_Camera.orbitTarget={m_Camera.position.x+f.x*5,m_Camera.position.y+f.y*5,m_Camera.position.z+f.z*5};
        if(auto e=m_EditorLayer.GetSelectedEntity();GetWorld().IsAlive(e))m_Camera.orbitTarget=GetWorld().WorldTransform(e).position;
        auto t=m_Camera.orbitTarget;float x=t.x-m_Camera.position.x,y=t.y-m_Camera.position.y,z=t.z-m_Camera.position.z;m_Camera.orbitDistance=std::max(.1f,std::sqrt(x*x+y*y+z*z));
        return true;
    }
    if(!focused||m_Camera.captureWindow!=window||(m_Camera.editorGesture==1?!pan:!orbit)){ReleaseEditorCameraCapture();return true;}
    double x=0,y=0;glfwGetCursorPos(window,&x,&y);float dx=float(x-m_Camera.lastMouseX),dy=float(y-m_Camera.lastMouseY);m_Camera.lastMouseX=x;m_Camera.lastMouseY=y;
    if(m_Camera.editorGesture==1){float scale=m_Camera.moveSpeed*.003f;m_Camera.position.x-=std::cos(m_Camera.yaw)*dx*scale;m_Camera.position.z+=std::sin(m_Camera.yaw)*dx*scale;m_Camera.position.y+=dy*scale;}
    else {m_Camera.yaw+=dx*m_Camera.mouseSensitivity;m_Camera.pitch=std::clamp(m_Camera.pitch-dy*m_Camera.mouseSensitivity,-1.55334f,1.55334f);auto f=forward();m_Camera.position={m_Camera.orbitTarget.x-f.x*m_Camera.orbitDistance,m_Camera.orbitTarget.y-f.y*m_Camera.orbitDistance,m_Camera.orbitTarget.z-f.z*m_Camera.orbitDistance};}
    return true;
}
