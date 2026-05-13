#pragma once
#include <array>
#include <string>
#include <vector>

#include "../ecs/MathTypes.h"
#include "../ecs/components/MeshRendererComponent.h"
#include "../render/RenderFactory.h" 

struct WindowConfig
{
    RenderBackend backend = RenderBackend::DX12;
    std::string title = "WhispEngine";
    int width = 1280;
    int height = 720;
    float clear[4] = { 0.08f, 0.08f, 0.12f, 1.0f };
};

struct EcsDemoEntityConfig
{
    std::string tag;
    std::string meshPath;
    std::string texturePath;
    std::string shaderPath;
    std::string materialPath;
    std::array<float, 4> materialTint = { 1.0f, 1.0f, 1.0f, 1.0f };
    bool visible = true;
    bool bounce = true;
    ecs::Vec3 position{};
    ecs::Vec3 rotation{};
    ecs::Vec3 scale{ 0.4f, 0.4f, 1.0f };
    ecs::Vec3 linearVelocity{};
    ecs::Vec3 angularVelocity{};
};

struct EcsDemoConfig
{
    bool logSnapshots = true;
    std::string sceneFile;
    std::vector<EcsDemoEntityConfig> initialEntities;
};

struct AppConfig
{
    struct PhysicsConfig
    {
        float gravity = 9.81f;
        float linearDamping = 0.985f;
        int substeps = 2;
        float restitution = 0.05f;
        float friction = 0.85f;
    };

    RenderBackend activeBackend = RenderBackend::DX12;
    std::vector<WindowConfig> windows;
    EcsDemoConfig ecsDemo;
    PhysicsConfig physics;
};

class ConfigLoader
{
public:
    static bool Load(const std::string& path, AppConfig& outCfg, std::string* outError = nullptr);

private:
    static RenderBackend ParseBackend(const std::string& s);
};
