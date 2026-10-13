#define NOMINMAX
#include "core/AssetPaths.h"
#include "ecs/PhysicsStressScene.h"
#include "ecs/systems/RenderSystem.h"
#include "render/ColorPipeline.h"
#include "render/Environment.h"
#include "resources/ResourceManager.h"
#include "resources/loaders/EnvironmentLoader.h"
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>
#if defined(ENABLE_DX12)
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <GLFW/glfw3.h>
#endif
static void Check(bool b, const char *m)
{
    if (!b)
        throw std::runtime_error(m);
}
static void Near(double a, double b, double t, const char *m)
{
    if (!std::isfinite(a) || std::abs(a - b) > t)
    {
        std::cerr << m << " got " << a << " expected " << b << '\n';
        throw std::runtime_error(m);
    }
}
using V = IblMath::V;
static EnvironmentResource Analytic(bool uniform)
{
    EnvironmentResource e;
    e.width = 128;
    e.height = 64;
    e.radiance.resize(e.width * e.height * 4);
    for (unsigned y = 0; y < e.height; ++y)
        for (unsigned x = 0; x < e.width; ++x)
        {
            double phi = 2 * std::numbers::pi * ((x + .5) / e.width - .5),
                   theta = std::numbers::pi * (y + .5) / e.height;
            V d{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
            for (unsigned k = 0; k < 3; ++k)
                e.radiance[(y * e.width + x) * 4 + k] = float(uniform ? 2 : 2 + d[k]);
            e.radiance[(y * e.width + x) * 4 + 3] = 1;
        }
    return e;
}
// Independent uniform-hemisphere quadrature of the full microfacet BRDF.
// No Hammersley sequence, importance-sampling pdf or production G1 function.
static std::array<double, 2> Reference(double nv, double r)
{
    constexpr int polar = 256, azimuth = 512;
    double alpha = std::pow(std::max(r, .04), 2), a2 = alpha * alpha;
    V v{std::sqrt(1 - nv * nv), 0, nv};
    auto lambda = [&](double c) { return (std::sqrt(1 + a2 * (1 - c * c) / (c * c)) - 1) / 2; };
    std::array<double, 2> ab{};
    for (int y = 0; y < polar; ++y)
    {
        double z = (y + .5) / polar, s = std::sqrt(1 - z * z);
        for (int x = 0; x < azimuth; ++x)
        {
            double phi = 2 * std::numbers::pi * (x + .5) / azimuth;
            V l{s * std::cos(phi), s * std::sin(phi), z}, h{v[0] + l[0], l[1], nv + z};
            double len = std::sqrt(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
            for (auto &c : h)
                c /= len;
            double vh = v[0] * h[0] + nv * h[2], den = 1 + (a2 - 1) * h[2] * h[2],
                   D = a2 / (std::numbers::pi * den * den),
                   G = 1 / ((1 + lambda(nv)) * (1 + lambda(z))), fc = std::pow(1 - vh, 5),
                   weight = D * G / (4 * nv) * 2 * std::numbers::pi / (polar * azimuth);
            ab[0] += (1 - fc) * weight;
            ab[1] += fc * weight;
        }
    }
    return ab;
}
static EnvironmentResource Cpu()
{
    JobSystem jobs;
    Check(jobs.Initialize(), "async jobs");
    {
        ResourceManager async(&jobs);
        const std::string key = "environments/ibl_reload.temporary.hdr";
        auto path = AssetPaths::ResolveAssetPath("environments/uniform.hdr").parent_path() /
                    "ibl_reload.temporary.hdr";
        Check(!std::filesystem::exists(path), "temporary HDR already exists");
        struct Cleanup
        {
            std::filesystem::path p;
            ~Cleanup()
            {
                std::error_code e;
                std::filesystem::remove(p, e);
            }
        } cleanup{path};
        std::filesystem::copy_file(AssetPaths::ResolveAssetPath("environments/uniform.hdr"), path);
        auto asset = async.LoadAsync<EnvironmentResource>(key).get();
        Check(asset->IsLoading(), "async Loading state");
        jobs.WaitAll();
        async.PumpFinalization(32);
        Check(asset->IsLoaded() && !asset->GetData().placeholder, "async Ready state");
        const auto version = asset->GetVersion();
        {
            std::ofstream corrupt(path, std::ios::binary);
            corrupt << "corrupted HDR";
        }
        async.ReloadEnvironmentAsync(key);
        Check(asset->IsLoading(), "async reload status");
        jobs.WaitAll();
        async.PumpFinalization(32);
        Check(asset->IsFailed() && asset->GetVersion() > version && !asset->GetData().placeholder,
              "failed reload last good");
        Near(asset->GetData().radiance[0], 2, 0, "failed reload retained radiance");
        std::filesystem::copy_file(AssetPaths::ResolveAssetPath("environments/uniform.hdr"), path,
                                   std::filesystem::copy_options::overwrite_existing);
        async.ReloadEnvironmentAsync(key);
        jobs.WaitAll();
        async.PumpFinalization(32);
        Check(asset->IsLoaded() && async.Get<EnvironmentResource>(key) == asset,
              "successful reload cache identity");
        async.ReloadEnvironmentAsync(key);
        async.ClearAll();
        jobs.WaitAll();
        async.PumpFinalization(32);
        Check(!async.Has<EnvironmentResource>(key), "Clear invalidates pending finalization");
        async.Shutdown();
    }
    jobs.Shutdown();
    ResourceManager resources(nullptr);
    Check(resources.Load<EnvironmentResource>("../escape.hdr")->IsFailed(), "unsafe HDR key");
    auto extreme = resources.Load<EnvironmentResource>("environments/extreme.hdr");
    Check(extreme->IsLoaded(), "extreme HDR decode");
    Near(extreme->GetData().radiance[0], 65536, 0, "FP32 HDR range");
    auto hdr = resources.Load<EnvironmentResource>("environments/uniform.hdr");
    Check(hdr->IsLoaded() && !hdr->GetData().placeholder, "HDR decode");
    for (std::size_t i = 0; i < hdr->GetData().radiance.size(); i += 4)
        Near(hdr->GetData().radiance[i], 2, 0, "HDR linear radiance not clamped");
    Check(resources.Load<EnvironmentResource>(hdr->GetKey()) == hdr, "shared environment cache");
    Check(resources.Load<EnvironmentResource>("environments/missing.hdr")->IsFailed(),
          "missing HDR fallback");
    V axes[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    auto e = Analytic(false);
    for (unsigned f = 0; f < 6; ++f)
    {
        auto d = IblMath::CubeDirection(f, 0, 0);
        for (int k = 0; k < 3; ++k)
            Near(d[k], axes[f][k], 0, "cube orientation");
        auto c = IblMath::SamplePanorama(e, d);
        for (int k = 0; k < 3; ++k)
        {
            double expected = 2 + d[k];
            if (f == 2 || f == 3)
            {
                if (k == 0)
                    expected = 2 + std::sin(std::numbers::pi / (2 * e.height)) *
                                       std::cos(std::numbers::pi / e.width);
                if (k == 1)
                    expected = 2 + d[k] * std::cos(std::numbers::pi / (2 * e.height));
            }
            Near(c[k], expected, .002, "panorama orientation / finite texel footprint");
        }
    }
    auto convolved = IblMath::Preprocess(e, 16, 5, 2048);
    for (unsigned f = 0; f < 6; ++f)
    {
        const auto &p = convolved.irradiance.subresources[f];
        for (int k = 0; k < 3; ++k)
            Near(p[(2 * 5 + 2) * 4 + k], 2 + 2. / 3 * axes[f][k], .008, "cosine convolution E/pi");
    }
    for (auto [nv, r] : {std::pair{.25, .35}, std::pair{.6, .6}, std::pair{.95, 1.}})
    {
        auto ref = Reference(nv, r), ab = IblMath::IntegrateBrdf(nv, r, 8192);
        Near(ab[0], ref[0], .003, "LUT A independent quadrature");
        Near(ab[1], ref[1], .003, "LUT B independent quadrature");
    }
    auto flat = IblMath::Preprocess(Analytic(true), 16, 4, 128);
    for (const auto *cube : {&flat.environment, &flat.irradiance, &flat.prefiltered})
        for (const auto &sub : cube->subresources)
            for (std::size_t i = 0; i < sub.size(); i += 4)
                Near(sub[i], 2, 1e-6, "uniform environment convolution");
    Check(flat.brdfLut == convolved.brdfLut, "LUT shared once");
    for (double nv : {0., 1e-8, 1.})
        for (double r : {0., 1.})
            for (double v : IblMath::IntegrateBrdf(nv, r))
                Check(std::isfinite(v) && v >= 0, "LUT edge finite");
    auto settings = SanitizeEnvironment({});
    Check(!settings.enabled && settings.toneMapping, "legacy default");
    settings.rotationDegrees = 450;
    Near(SanitizeEnvironment(settings).rotationDegrees, 90, 0, "rotation normalization");
    std::cout << "CPU HDR / faces / cosine irradiance / independent full-BRDF quadrature / uniform "
                 "GGX / cache passed\n";
    return flat;
}
#if defined(ENABLE_DX12)
static float Transfer(float c)
{
    return c <= .0031308f ? c * 12.92f : 1.055f * std::pow(c, 1 / 2.4f) - .055f;
}
static void Save(const std::string &name, const std::vector<std::uint8_t> &p, unsigned w = 320,
                 unsigned h = 240)
{
    std::filesystem::create_directories("ibl-readback");
    std::ofstream out("ibl-readback/" + name + ".ppm", std::ios::binary);
    out << "P6\n" << w << ' ' << h << "\n255\n";
    for (std::size_t i = 0; i < p.size(); i += 4)
        out.write(reinterpret_cast<const char *>(p.data() + i), 3);
}
static void Gpu(const EnvironmentResource &uniform)
{
    GlfwWindow window;
    Check(window.Create(320, 240, "IBL regression"), "window");
    glfwHideWindow(window.GetGlfwHandle());
    Dx12RenderAdapter a;
    Check(a.Initialize(&window), "DX12");
    ResourceManager resources(nullptr);
    auto shader =
        a.CreateShaderProgram(resources.Load<ShaderResource>("dx12/textured.hlsl")->GetData());
    Check(shader.IsValid() && a.SupportsInstancing(shader), "HDR ordinary/instance PSOs");
    Check(a.UploadEnvironment(uniform, 1), "environment GPU upload");
    for (unsigned slot = 0; slot < 3; ++slot)
    {
        auto p = a.ReadBackEnvironment(slot);
        Check(!p.empty(), "cubemap readback");
        for (std::size_t i = 0; i < p.size(); i += 4)
            Near(p[i], 2, 0, "GPU linear >1 cubemap");
    }
    auto lut = a.ReadBackEnvironment(3);
    Check(lut == *uniform.brdfLut, "GPU LUT upload exact");
    MeshData q;
    q.vertices.resize(4);
    q.indices = {0, 1, 2, 0, 2, 3};
    float xyz[4][3] = {{-.8f, .8f, .5f}, {.8f, .8f, .5f}, {.8f, -.8f, .5f}, {-.8f, -.8f, .5f}};
    for (int i = 0; i < 4; ++i)
    {
        std::copy_n(xyz[i], 3, q.vertices[i].position);
        q.vertices[i].normal[2] = -1;
        q.vertices[i].uv[0] = i == 1 || i == 2 ? 1.f : 0.f;
        q.vertices[i].uv[1] = i >= 2 ? 1.f : 0.f;
    }
    auto mesh = a.UploadMesh(q);
    TextureData white;
    white.pixels = {255, 255, 255, 255};
    auto texture = a.CreateTexture2D(white);
    RenderInstanceData data;
    data.model = data.mvp = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    data.normal = BuildNormalMatrix(data.model);
    data.surface = {1, .6f, 1, 0};
    data.baseColor = {1, 1, 1, 1};
    data.tint = {1, 1, 1, 1};
    EnvironmentSettings settings;
    settings.enabled = true;
    LightingSettings light;
    light.directional.enabled = false;
    light.ambientIntensity = 0;
    std::vector<float> linear;
    std::array<float, 4> camera{0, 0, -2, 0};
    auto frame = [&](bool instance, bool viewport = false, bool destroy = false)
    {
        a.SetEnvironmentSettings(settings);
        a.BeginFrame();
        a.Clear(.02f, .03f, .04f, 1);
        if (viewport)
            Check(a.BeginViewportRender(320, 240, nullptr), "HDR viewport");
        a.BeginMainPass();
        a.SetMeshRange({});
        a.BindShader(shader);
        a.BindTexture(0, texture);
        a.SetLighting(BuildLightingConstants(light));
        a.SetCameraPosition(camera);
        a.SetMaterialState({true, false});
        if (instance)
            Check(a.DrawMeshInstanced(mesh, std::span(&data, 1)), "HDR instance draw");
        else
        {
            a.SetModelMatrix(data.model);
            a.SetNormalMatrix(data.normal);
            a.SetSurfaceParameters(data.surface);
            a.SetBaseColor(data.baseColor);
            a.SetTextureParameters(data.textureParameters);
            a.SetTestTransform(data.mvp.data());
            a.SetTestColor(1, 1, 1, 1);
            a.DrawMesh(mesh);
        }
        if (destroy)
            a.ReleaseEnvironment();
        a.EndMainPass();
        if (viewport)
            a.EndViewportRender();
        a.EndFrame();
        linear = a.ReadBackHdrPixels();
        auto pixels = viewport ? a.ReadBackViewportPixels() : a.ReadBackFramePixels();
        a.Present();
        return pixels;
    };
    auto pixels = frame(false);
    Check(pixels == frame(true), "IBL ordinary / instance exact");
    auto viewport = frame(false, true);
    for (int y = 40; y < 200; ++y)
        for (int x = 40; x < 280; ++x)
            for (int k = 0; k < 3; ++k)
                Check(viewport[(y * 320 + x) * 4 + k] == pixels[(y * 320 + x) * 4 + k],
                      "viewport / standalone output equivalence");
    unsigned center = (120 * 320 + 160) * 4;
    Check(linear[center] > 1, "HDR radiance preserved before output");
    float expected = Transfer(linear[center] / (1 + linear[center]));
    Near(pixels[center] / 255., expected, 2. / 255, "Reinhard then single encode");
    Save("metal-full", pixels);
    {
        std::ofstream out("ibl-readback/linear-hdr.f32", std::ios::binary);
        out.write(reinterpret_cast<const char *>(linear.data()), linear.size() * sizeof(float));
    }
    settings.exposure = 1;
    auto exposed = frame(false);
    Check(exposed[center] > pixels[center], "exposure EV doubles radiance");
    Near(exposed[center] / 255., Transfer(2 * linear[center] / (1 + 2 * linear[center])), 2. / 255,
         "exposure ordering");
    settings.exposure = 0;
    settings.toneMapping = false;
    auto clipped = frame(false);
    Check(clipped[center] == 255, "tone OFF clip only output");
    settings.toneMapping = true;
    settings.rotationDegrees = 90;
    Check(frame(false) == pixels, "uniform rotation invariance");
    settings.rotationDegrees = 0;
    settings.specular = false;
    auto metalDiffuse = frame(false);
    Check(metalDiffuse[center] == 0, "metal diffuse must vanish");
    data.surface[0] = 0;
    data.baseColor = {.5f, .5f, .5f, 1};
    auto dielectric = frame(false);
    Near(linear[center], .96, .002, "dielectric energy split irradiance/pi");
    Save("dielectric-diffuse", dielectric);
    settings.diffuse = false;
    settings.specular = true;
    auto spec = frame(false);
    auto ref = Reference(.99999, .6);
    Near(linear[center], 2 * (.04 * ref[0] + ref[1]), .01,
         "dielectric specular exact Smith oracle");
    Save("dielectric-specular", spec);
    settings.enabled = false;
    auto off = frame(false);
    Check(off[center] == 0, "IBL OFF old ambient approximation");
    settings.enabled = true;
    settings.diffuse = true;
    settings.specular = true;
    data.baseColor = {1, 1, 1, 1};
    data.surface[0] = 1;
    auto restored = frame(false);
    Check(restored == pixels, "IBL toggle restore");
    Check(frame(false, false, true) == pixels, "destroy-before-submit retains table/resources");
    Check(a.UploadEnvironment(uniform, 2), "environment recreate");
    Check(frame(true) == pixels, "GPU resource recreation equivalence");
    auto directional = IblMath::Preprocess(Analytic(false), 32, 8, 256);
    Check(a.UploadEnvironment(directional, 3), "directional environment upload");
    for (unsigned slot = 0; slot < 3; ++slot)
    {
        const auto &cube = slot == 0 ? directional.environment
                                    : slot == 1 ? directional.irradiance : directional.prefiltered;
        for (unsigned sub = 0; sub < cube.subresources.size(); ++sub)
            Check(a.ReadBackEnvironment(slot, sub) == cube.subresources[sub],
                  "GPU directional face/mip orientation and float payload");
    }
    settings.specular = false;
    data.surface[0] = 0;
    auto orientation0 = frame(false);
    settings.rotationDegrees = 180;
    auto orientation180 = frame(false);
    Check(orientation0[center + 2] != orientation180[center + 2], "rotation diffuse sample");
    settings.rotationDegrees = 0;
    settings.specular = true;
    settings.diffuse = false;
    data.surface[0] = 1;
    data.surface[1] = .1f;
    auto sharp = frame(false);
    data.surface[1] = .9f;
    auto broad = frame(false);
    Check(sharp != broad, "GGX roughness-to-mip response");
    Save("roughness-sharp", sharp);
    Save("roughness-broad", broad);
    camera = {1, 0, -2, 0};
    Check(frame(false) != broad, "view-dependent environment reflection");
    camera = {0, 0, -2, 0};
    TextureData normalMap;
    normalMap.pixels = {204, 128, 230, 255};
    auto normal = a.CreateTexture2D(normalMap);
    TextureData blackAo;
    blackAo.pixels = {0, 255, 255, 255};
    auto ao = a.CreateTexture2D(blackAo);
    // Changing tangent normal changes the environment reflection, not only direct light.
    settings.specular = true;
    settings.diffuse = false;
    data.surface[1] = .3f;
    auto geometric = frame(false);
    data.textureParameters = {1, 1, 1, 1};
    a.BindTexture(1, normal);
    auto mapped = frame(false);
    Check(mapped != geometric, "normal map must affect IBL reflection");
    Check(mapped == frame(true), "mapped IBL ordinary/instance equivalence");
    Save("normal-mapped", mapped);
    data.textureParameters[0] = 0;
    Check(frame(false) == geometric, "normal scale zero environment fallback");
    data.textureParameters = {1, 1, 0, 1};
    a.BindTexture(1, RenderTextureHandle::Invalid());
    // Scalar AO blocks indirect only. The direct-only reference uses the same HDR output.
    light.directional.enabled = true;
    light.directional.direction = {0, 0, 1};
    light.directional.color = {1, 1, 1};
    light.directional.intensity = .3f;
    settings.diffuse = false;
    settings.specular = false;
    auto directOnly = frame(false);
    settings.diffuse = true;
    settings.specular = true;
    a.BindTexture(3, ao);
    data.textureParameters = {1, 1, 4, 1};
    Check(frame(false) == directOnly, "AO must leave direct light unchanged");
    data.textureParameters[1] = 0;
    Check(frame(false) != directOnly, "AO strength zero must restore indirect");
    data.textureParameters = {1, 1, 0, 1};
    light.directional.enabled = false;
    a.BindTexture(3, RenderTextureHandle::Invalid());
    // Negative and nonuniform render matrices retain both ordinary and instanced TBN contracts.
    data.model = {-1.2f, 0, 0, 0, 0, .7f, 0, 0, 0, 0, 1.8f, 0, 0, 0, 0, 1};
    data.normal = BuildNormalMatrix(data.model);
    auto transformed = frame(false);
    Check(transformed == frame(true), "negative/nonuniform IBL paths differ");
    data.model = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    data.normal = BuildNormalMatrix(data.model);
    // A localized HDR light tests the spatial specular lobe, beyond average color.
    auto spot = Analytic(true);
    for (unsigned y = 0; y < spot.height; ++y)
        for (unsigned x = 0; x < spot.width; ++x)
        {
            double phi = 2 * std::numbers::pi * ((x + .5) / spot.width - .5),
                   theta = std::numbers::pi * (y + .5) / spot.height,
                   z = std::sin(theta) * std::sin(phi);
            float value = float(.1 + 50 * std::pow(std::max(-z, 0.), 64));
            for (int k = 0; k < 3; ++k)
                spot.radiance[(y * spot.width + x) * 4 + k] = value;
        }
    spot = IblMath::Preprocess(std::move(spot), 64, 8, 512);
    Check(a.UploadEnvironment(spot, 4), "localized HDR prefilter upload");
    settings.diffuse = false;
    settings.specular = true;
    data.surface = {1, .1f, 1, 0};
    auto spotSharp = frame(false);
    auto sharpLinear = linear;
    data.surface[1] = .9f;
    auto spotBroad = frame(false);
    auto broadLinear = linear;
    auto lobe = [](const std::vector<float> &p)
    {
        float peak = 0;
        for (int y = 30; y < 210; ++y)
            for (int x = 30; x < 290; ++x)
                peak = std::max(peak, p[(y * 320 + x) * 4]);
        unsigned area = 0;
        for (int y = 30; y < 210; ++y)
            for (int x = 30; x < 290; ++x)
                area += p[(y * 320 + x) * 4] > .5f * peak;
        return std::pair{peak, area};
    };
    auto narrow = lobe(sharpLinear), wide = lobe(broadLinear);
    Check(narrow.first > wide.first && wide.second > narrow.second * 1.5,
          "GGX environment highlight must broaden");
    std::cout << "HDR specular peak/half-height area " << narrow.first << '/' << narrow.second
              << " -> " << wide.first << '/' << wide.second << '\n';
    Save("spot-sharp", spotSharp);
    Save("spot-broad", spotBroad);
    a.DestroyTexture(normal);
    a.DestroyTexture(ao);
    auto legacySource = resources.Load<ShaderResource>("dx12/textured_unlit.hlsl")->GetData();
    for (auto *src : {&legacySource.vertexSource, &legacySource.fragmentSource})
    {
        std::size_t pos = 0;
        while ((pos = src->find("VSOut", pos)) != std::string::npos)
        {
            src->replace(pos, 5, "LegacyVertexOutput");
            pos += 18;
        }
    }
    auto original = shader;
    shader = a.CreateShaderProgram(legacySource);
    Check(shader.IsValid() && a.SupportsInstancing(shader), "generic HDR unlit fallback PSOs");
    auto unlit = frame(false);
    Check(unlit == frame(true), "generic unlit fallback paths");
    Near(unlit[center] / 255., Transfer(.5f), 2. / 255, "unlit HDR output mapping");
    a.DestroyShader(shader);
    shader = original;
    Check(a.UploadEnvironment(uniform, 5), "production environment upload");
    settings.diffuse = settings.specular = settings.enabled = true;
    settings.toneMapping = true;
    settings.rotationDegrees = settings.exposure = 0;
    a.SetEnvironmentSettings(settings);
    ecs::RenderSystem render;
    ecs::World world;
    render.SetRenderAdapter(&a);
    render.SetResourceManager(&resources);
    render.SetLightingSettings(&light);
    render.SetCameraProjection(.8f, 320.f / 240, .01f, 100);
    render.SetCameraTransform({0, 0, -4}, 0, 0);
    std::vector<float> sceneLinear;
    auto sceneFrame = [&](bool instanced, int count = 1)
    {
        render.SetInstancingEnabled(instanced);
        std::vector<std::uint8_t> pixels;
        for (int i = 0; i < count; ++i)
        {
            a.BeginFrame();
            a.Clear(.03f, .04f, .05f, 1);
            render.Update(world, 0);
            a.EndFrame();
            pixels = a.ReadBackFramePixels();
            sceneLinear = a.ReadBackHdrPixels();
            a.Present();
        }
        return pixels;
    };
    for (const char *key : {"models/pbr_textures.gltf", "models/pbr_textures_embedded.gltf",
                            "models/pbr_textures_uv1.gltf", "models/material_slots.obj",
                            "models/material_slots.gltf"})
    {
        world.Clear();
        auto loaded = resources.Load<MeshResource>(key);
        Check(loaded->IsLoaded(), "IBL imported model load");
        for (const auto &m : loaded->GetData().meshData.materials)
            for (const auto *path : {&m.texturePath, &m.normalTexturePath,
                                     &m.metallicRoughnessTexturePath, &m.occlusionTexturePath})
                if (!path->empty())
                    resources.Load<TextureResource>(*path);
        for (int i = 0; i < 2; ++i)
        {
            auto entity = world.CreateEntity();
            world.AddComponent<ecs::TransformComponent>(entity);
            world.AddComponent<ecs::MeshRendererComponent>(entity).meshPath = key;
            auto &material = world.AddComponent<ecs::MaterialComponent>(entity);
            material.overrides.shadingModel = ShadingModel::PBR;
            material.overrides.metallic = .4f;
        }
        auto ordinary = sceneFrame(false, 16);
        Check(ordinary == sceneFrame(true), "IBL imported ordinary/instanced equality");
        std::size_t visible = 0;
        for (std::size_t i = 0; i < ordinary.size(); i += 4)
            visible += ordinary[i] > 80;
        Check(visible > 1000, "IBL imported model lacks lit geometry");
        Save(std::filesystem::path(key).stem().string(), ordinary);
    }
    world.Clear();
    std::vector<ecs::Entity> entities;
    ecs::CreatePhysicsStressScene(world, entities, 500);
    resources.Load<MeshResource>("models/validation_cube.obj");
    resources.Load<MaterialResource>("materials/stress.material.json");
    resources.Load<TextureResource>("defaults/texture");
    for (auto entity : entities)
    {
        auto &m = *world.GetComponent<ecs::MaterialComponent>(entity);
        m.overrides.shadingModel = ShadingModel::PBR;
        m.overrides.metallic = .4f;
        m.overrides.roughness = .6f;
        m.overrides.normalTexturePath = "textures/pbr_normal.png";
        m.overrides.metallicRoughnessTexturePath = m.overrides.occlusionTexturePath =
            "textures/pbr_orm.png";
    }
    ShadowSettings shadows;
    shadows.enabled = true;
    shadows.pcf = false;
    shadows.edgeFade = 0;
    render.SetShadowSettings(&shadows);
    render.SetCameraTransform({22, 20, -12}, std::atan2(-22.f, 42.f), -.35f);
    render.SetCameraProjection(1.04719755f, 320.f / 240, .01f, 150);
    light.directional.enabled = true;
    light.directional.direction = {-.4f, -1, .3f};
    light.directional.intensity = .85f;
    light.ambientIntensity = 0;
    auto lit = sceneFrame(true, 16);
    auto litLinear = sceneLinear;
    Check(a.GetSubmissionStatistics().drawCalls == 1 &&
              a.GetSubmissionStatistics().shadowDrawCalls == 1 &&
              a.GetSubmissionStatistics().outputDrawCalls == 1,
          "500 IBL instances main/shadow/output batching");
    Check(a.GetSubmissionStatistics().uploadBytes == 501 * sizeof(RenderInstanceData),
          "IBL shadow/main instance upload reuse");
    Check(lit == sceneFrame(false), "500 IBL ordinary/instanced equality");
    Save("stress-500-ibl", lit);
    shadows.enabled = false;
    sceneFrame(true);
    auto noShadowLinear = sceneLinear;
    light.directional.intensity = 0;
    auto indirect = sceneFrame(true);
    auto indirectLinear = sceneLinear;
    light.directional.intensity = .85f;
    shadows.enabled = true;
    shadows.debugMode = 1;
    auto mask = sceneFrame(true);
    shadows.debugMode = 0;
    int dark = 0, valid = 0;
    for (std::size_t i = 0; i < litLinear.size(); i += 4)
    {
        if (std::abs(litLinear[i] - noShadowLinear[i]) < 1e-5f)
            continue;
        double visibility = mask[i] / 255.;
        if (visibility < .01 && indirectLinear[i] > .01f)
            ++dark;
        for (int c = 0; c < 3; ++c)
            Near(litLinear[i + c],
                 indirectLinear[i + c] +
                     (noShadowLinear[i + c] - indirectLinear[i + c]) * visibility,
                 .004, "shadow must preserve IBL and scale only direct BRDF");
        ++valid;
    }
    Check(dark > 20 && valid > 20, "IBL shadow readback lacks lit indirect receivers");
    Save("stress-indirect", indirect);
    Save("stress-shadow-mask", mask);
    auto reloaded = resources.Reload<TextureResource>("textures/pbr_normal.png");
    Check(reloaded->IsLoaded() && sceneFrame(true, 8) == lit, "mapped IBL texture reload");
    auto materialReload = resources.Reload<MaterialResource>("materials/stress.material.json");
    Check(materialReload->IsLoaded() && sceneFrame(true, 8) == lit, "IBL material reload");
    world.DestroyEntity(entities[1]);
    sceneFrame(true);
    Check(a.GetSubmissionStatistics().renderedInstances == 500, "IBL entity removal");
    world.Clear();
    sceneFrame(true);
    Check(a.GetSubmissionStatistics().drawCalls == 0 &&
              a.GetSubmissionStatistics().shadowDrawCalls == 0,
          "IBL Clear stale draw");
    render.ReleaseGpuResources();
    Check(a.UploadEnvironment(uniform, 6), "environment recreation after ECS release");
    glfwSetWindowSize(window.GetGlfwHandle(), 400, 280);
    glfwPollEvents();
    auto resized = frame(false);
    Check(resized.size() == 400 * 280 * 4 && linear.size() == 400 * 280 * 4,
          "HDR resize dimensions");
    Check(resized == frame(true), "HDR resize ordinary/instance equality");
    a.ReleaseEnvironment();
    a.DestroyShader(shader);
    a.DestroyMesh(mesh);
    a.DestroyTexture(texture);
    Check(a.GetValidationErrorCount() == 0, "D3D12 ERROR/CORRUPTION");
    a.Shutdown();
    std::cout << "GPU HDR/cubemap/LUT readback, metal/dielectric, exposure/output, rotation, "
                 "roughness, instancing and pending lifetime passed\n";
}
#endif
int main(int argc, char **argv)
{
    try
    {
        auto uniform = Cpu();
#if defined(ENABLE_DX12)
        if (argc > 1 && std::string(argv[1]) == "--dx12")
            Gpu(uniform);
#endif
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "IBL FAILED: " << e.what() << '\n';
        return 1;
    }
}
