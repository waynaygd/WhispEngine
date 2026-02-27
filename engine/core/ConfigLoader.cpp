#include "ConfigLoader.h"
#include "Logger.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

static void SetError(std::string* outError, const std::string& msg)
{
	if (outError) *outError = msg;
}

RenderBackend ConfigLoader::ParseBackend(const std::string& s)
{
    if (s == "DX12" || s == "dx12" || s == "D3D12" || s == "d3d12")
        return RenderBackend::DX12;
    if (s == "Vulkan" || s == "vulkan" || s == "VK" || s == "vk")
        return RenderBackend::Vulkan;

    return RenderBackend::DX12;
}

bool ConfigLoader::Load(const std::string& path, AppConfig& outCfg, std::string* outError)
{
    outCfg.windows.clear();

    std::ifstream f(path);
    if (!f.is_open())
    {
        std::ostringstream ss;
        ss << "ConfigLoader: cannot open file: " << path;
        SetError(outError, ss.str());
        Logger::Get().Error(ss.str());
        return false;
    }

    nlohmann::json j;
    try
    {
        f >> j;
    }
    catch (const std::exception& e)
    {
        std::ostringstream ss;
        ss << "ConfigLoader: JSON parse error: " << e.what();
        SetError(outError, ss.str());
        Logger::Get().Error(ss.str());
        return false;
    }

    if (!j.contains("windows") || !j["windows"].is_array())
    {
        std::string msg = "ConfigLoader: 'windows' array is missing";
        SetError(outError, msg);
        Logger::Get().Error(msg);
        return false;
    }

    for (const auto& jw : j["windows"])
    {
        WindowConfig wc;

        wc.backend = ParseBackend(jw.value("backend", "DX12"));

        wc.title = jw.value("title", std::string("WhispEngine"));
        wc.width = jw.value("width", 1280);
        wc.height = jw.value("height", 720);

        if (jw.contains("clearColor") && jw["clearColor"].is_array())
        {
            const auto& c = jw["clearColor"];
            if (c.size() > 0) wc.clear[0] = c[0].get<float>();
            if (c.size() > 1) wc.clear[1] = c[1].get<float>();
            if (c.size() > 2) wc.clear[2] = c[2].get<float>();
            if (c.size() > 3) wc.clear[3] = c[3].get<float>();
        }

        outCfg.windows.push_back(wc);
    }

    Logger::Get().Info("ConfigLoader: loaded " + std::to_string(outCfg.windows.size()) + " windows from config");
    return true;
}