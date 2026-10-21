#include "MeshLoader.h"
#include "MaterialLoader.h"
#include "../TangentSpace.h"
#include <functional>
#include <assimp/GltfMaterial.h>

#include "../../core/AssetPaths.h"
#include "../../core/Logger.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
constexpr char kMeshCacheMagic[4] = { 'W', 'M', 'S', 'H' };
// Version 4 also stores material slots and imported material metadata.
constexpr std::uint32_t kMeshCacheVersion = 6;

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
        version != kMeshCacheVersion || vertexCount>10000000 || indexCount>30000000 || submeshCount>100000)
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
        stream.read(reinterpret_cast<char*>(&submesh.materialSlot),sizeof(submesh.materialSlot));
        stream.read(reinterpret_cast<char*>(&submesh.hasUV0),sizeof(submesh.hasUV0));
        if (!ReadString(stream, submesh.name) ||
            !ReadString(stream, submesh.materialName) ||
            !ReadString(stream, submesh.diffuseTexturePath))
        {
            return false;
        }
    }

    std::string materialsJson;
    if(!ReadString(stream,materialsJson))return false;
    try {
        for(const auto& j:nlohmann::json::parse(materialsJson)) {
            auto parsed=MaterialLoader::Parse(normalizedKey+"#material/"+std::to_string(outMesh.meshData.materials.size()),j);
            if(!parsed.success)return false;
            outMesh.meshData.materials.push_back(std::move(parsed.data));
        }
    } catch(...) {return false;}
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
        stream.write(reinterpret_cast<const char*>(&submesh.materialSlot),sizeof(submesh.materialSlot));
        stream.write(reinterpret_cast<const char*>(&submesh.hasUV0),sizeof(submesh.hasUV0));
        WriteString(stream, submesh.name);
        WriteString(stream, submesh.materialName);
        WriteString(stream, submesh.diffuseTexturePath);
    }
    auto j=nlohmann::json::array();
    for(const auto& m:mesh.meshData.materials)j.push_back(MaterialLoader::Serialize(m));
    WriteString(stream,j.dump());
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
    // OBJ metadata and glTF external buffers may change without touching the model.
    bool dependenciesFresh=true;
    // Assimp does not expose the complete OBJ MTL dependency graph. Reimport
    // material-bearing OBJ on resource load/reload rather than caching stale slots.
    // ResourceManager still caches the imported result between frames.
    if(resolvedPath.extension()==".obj") {
        std::ifstream obj(resolvedPath);std::string line;
        while(std::getline(obj,line)){std::istringstream fields(line);std::string command;fields>>command;if(command=="mtllib"){dependenciesFresh=false;break;}}
    }
    if(std::filesystem::exists(binaryCachePath))for(const auto& entry:std::filesystem::directory_iterator(resolvedPath.parent_path())) {
        const auto ext=entry.path().extension();
        if((ext==".mtl"||ext==".bin")&&entry.last_write_time()>std::filesystem::last_write_time(binaryCachePath))dependenciesFresh=false;
    }
    if (dependenciesFresh && std::filesystem::exists(binaryCachePath) &&
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

    for(unsigned i=0;i<scene->mNumMaterials;++i) {
        const auto* source=scene->mMaterials[i];MaterialResource m;
        aiString name;source->Get(AI_MATKEY_NAME,name);m.name=name.C_Str();
        m.sourcePath=normalizedKey+"#material/"+std::to_string(i);m.placeholder=false;
        aiColor4D color(1,1,1,1);
        if(source->Get(AI_MATKEY_BASE_COLOR,color)!=aiReturn_SUCCESS)source->Get(AI_MATKEY_COLOR_DIFFUSE,color);
        if(m.name!="DefaultMaterial") {m.baseColor[0]=color.r;m.baseColor[1]=color.g;m.baseColor[2]=color.b;m.baseColor[3]=color.a;}
        for(auto& value:m.baseColor)value=std::isfinite(value)?std::clamp(value,0.f,1.f):1.f;
        source->Get(AI_MATKEY_TWOSIDED,m.doubleSided);
        aiString mode;if(source->Get(AI_MATKEY_GLTF_ALPHAMODE,mode)==aiReturn_SUCCESS) {
            if(std::string(mode.C_Str())=="MASK")m.surfaceMode=MaterialSurfaceMode::Masked;
            if(std::string(mode.C_Str())=="BLEND")m.surfaceMode=MaterialSurfaceMode::Transparent;
        } else {float opacity=1;source->Get(AI_MATKEY_OPACITY,opacity);if(opacity<.999f)m.surfaceMode=MaterialSurfaceMode::Transparent;}
        source->Get(AI_MATKEY_GLTF_ALPHACUTOFF,m.alphaCutoff);
        m.alphaCutoff=std::isfinite(m.alphaCutoff)?std::clamp(m.alphaCutoff,0.f,1.f):.5f;
        aiString texture;
        if(source->GetTexture(aiTextureType_BASE_COLOR,0,&texture)!=aiReturn_SUCCESS)source->GetTexture(aiTextureType_DIFFUSE,0,&texture);
        if(texture.length) {
            const auto* embedded=scene->GetEmbeddedTexture(texture.C_Str());
            if(embedded) {
                for(unsigned t=0;t<scene->mNumTextures;++t)if(scene->mTextures[t]==embedded)m.texturePath=normalizedKey+"#embedded/"+std::to_string(t);
            } else {
                m.texturePath=AssetPaths::NormalizeAssetKey(resolvedPath.parent_path()/std::filesystem::u8path(texture.C_Str()));
                if(m.texturePath.empty())m.diagnostics="Imported texture escapes asset root; default texture used";
            }
        }
        const bool gltf=resolvedPath.extension()==".gltf"||resolvedPath.extension()==".glb";
        if(gltf) {
            m.shadingModel=ShadingModel::PBR;m.metallic=1;m.roughness=1;
            source->Get(AI_MATKEY_METALLIC_FACTOR,m.metallic);source->Get(AI_MATKEY_ROUGHNESS_FACTOR,m.roughness);
            source->Get(AI_MATKEY_GLTF_TEXTURE_SCALE(aiTextureType_NORMALS,0),m.normalScale);
            if(source->Get(AI_MATKEY_GLTF_TEXTURE_STRENGTH(aiTextureType_LIGHTMAP,0),m.occlusionStrength)!=aiReturn_SUCCESS)source->Get("$tex.file.strength",aiTextureType_LIGHTMAP,0,m.occlusionStrength);
        }
        const auto importTexture=[&](aiTextureType type,std::string& path,int uvSlot) {
            aiString image;unsigned uv=0;
            if(source->GetTexture(type,0,&image,nullptr,&uv)!=aiReturn_SUCCESS)return;
            m.textureTexCoords[uvSlot]=int(uv);
            if(uv!=0)m.diagnostics+="Unsupported UV set for slot "+std::to_string(uvSlot)+"; map disabled. ";
            if(const auto* embedded=scene->GetEmbeddedTexture(image.C_Str())) {
                for(unsigned t=0;t<scene->mNumTextures;++t)if(scene->mTextures[t]==embedded)path=normalizedKey+"#embedded/"+std::to_string(t);
            } else path=AssetPaths::NormalizeAssetKey(resolvedPath.parent_path()/std::filesystem::u8path(image.C_Str()));
            if(path.empty())m.diagnostics+="Imported texture escapes asset root; map disabled. ";
        };
        importTexture(aiTextureType_BASE_COLOR,m.texturePath,0);
        importTexture(aiTextureType_NORMALS,m.normalTexturePath,1);
        if(gltf)importTexture(aiTextureType_UNKNOWN,m.metallicRoughnessTexturePath,2);
        importTexture(aiTextureType_LIGHTMAP,m.occlusionTexturePath,3);
        if(!std::isfinite(m.normalScale)||m.normalScale<0||m.normalScale>16){m.normalScale=1;m.normalTexturePath.clear();m.diagnostics+="Invalid normal scale; map disabled. ";}
        if(!std::isfinite(m.occlusionStrength)||m.occlusionStrength<0||m.occlusionStrength>1){m.occlusionStrength=1;m.occlusionTexturePath.clear();m.diagnostics+="Invalid AO strength; map disabled. ";}
        m.metallic=std::isfinite(m.metallic)?std::clamp(m.metallic,0.f,1.f):1.f;
        m.roughness=std::isfinite(m.roughness)?std::clamp(m.roughness,0.f,1.f):1.f;
        mesh.meshData.materials.push_back(std::move(m));
    }
    std::size_t totalVertices = 0;
    std::size_t totalIndices = 0;

    // Bake each referenced node instance, preserving authored tangents with the
    // linear transform rather than Assimp PreTransform's inverse transpose.
    std::vector<std::pair<const aiMesh*,aiMatrix4x4>> references;
    const std::function<void(const aiNode*,const aiMatrix4x4&)> visit=[&](const aiNode* node,const aiMatrix4x4& parent) {
        const auto model=parent*node->mTransformation;
        for(unsigned i=0;i<node->mNumMeshes;++i)references.emplace_back(scene->mMeshes[node->mMeshes[i]],model);
        for(unsigned i=0;i<node->mNumChildren;++i)visit(node->mChildren[i],model);
    };
    visit(scene->mRootNode,aiMatrix4x4());
    unsigned meshIndex=0;
    for (const auto& reference:references)
    {
        const aiMesh* sourceMesh = reference.first;
        const auto& nodeModel=reference.second;
        const aiMatrix3x3 linear(nodeModel);
        const float orientation=nodeModel.Determinant()<0?-1.f:1.f;
        const aiMatrix3x3 normalTransform=aiMatrix3x3(nodeModel).Inverse().Transpose();
        if (sourceMesh == nullptr)
            continue;

        MeshSubmesh submesh;
        submesh.name = sourceMesh->mName.length > 0
            ? std::string(sourceMesh->mName.C_Str())
            : ("Submesh_" + std::to_string(meshIndex));
        submesh.baseVertex = static_cast<std::uint32_t>(mesh.meshData.vertices.size());
        submesh.vertexCount = sourceMesh->mNumVertices;
        submesh.firstIndex = static_cast<std::uint32_t>(mesh.meshData.indices.size());

        submesh.materialSlot=sourceMesh->mMaterialIndex;
        submesh.hasUV0=sourceMesh->HasTextureCoords(0);
        if(submesh.materialSlot<mesh.meshData.materials.size()) {
            submesh.materialName=mesh.meshData.materials[submesh.materialSlot].name;
            submesh.diffuseTexturePath=mesh.meshData.materials[submesh.materialSlot].texturePath;
        }

        for (unsigned int vertexIndex = 0; vertexIndex < sourceMesh->mNumVertices; ++vertexIndex)
        {
            MeshVertex vertex;

            if (sourceMesh->HasPositions())
            {
                const aiVector3D position = nodeModel*sourceMesh->mVertices[vertexIndex];
                vertex.position[0] = position.x;
                vertex.position[1] = position.y;
                vertex.position[2] = position.z;
            }

            if (sourceMesh->HasNormals())
            {
                const aiVector3D normal = normalTransform*sourceMesh->mNormals[vertexIndex];
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

            if(sourceMesh->HasTangentsAndBitangents()&&sourceMesh->HasNormals()) {
                const auto localN=sourceMesh->mNormals[vertexIndex],localT=sourceMesh->mTangents[vertexIndex],localB=sourceMesh->mBitangents[vertexIndex];
                const float sign=(localN^localT)*localB<0?-1.f:1.f;
                auto n=TangentSpace::Unit({vertex.normal[0],vertex.normal[1],vertex.normal[2]});
                auto transformed=linear*localT;TangentSpace::V t{transformed.x,transformed.y,transformed.z};
                double projection=TangentSpace::Dot(n,t);for(int j=0;j<3;++j)t[j]-=n[j]*projection;
                t=TangentSpace::Unit(t);
                if(TangentSpace::Dot(n,n)>.5&&TangentSpace::Dot(t,t)>.5){for(int j=0;j<3;++j)vertex.tangent[j]=float(t[j]);vertex.tangent[3]=sign*orientation;}
            }
            mesh.meshData.vertices.push_back(vertex);
        }

        for (unsigned int faceIndex = 0; faceIndex < sourceMesh->mNumFaces; ++faceIndex)
        {
            const aiFace& face = sourceMesh->mFaces[faceIndex];
            if (face.mNumIndices != 3)
                continue;

            for (unsigned int index = 0; index < face.mNumIndices; ++index)
                mesh.meshData.indices.push_back(submesh.baseVertex + face.mIndices[orientation<0?2-index:index]);
        }

        submesh.indexCount = static_cast<std::uint32_t>(mesh.meshData.indices.size()) - submesh.firstIndex;
        TangentSpace::Generate(mesh.meshData,submesh.firstIndex,submesh.indexCount);
        submesh.vertexCount=static_cast<std::uint32_t>(mesh.meshData.vertices.size())-submesh.baseVertex;
        mesh.meshData.submeshes.push_back(std::move(submesh));
        ++meshIndex;
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

    if(!normalizedKey.starts_with("@engine/"))SaveMeshBinaryCache(binaryCachePath, mesh);

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
