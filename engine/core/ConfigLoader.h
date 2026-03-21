#pragma once
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
    ecs::PrimitiveType primitive = ecs::PrimitiveType::Triangle;
    ecs::Vec4 color{};
    std::string material = "default";
    std::string texture;
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
    std::vector<EcsDemoEntityConfig> initialEntities;
};

struct AppConfig
{
    RenderBackend activeBackend = RenderBackend::DX12;
    std::vector<WindowConfig> windows;
    EcsDemoConfig ecsDemo;
};

class ConfigLoader
{
public:
    static bool Load(const std::string& path, AppConfig& outCfg, std::string* outError = nullptr);

private:
    static RenderBackend ParseBackend(const std::string& s);
    static ecs::PrimitiveType ParsePrimitiveType(const std::string& s);
};
