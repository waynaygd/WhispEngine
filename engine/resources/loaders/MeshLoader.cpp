#include "MeshLoader.h"

#include "../../core/AssetPaths.h"
#include "../../core/Logger.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
constexpr char kMeshCacheMagic[4] = { 'W', 'M', 'S', 'H' };
// Version 2 flips imported OBJ UVs into the engine's top-left texture convention.
constexpr std::uint32_t kMeshCacheVersion = 2;

std::filesystem::path BuildBinaryCachePath(const std::filesystem::path& sourcePath)
{
    std::filesystem::path cachePath = sourcePath;
    cachePath += ".wmesh";
    return cachePath;
}

void WriteString(std::ofstream& stream, const std::string& value)
{
    const auto size = static_cast<std::uint32_t>(value.size());
    stream.write(reinterpret_cast<const char*>(&size), sizeof(size));
    if (size > 0)
        stream.write(value.data(), size);
}

bool ReadString(std::ifstream& stream, std::string& outValue)
{
    std::uint32_t size = 0;
    stream.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (!stream.good() || size > 1024 * 1024)
        return false;

    outValue.resize(size);
    if (size > 0)
        stream.read(outValue.data(), size);
    return stream.good();
}

bool LoadMeshBinaryCache(
    const std::filesystem::path& cachePath,
    const std::string& normalizedKey,
    MeshResource& outMesh)
{
    std::ifstream stream(cachePath, std::ios::binary);
    if (!stream.is_open())
        return false;

    char magic[4]{};
    std::uint32_t version = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t submeshCount = 0;
    stream.read(magic, sizeof(magic));
    stream.read(reinterpret_cast<char*>(&version), sizeof(version));
    stream.read(reinterpret_cast<char*>(&vertexCount), sizeof(vertexCount));
    stream.read(reinterpret_cast<char*>(&indexCount), sizeof(indexCount));
    stream.read(reinterpret_cast<char*>(&submeshCount), sizeof(submeshCount));

    if (!stream.good() ||
        std::memcmp(magic, kMeshCacheMagic, sizeof(kMeshCacheMagic)) != 0 ||
        version != kMeshCacheVersion)
    {
        return false;
    }

    outMesh.meshData.vertices.resize(vertexCount);
    outMesh.meshData.indices.resize(indexCount);
    outMesh.meshData.submeshes.resize(submeshCount);
    if (vertexCount > 0)
    {
        stream.read(
            reinterpret_cast<char*>(outMesh.meshData.vertices.data()),
            static_cast<std::streamsize>(sizeof(MeshVertex) * vertexCount));
    }
    if (indexCount > 0)
    {
        stream.read(
            reinterpret_cast<char*>(outMesh.meshData.indices.data()),
            static_cast<std::streamsize>(sizeof(std::uint32_t) * indexCount));
    }

    for (auto& submesh : outMesh.meshData.submeshes)
    {
        stream.read(reinterpret_cast<char*>(&submesh.baseVertex), sizeof(submesh.baseVertex));
        stream.read(reinterpret_cast<char*>(&submesh.vertexCount), sizeof(submesh.vertexCount));
        stream.read(reinterpret_cast<char*>(&submesh.firstIndex), sizeof(submesh.firstIndex));
        stream.read(reinterpret_cast<char*>(&submesh.indexCount), sizeof(submesh.indexCount));
        if (!ReadString(stream, submesh.name) ||
            !ReadString(stream, submesh.materialName) ||
            !ReadString(stream, submesh.diffuseTexturePath))
        {
            return false;
        }
    }

    outMesh.name = cachePath.stem().stem().string();
    outMesh.sourcePath = normalizedKey;
    outMesh.placeholder = false;
    return stream.good();
}

