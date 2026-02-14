#include "core/Application.h"

int main()
{
    Application app;

    if (!app.Initialize())
        return -1;

    int result = app.Run();
    app.Shutdown();

    return result;
}
