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

struct AppConfig
{
    std::vector<WindowConfig> windows;
};

class ConfigLoader
{
public:
    static bool Load(const std::string& path, AppConfig& outCfg, std::string* outError = nullptr);

private:
    static RenderBackend ParseBackend(const std::string& s);
};
