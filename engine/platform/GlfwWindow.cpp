#include "GlfwWindow.h"
#include "../core/Logger.h"
#include "branding/GeneratedIcon.h"

#include <GLFW/glfw3.h>
#include <stb_image.h>
#include <array>
#include <memory>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

static int g_GlfwRefCount = 0;

namespace
{
void SetApplicationIcon(GLFWwindow* window)
{
    // GLFW copies the RGBA pixels; no CPU images survive this creation step.
    struct FreePixels { void operator()(unsigned char* p)const { stbi_image_free(p); } };
    std::array<std::unique_ptr<unsigned char, FreePixels>, 7> pixels{};
    std::array<GLFWimage, 7> images{};
    int count=0;
    for(const auto& frame:whisp::branding::WindowIcons){
        int width=0,height=0,channels=0;
        auto* rgba=stbi_load_from_memory(frame.png,static_cast<int>(frame.bytes),&width,&height,&channels,STBI_rgb_alpha);
        if(!rgba){Logger::Get().Warn("Application icon could not be decoded");continue;}
        pixels[count].reset(rgba);images[count++]={width,height,rgba};
    }
    if(count)glfwSetWindowIcon(window,count,images.data());
}

void PointerCallback(GLFWwindow* window,double x,double y){if(auto* owner=static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window)))owner->PointerMoved(x,y);}
void PointerEnterCallback(GLFWwindow* window,int entered){if(!entered)if(auto* owner=static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window)))owner->PointerLeft();}
void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    (void)xoffset;

    auto* owner = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
    if (owner != nullptr)
        owner->AddScrollDeltaY(yoffset);
}
}

GlfwWindow::~GlfwWindow()
{
    if (m_Window)
    {
        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
    }

    if (m_HoldsGlfw)
    {
        --g_GlfwRefCount;
        if (g_GlfwRefCount == 0)
            glfwTerminate();
    }
}

bool GlfwWindow::Create(int width, int height, const std::string& title)
{
    if(m_Window||m_HoldsGlfw)return false;
    if (g_GlfwRefCount == 0)
    {
        if (!glfwInit())
        {
            Logger::Get().Error("glfwInit failed");
            return false;
        }
    }
    ++g_GlfwRefCount;
    m_HoldsGlfw=true;

    // ImGui's platform backend leaves hidden/undecorated creation hints set.
    // Engine-owned Editor/Game windows must not inherit those global hints.
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    m_Window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_Window)
    {
        Logger::Get().Error("glfwCreateWindow failed");

        --g_GlfwRefCount;
        m_HoldsGlfw=false;
        if (g_GlfwRefCount == 0)
            glfwTerminate();

        return false;
    }

    SetApplicationIcon(m_Window);
    glfwSetWindowUserPointer(m_Window, this);
    glfwSetScrollCallback(m_Window, ScrollCallback);
    glfwSetCursorPosCallback(m_Window,PointerCallback);glfwSetCursorEnterCallback(m_Window,PointerEnterCallback);

    return true;
}

void GlfwWindow::PollEvents()
{
    glfwPollEvents();
}

bool GlfwWindow::ShouldClose() const
{
    return m_Window && glfwWindowShouldClose(m_Window);
}

void* GlfwWindow::GetNativeHandle() const
{
#ifdef _WIN32
    return (void*)glfwGetWin32Window(m_Window);
#else
    return nullptr;
#endif
}

void GlfwWindow::SetTitle(const char* title)
{
    glfwSetWindowTitle(m_Window, title);
}

double GlfwWindow::ConsumeScrollDeltaY()
{
    const double delta = m_PendingScrollDeltaY;
    m_PendingScrollDeltaY = 0.0;
    return delta;
}

void GlfwWindow::AddScrollDeltaY(double delta)
{
    m_PendingScrollDeltaY += delta;
}


std::array<double,2> GlfwWindow::PointerPosition()const{
    if(m_HasPointer)return m_Pointer;std::array<double,2> p{};if(m_Window)glfwGetCursorPos(m_Window,&p[0],&p[1]);return p;
}
