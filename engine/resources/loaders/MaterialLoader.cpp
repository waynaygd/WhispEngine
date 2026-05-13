#include "MaterialLoader.h"

#include "../../core/Logger.h"

#include <fstream>

#include <nlohmann/json.hpp>

ResourceLoadResult<MaterialResource> MaterialLoader::Load(const std::string& normalizedKey, const std::filesystem::path& resolvedPath)
{
    ResourceLoadResult<MaterialResource> result;
    if (normalizedKey.empty())
    {
        result.errorMessage = "material key is empty";
        return result;
    }

    std::ifstream file(resolvedPath);
    if (!file.is_open())
    {
        result.errorMessage = "material file was not found at the resolved runtime path";
        return result;
    }

    nlohmann::json json;
    try
    {
        file >> json;
    }
    catch (const std::exception& e)
    {
        result.errorMessage = std::string("material JSON parse failed: ") + e.what();
        return result;
    }

    MaterialResource material;
    material.name = json.value("name", resolvedPath.stem().string());
    material.sourcePath = normalizedKey;
    material.shaderPath = json.value("shaderPath", std::string("dx12/textured.hlsl"));
    material.texturePath = json.value("texturePath", std::string());
    if (json.contains("baseColor") && json["baseColor"].is_array())
    {
        const auto& color = json["baseColor"];
        for (std::size_t i = 0; i < color.size() && i < 4; ++i)
            material.baseColor[i] = color[i].get<float>();
    }
    material.placeholder = false;

    Logger::Get().Info(
        "MaterialLoader: loaded key=" + normalizedKey +
        " shader=" + material.shaderPath +
        " texture=" + material.texturePath);

    result.success = true;
    result.data = std::move(material);
    return result;
}

MaterialResource MaterialLoader::CreateDefault()
{
    MaterialResource material;
    material.name = "DefaultMaterial";
    material.sourcePath = "defaults/material";
    material.shaderPath = "dx12/textured.hlsl";
    material.texturePath.clear();
    material.placeholder = true;
    return material;
}
