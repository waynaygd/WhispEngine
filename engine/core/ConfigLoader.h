#pragma once
#include <string>
#include <vector>

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
    float x = 0.0f;
    float y = 0.0f;
    float scale = 0.4f;
    float angle = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float angularVelocity = 0.0f;
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
};
