#include "ConfigLoader.h"
#include "Logger.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

static void SetError(std::string* outError, const std::string& msg)
{
	if (outError) *outError = msg;
}

static void ReadVec3(const nlohmann::json& j, const char* key, ecs::Vec3& outVec)
{
    if (!j.contains(key))
        return;

    const auto& value = j[key];
    if (value.is_array())
    {
        if (value.size() > 0) outVec.x = value[0].get<float>();
        if (value.size() > 1) outVec.y = value[1].get<float>();
        if (value.size() > 2) outVec.z = value[2].get<float>();
        return;
    }

    if (value.is_number())
    {
        outVec.x = value.get<float>();
        outVec.y = outVec.x;
        outVec.z = outVec.x;
    }
}

static void ReadColor4(const nlohmann::json& j, const char* key, std::array<float, 4>& outColor)
{
    if (!j.contains(key) || !j[key].is_array())
        return;

    const auto& value = j[key];
    for (std::size_t i = 0; i < value.size() && i < outColor.size(); ++i)
        outColor[i] = value[i].get<float>();
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
    outCfg.activeBackend = RenderBackend::DX12;
    outCfg.windows.clear();
    outCfg.ecsDemo = EcsDemoConfig{};

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

    outCfg.activeBackend = ParseBackend(j.value("activeRenderer", std::string("DX12")));

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

    if (j.contains("ecsDemo") && j["ecsDemo"].is_object())
    {
        const auto& demo = j["ecsDemo"];
        outCfg.ecsDemo.logSnapshots = demo.value("logSnapshots", true);
        outCfg.ecsDemo.sceneFile = demo.value("sceneFile", std::string());

        if (demo.contains("initialEntities") && demo["initialEntities"].is_array())
        {
            for (const auto& je : demo["initialEntities"])
            {
                EcsDemoEntityConfig entityCfg;
                entityCfg.tag = je.value("tag", std::string());
                entityCfg.meshPath = je.value("meshPath", std::string());
                entityCfg.texturePath = je.value("texturePath", std::string());
                entityCfg.shaderPath = je.value("shaderPath", std::string());
                entityCfg.materialPath = je.value("materialPath", std::string());
                ReadColor4(je, "color", entityCfg.materialTint);
                entityCfg.visible = je.value("visible", true);
                entityCfg.bounce = je.value("bounce", true);
                ReadVec3(je, "position", entityCfg.position);
                ReadVec3(je, "rotation", entityCfg.rotation);
                ReadVec3(je, "scale", entityCfg.scale);
                ReadVec3(je, "linearVelocity", entityCfg.linearVelocity);
                ReadVec3(je, "angularVelocity", entityCfg.angularVelocity);
                ReadVec3(je, "colliderHalfExtents", entityCfg.colliderHalfExtents);
                ReadVec3(je, "colliderOffset", entityCfg.colliderOffset);
                entityCfg.colliderManual = je.value("colliderManual", false);

                if (je.contains("x")) entityCfg.position.x = je.value("x", 0.0f);
                if (je.contains("y")) entityCfg.position.y = je.value("y", 0.0f);
                if (je.contains("z")) entityCfg.position.z = je.value("z", 0.0f);
                if (je.contains("angle")) entityCfg.rotation.z = je.value("angle", 0.0f);
                if (je.contains("vx")) entityCfg.linearVelocity.x = je.value("vx", 0.0f);
                if (je.contains("vy")) entityCfg.linearVelocity.y = je.value("vy", 0.0f);
                if (je.contains("scale") && je["scale"].is_number())
                {
                    const float uniformScale = je["scale"].get<float>();
                    entityCfg.scale.x = uniformScale;
                    entityCfg.scale.y = uniformScale;
                    entityCfg.scale.z = 1.0f;
                }
                if (je.contains("angularVelocity") && je["angularVelocity"].is_number())
                    entityCfg.angularVelocity.z = je["angularVelocity"].get<float>();
                if (je.contains("colliderHalfExtents") || je.contains("colliderOffset"))
                    entityCfg.colliderManual = true;

                outCfg.ecsDemo.initialEntities.push_back(entityCfg);
            }
        }
    }

    if (j.contains("physics") && j["physics"].is_object())
    {
        const auto& physics = j["physics"];
        outCfg.physics.gravity = physics.value("gravity", outCfg.physics.gravity);
        outCfg.physics.linearDamping = physics.value("linearDamping", outCfg.physics.linearDamping);
        outCfg.physics.substeps = physics.value("substeps", outCfg.physics.substeps);
        outCfg.physics.restitution = physics.value("restitution", outCfg.physics.restitution);
        outCfg.physics.friction = physics.value("friction", outCfg.physics.friction);
    }

    Logger::Get().Info(
        "ConfigLoader: loaded " + std::to_string(outCfg.windows.size()) +
        " windows from config, active renderer=" + std::string(j.value("activeRenderer", "DX12")) +
        ", ecs demo entities=" + std::to_string(outCfg.ecsDemo.initialEntities.size()));
    return true;
}
