#include "resources/ResourceManager.h"
#include <iostream>
#include <stdexcept>

static void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
{
    try {
        JobSystem jobs; jobs.Initialize();
        {
            ResourceManager resources(&jobs);
            auto mesh = resources.LoadAsync<MeshResource>("models/validation_cube.obj").get();
            auto same = resources.LoadAsync<MeshResource>("models/validation_cube.obj").get();
            Require(mesh == same, "async mesh requests did not share a resource");
            auto material = resources.LoadAsync<MaterialResource>("materials/blue.material.json").get();
            auto missing = resources.LoadAsync<TextureResource>("textures/missing-regression.png").get();
            jobs.WaitAll();
            resources.PumpFinalization(100);
            // Material finalization schedules dependencies on the owning thread.
            jobs.WaitAll(); resources.PumpFinalization(100); resources.PollAsyncLoads();
            Require(mesh->IsLoaded() && !mesh->GetData().meshData.vertices.empty(), "async mesh did not finalize");
            Require(material->IsLoaded(), "async material did not finalize");
            Require(missing->IsFailed() && missing->IsUsable(), "async failure did not preserve fallback");
            resources.ClearAll();
            auto reloaded = resources.LoadAsync<MeshResource>("models/validation_cube.obj").get();
            resources.Shutdown();
            Require(reloaded->IsLoaded(), "resource shutdown did not drain/finalize pending decode");
        }
        jobs.Shutdown();
        std::cout << "Resource regression passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
