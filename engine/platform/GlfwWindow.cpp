#include "GlfwWindow.h"
#include "../core/Logger.h"

#include <GLFW/glfw3.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

static int g_GlfwRefCount = 0;

namespace
{
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

    if (g_GlfwRefCount > 0)
    {
        --g_GlfwRefCount;
        if (g_GlfwRefCount == 0)
            glfwTerminate();
    }
}

bool GlfwWindow::Create(int width, int height, const std::string& title)
{
    if (g_GlfwRefCount == 0)
    {
        if (!glfwInit())
        {
            Logger::Get().Error("glfwInit failed");
            return false;
        }
    }
    ++g_GlfwRefCount;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    m_Window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_Window)
    {
        Logger::Get().Error("glfwCreateWindow failed");

        --g_GlfwRefCount;
        if (g_GlfwRefCount == 0)
            glfwTerminate();

        return false;
    }

    glfwSetWindowUserPointer(m_Window, this);
    glfwSetScrollCallback(m_Window, ScrollCallback);

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