void SaveMeshBinaryCache(const std::filesystem::path& cachePath, const MeshResource& mesh)
{
    std::ofstream stream(cachePath, std::ios::binary);
    if (!stream.is_open())
        return;

    const auto vertexCount = static_cast<std::uint32_t>(mesh.meshData.vertices.size());
    const auto indexCount = static_cast<std::uint32_t>(mesh.meshData.indices.size());
    const auto submeshCount = static_cast<std::uint32_t>(mesh.meshData.submeshes.size());
    stream.write(kMeshCacheMagic, sizeof(kMeshCacheMagic));
    stream.write(reinterpret_cast<const char*>(&kMeshCacheVersion), sizeof(kMeshCacheVersion));
    stream.write(reinterpret_cast<const char*>(&vertexCount), sizeof(vertexCount));
    stream.write(reinterpret_cast<const char*>(&indexCount), sizeof(indexCount));
    stream.write(reinterpret_cast<const char*>(&submeshCount), sizeof(submeshCount));
    if (vertexCount > 0)
    {
        stream.write(
            reinterpret_cast<const char*>(mesh.meshData.vertices.data()),
            static_cast<std::streamsize>(sizeof(MeshVertex) * vertexCount));
    }
    if (indexCount > 0)
    {
        stream.write(
            reinterpret_cast<const char*>(mesh.meshData.indices.data()),
            static_cast<std::streamsize>(sizeof(std::uint32_t) * indexCount));
    }

    for (const auto& submesh : mesh.meshData.submeshes)
    {
        stream.write(reinterpret_cast<const char*>(&submesh.baseVertex), sizeof(submesh.baseVertex));
        stream.write(reinterpret_cast<const char*>(&submesh.vertexCount), sizeof(submesh.vertexCount));
        stream.write(reinterpret_cast<const char*>(&submesh.firstIndex), sizeof(submesh.firstIndex));
        stream.write(reinterpret_cast<const char*>(&submesh.indexCount), sizeof(submesh.indexCount));
        WriteString(stream, submesh.name);
        WriteString(stream, submesh.materialName);
        WriteString(stream, submesh.diffuseTexturePath);
    }
}
}

