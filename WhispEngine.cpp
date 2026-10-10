#include "core/Application.h"
#include <cstring>
#include <cstdlib>
#include <algorithm>

int main(int argc, char** argv)
{
    Application app;

    if (!app.Initialize())
        return -1;

    // Optional repeatable visual smoke/benchmark run. Normal startup is unchanged.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--stress") == 0 && i + 1 < argc) {
            app.SetupPhysicsStressScene(std::atoi(argv[++i]));
            app.SetEditorPlayMode(true);
        } else if (std::strcmp(argv[i], "--serial") == 0) {
            app.GetPhysicsSystem()->SetParallel(false);
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            app.SetFrameLimit(std::max(1, std::atoi(argv[++i])));
        } else if (std::strcmp(argv[i], "--diagnostics") == 0 && i + 1 < argc) {
            app.SetDiagnosticOutput(argv[++i]);
        } else if (std::strcmp(argv[i], "--stability-scenario") == 0 && i + 1 < argc) {
            app.SetDiagnosticOutput(argv[++i], true);
        } else if (std::strcmp(argv[i], "--wait-tracy") == 0) {
            app.WaitForTracyConnection();
        } else if (std::strcmp(argv[i], "--fixed-state") == 0) {
            app.SetUpdateMode(UpdateMode::Fixed);
        } else if (std::strcmp(argv[i], "--no-interpolation") == 0) {
            app.GetRenderInterpolation().SetEnabled(false);
        } else if (std::strcmp(argv[i], "--no-instancing") == 0) {
            app.SetGpuInstancingEnabled(false);
        } else if (std::strcmp(argv[i], "--render-benchmark") == 0 && i+2<argc) {
            app.SetupPhysicsStressScene(std::atoi(argv[++i]));
            app.SetEditorPlayMode(false);
            app.SetRenderBenchmarkOutput(argv[++i]);
        } else if (std::strcmp(argv[i], "--slow-frame") == 0 && i + 2 < argc) {
            const auto frame = std::max(0, std::atoi(argv[++i]));
            const auto milliseconds = std::max(0, std::atoi(argv[++i]));
            app.SetDiagnosticSlowFrame(frame, milliseconds);
        }
    }

    int result = app.Run();
    app.Shutdown();
    return result;
}
