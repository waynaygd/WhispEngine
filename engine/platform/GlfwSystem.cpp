#include "GlfwSystem.h"
#include "../core/Logger.h"
#include <GLFW/glfw3.h>

static int g_GlfwRefCount = 0;

bool GlfwSystem::Init()
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
    return true;
}

void GlfwSystem::Shutdown()
{
    if (g_GlfwRefCount <= 0) return;
    --g_GlfwRefCount;

    if (g_GlfwRefCount == 0)
        glfwTerminate();
}
