#include "core/Application.h"
#include <cstring>

int main(int argc, char** argv)
{
    Application app;

    if (!app.Initialize())
        return -1;

    int result = app.Run();
    app.Shutdown();
    return result;
}
