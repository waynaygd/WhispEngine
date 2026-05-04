#include "AssetDependencyValidation.h"

#include "AssetPaths.h"
#include "Logger.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <stb_image.h>

#include <cstring>
#include <filesystem>
#include <string>

namespace
{
constexpr unsigned char kValidationImage[] =
{
    0x42, 0x4D, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x13, 0x0B,
    0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00
};

constexpr const char* kValidationMesh =
    "o ValidationTriangle\n"
    "v 0.0 0.0 0.0\n"
    "v 1.0 0.0 0.0\n"
    "v 0.0 1.0 0.0\n"
    "vt 0.0 0.0\n"
    "vt 1.0 0.0\n"
    "vt 0.0 1.0\n"
    "f 1/1 2/2 3/3\n";

std::string PathToLogString(const std::filesystem::path& path)
{
    return path.empty() ? std::string("<not found>") : path.string();
}
}

bool ValidateAssetDependencyAvailability()
{
    Logger::Get().Info("Asset validation: checking runtime roots and third-party loaders");

    const std::filesystem::path assetRoot = AssetPaths::ResolveAssetRoot();
    const std::filesystem::path shaderRoot = AssetPaths::ResolveShaderRoot();

    Logger::Get().Info("Asset validation: asset root -> " + PathToLogString(assetRoot));
    Logger::Get().Info("Asset validation: shader root -> " + PathToLogString(shaderRoot));

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFileFromMemory(
        kValidationMesh,
        std::strlen(kValidationMesh),
        aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_JoinIdenticalVertices,
        "obj");

    if (scene == nullptr || !scene->HasMeshes() || scene->mNumMeshes == 0)
    {
        Logger::Get().Error("Asset validation: Assimp memory import failed");
        return false;
    }

    Logger::Get().Info(
        "Asset validation: Assimp ready, meshes=" + std::to_string(scene->mNumMeshes) +
        ", vertices=" + std::to_string(scene->mMeshes[0]->mNumVertices));

    int width = 0;
    int height = 0;
    int components = 0;

    if (stbi_info_from_memory(
            kValidationImage,
            static_cast<int>(sizeof(kValidationImage)),
            &width,
            &height,
            &components) == 0)
    {
        Logger::Get().Error("Asset validation: stb_image info decode failed");
        return false;
    }

    int decodedComponents = 0;
    unsigned char* pixels = stbi_load_from_memory(
        kValidationImage,
        static_cast<int>(sizeof(kValidationImage)),
        &width,
        &height,
        &decodedComponents,
        4);

    if (pixels == nullptr)
    {
        Logger::Get().Error("Asset validation: stb_image pixel decode failed");
        return false;
    }

    stbi_image_free(pixels);

    Logger::Get().Info(
        "Asset validation: stb_image ready, size=" + std::to_string(width) + "x" +
        std::to_string(height) + ", sourceChannels=" + std::to_string(components));
    Logger::Get().Info("Asset validation: dependency bootstrap complete");
    return true;
}
