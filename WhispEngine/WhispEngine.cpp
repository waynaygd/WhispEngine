#include "core/Application.h"
#include <cstring>

static RenderBackend ParseBackend(int argc, char** argv)
{
    RenderBackend backend = RenderBackend::DX12;

    for (int i = 1; i < argc; ++i)
    {
        const char* a = argv[i];
        if (std::strncmp(a, "--backend=", 10) == 0)
        {
            const char* v = a + 10;
            if (std::strcmp(v, "dx12") == 0) backend = RenderBackend::DX12;
            else if (std::strcmp(v, "vulkan") == 0 || std::strcmp(v, "vk") == 0) backend = RenderBackend::Vulkan;
        }
    }

    return backend;
}

int main(int argc, char** argv)
{
    Application app;

    app.SetBackend(ParseBackend(argc, argv)); 

    if (!app.Initialize())
        return -1;

    int result = app.Run();
    app.Shutdown();
    return result;
}