ResourceLoadResult<MeshResource> MeshLoader::Load(const std::string& normalizedKey, const std::filesystem::path& resolvedPath)
{
    ResourceLoadResult<MeshResource> result;

    if (normalizedKey.empty())
    {
        result.errorMessage = "mesh key is empty";
        return result;
    }

    if (resolvedPath.empty() || !std::filesystem::exists(resolvedPath))
    {
        result.errorMessage = "mesh asset was not found at the resolved runtime path";
        return result;
    }

    const std::filesystem::path binaryCachePath = BuildBinaryCachePath(resolvedPath);
    if (std::filesystem::exists(binaryCachePath) &&
        std::filesystem::last_write_time(binaryCachePath) >= std::filesystem::last_write_time(resolvedPath))
    {
        MeshResource cachedMesh;
        if (LoadMeshBinaryCache(binaryCachePath, normalizedKey, cachedMesh))
        {
            Logger::Get().Info(
                "MeshLoader: loaded binary cache key=" + normalizedKey +
                " vertices=" + std::to_string(cachedMesh.meshData.vertices.size()) +
                " indices=" + std::to_string(cachedMesh.meshData.indices.size()) +
                " submeshes=" + std::to_string(cachedMesh.meshData.submeshes.size()));

            result.success = true;
            result.data = std::move(cachedMesh);
            return result;
        }
    }

    Assimp::Importer importer;
    const unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_JoinIdenticalVertices |
        aiProcess_ImproveCacheLocality |
        aiProcess_FlipUVs;

    const std::string resolvedPathUtf8 = AssetPaths::ToUtf8String(resolvedPath);
    const aiScene* scene = importer.ReadFile(resolvedPathUtf8.c_str(), flags);
    if (scene == nullptr)
    {
        result.errorMessage = importer.GetErrorString();
        return result;
    }

    if (!scene->HasMeshes() || scene->mNumMeshes == 0)
    {
        result.errorMessage = "scene does not contain any meshes";
        return result;
    }

    MeshResource mesh;
    mesh.name = resolvedPath.stem().string();
    mesh.sourcePath = normalizedKey;
    mesh.placeholder = false;

    std::size_t totalVertices = 0;
    std::size_t totalIndices = 0;

    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
    {
        const aiMesh* sourceMesh = scene->mMeshes[meshIndex];
        if (sourceMesh == nullptr)
            continue;

        MeshSubmesh submesh;
        submesh.name = sourceMesh->mName.length > 0
            ? std::string(sourceMesh->mName.C_Str())
            : ("Submesh_" + std::to_string(meshIndex));
        submesh.baseVertex = static_cast<std::uint32_t>(mesh.meshData.vertices.size());
        submesh.vertexCount = sourceMesh->mNumVertices;
        submesh.firstIndex = static_cast<std::uint32_t>(mesh.meshData.indices.size());

        if (scene->HasMaterials() && sourceMesh->mMaterialIndex < scene->mNumMaterials)
        {
            const aiMaterial* material = scene->mMaterials[sourceMesh->mMaterialIndex];
            if (material != nullptr)
            {
                aiString materialName;
                if (material->Get(AI_MATKEY_NAME, materialName) == aiReturn_SUCCESS && materialName.length > 0)
                    submesh.materialName = materialName.C_Str();

                aiString diffuseTexture;
                if (material->GetTexture(aiTextureType_DIFFUSE, 0, &diffuseTexture) == aiReturn_SUCCESS &&
                    diffuseTexture.length > 0)
                {
                    const std::filesystem::path rawTexturePath = diffuseTexture.C_Str();
                    const std::filesystem::path candidatePath = resolvedPath.parent_path() / rawTexturePath;
                    const std::string normalizedTexture = AssetPaths::NormalizeAssetKey(candidatePath);
                    submesh.diffuseTexturePath = normalizedTexture.empty()
                        ? rawTexturePath.generic_string()
                        : normalizedTexture;
                }
            }
        }

        for (unsigned int vertexIndex = 0; vertexIndex < sourceMesh->mNumVertices; ++vertexIndex)
        {
            MeshVertex vertex;

            if (sourceMesh->HasPositions())
            {
                const aiVector3D& position = sourceMesh->mVertices[vertexIndex];
                vertex.position[0] = position.x;
                vertex.position[1] = position.y;
                vertex.position[2] = position.z;
            }

            if (sourceMesh->HasNormals())
            {
                const aiVector3D& normal = sourceMesh->mNormals[vertexIndex];
                vertex.normal[0] = normal.x;
                vertex.normal[1] = normal.y;
                vertex.normal[2] = normal.z;
            }

            if (sourceMesh->HasTextureCoords(0))
            {
                const aiVector3D& uv = sourceMesh->mTextureCoords[0][vertexIndex];
                vertex.uv[0] = uv.x;
                vertex.uv[1] = uv.y;
            }

            mesh.meshData.vertices.push_back(vertex);
        }

        for (unsigned int faceIndex = 0; faceIndex < sourceMesh->mNumFaces; ++faceIndex)
        {
            const aiFace& face = sourceMesh->mFaces[faceIndex];
            if (face.mNumIndices != 3)
                continue;

            for (unsigned int index = 0; index < face.mNumIndices; ++index)
                mesh.meshData.indices.push_back(submesh.baseVertex + face.mIndices[index]);
        }

        submesh.indexCount = static_cast<std::uint32_t>(mesh.meshData.indices.size()) - submesh.firstIndex;
        mesh.meshData.submeshes.push_back(std::move(submesh));
        totalVertices += sourceMesh->mNumVertices;
    }

    totalIndices = mesh.meshData.indices.size();

    if (mesh.meshData.vertices.empty() || mesh.meshData.indices.empty())
    {
        result.errorMessage = "mesh import completed, but produced no vertices or indices";
        return result;
    }

    Logger::Get().Info(
        "MeshLoader: loaded key=" + normalizedKey +
        " vertices=" + std::to_string(totalVertices) +
        " indices=" + std::to_string(totalIndices) +
        " submeshes=" + std::to_string(mesh.meshData.submeshes.size()));

    SaveMeshBinaryCache(binaryCachePath, mesh);

    result.success = true;
    result.data = std::move(mesh);
    return result;
}

MeshResource MeshLoader::CreateDefault()
{
    MeshResource mesh;
    mesh.name = "DefaultMesh";
    mesh.sourcePath = "defaults/mesh";
    mesh.placeholder = true;

    mesh.meshData.vertices =
    {
        { { 0.0f, 0.5f, 0.0f },  { 0.0f, 0.0f, 1.0f }, { 0.5f, 0.0f } },
        { { 0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
        { { -0.5f, -0.5f, 0.0f },{ 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
    };
    mesh.meshData.indices = { 0, 1, 2 };
    mesh.meshData.submeshes =
    {
        MeshSubmesh
        {
            "DefaultSubmesh",
            0,
            3,
            0,
            3,
            "DefaultMaterial",
            "defaults/texture"
        }
    };

    return mesh;
}
