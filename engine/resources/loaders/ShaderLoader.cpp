#include "ShaderLoader.h"

#include "../../core/Logger.h"

#include <fstream>
#include <filesystem>
#include <sstream>

namespace
{
std::string ReadTextFile(const std::filesystem::path& path, std::string& outError)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open())
    {
        outError = "failed to open shader source file";
        return {};
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof())
    {
        outError = "failed while reading shader source file";
        return {};
    }

    return buffer.str();
}
}

ResourceLoadResult<ShaderResource> ShaderLoader::Load(const std::string& normalizedKey, const std::filesystem::path& resolvedPath)
{
    ResourceLoadResult<ShaderResource> result;

    if (normalizedKey.empty())
    {
        result.errorMessage = "shader key is empty";
        return result;
    }

    if (resolvedPath.empty() || !std::filesystem::exists(resolvedPath))
    {
        result.errorMessage = "shader asset was not found at the resolved runtime path";
        return result;
    }

    std::filesystem::path vertexPath;
    std::filesystem::path fragmentPath;
    std::string language;

    const std::string extension = resolvedPath.extension().generic_string();
    if (extension == ".vert")
    {
        vertexPath = resolvedPath;
        fragmentPath = resolvedPath;
        fragmentPath.replace_extension(".frag");
        language = "glsl";
    }
    else if (extension == ".frag")
    {
        fragmentPath = resolvedPath;
        vertexPath = resolvedPath;
        vertexPath.replace_extension(".vert");
        language = "glsl";
    }
    else if (extension == ".hlsl")
    {
        vertexPath = resolvedPath;
        fragmentPath = resolvedPath;
        language = "hlsl";
    }
    else
    {
        result.errorMessage = "unsupported shader source extension: " + extension;
        return result;
    }

    if (!std::filesystem::exists(vertexPath))
    {
        result.errorMessage = "vertex shader source file was not found";
        return result;
    }

    if (!std::filesystem::exists(fragmentPath))
    {
        result.errorMessage = "fragment shader source file was not found";
        return result;
    }

    std::string vertexError;
    std::string fragmentError;
    const std::string vertexSource = ReadTextFile(vertexPath, vertexError);
    if (!vertexError.empty())
    {
        result.errorMessage = "vertex shader load failed: " + vertexError;
        return result;
    }

    const std::string fragmentSource = ReadTextFile(fragmentPath, fragmentError);
    if (!fragmentError.empty())
    {
        result.errorMessage = "fragment shader load failed: " + fragmentError;
        return result;
    }

    ShaderResource shader;
    shader.name = vertexPath.stem().string();
    shader.sourcePath = normalizedKey;
    shader.vertexPath = vertexPath.generic_string();
    shader.fragmentPath = fragmentPath.generic_string();
    shader.vertexSource = vertexSource;
    shader.fragmentSource = fragmentSource;
    shader.language = language;
    shader.entryPoint = "main";
    shader.hotReloadable = false;
    shader.placeholder = false;

    Logger::Get().Info(
        "ShaderLoader: loaded key=" + normalizedKey +
        " vertexChars=" + std::to_string(shader.vertexSource.size()) +
        " fragmentChars=" + std::to_string(shader.fragmentSource.size()));

    result.success = true;
    result.data = std::move(shader);
    return result;
}

ShaderResource ShaderLoader::CreateDefault()
{
    ShaderResource shader;
    shader.name = "DefaultShader";
    shader.sourcePath = "defaults/shader";
    shader.vertexPath = "defaults/shader.vert";
    shader.fragmentPath = "defaults/shader.frag";
    shader.vertexSource = "// Default vertex shader placeholder\n";
    shader.fragmentSource = "// Default fragment shader placeholder\n";
    shader.language = "none";
    shader.entryPoint = "main";
    shader.hotReloadable = false;
    shader.placeholder = true;
    return shader;
}
