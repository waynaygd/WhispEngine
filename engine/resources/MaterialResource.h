#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>

enum class ShadingModel { Lambert, PBR };
enum class MaterialSurfaceMode { Opaque, Masked, Transparent };
// CPU asset. GPU handles remain in the existing shader/texture resources.
struct MaterialResource {
    std::string name = "UnnamedMaterial";
    std::string sourcePath;
    std::string shaderPath = "dx12/textured.hlsl";
    std::string texturePath; // Base Color; legacy key remains canonical.
    std::string normalTexturePath, metallicRoughnessTexturePath, occlusionTexturePath, emissiveTexturePath;
    std::array<int,4> textureTexCoords{0,0,0,0}; // base, normal, MR, AO; UV0 supported.
    float normalScale=1.f, occlusionStrength=1.f;
    // Linear material RGBA factor. UI displays RGB in sRGB; alpha stays linear.
    float baseColor[4] = {1,1,1,1};
    ShadingModel shadingModel = ShadingModel::Lambert;
    float metallic = 0.f, roughness = .5f;
    MaterialSurfaceMode surfaceMode = MaterialSurfaceMode::Opaque;
    float alphaCutoff = .5f;
    bool doubleSided = false, castShadows = true, receiveShadows = true;
    bool placeholder = true;
    std::string diagnostics;
};
struct MaterialOverrides {
    std::optional<float> metallic, roughness, normalScale, occlusionStrength;
    std::optional<std::string> normalTexturePath, metallicRoughnessTexturePath, occlusionTexturePath;
    std::optional<std::array<int,4>> textureTexCoords;
    std::optional<ShadingModel> shadingModel;
    std::optional<std::string> shaderPath, texturePath;
    std::optional<bool> doubleSided, castShadows, receiveShadows;
};
inline void ApplyMaterialOverrides(MaterialResource& material, const MaterialOverrides& overrides) {
    if(overrides.normalTexturePath)material.normalTexturePath=*overrides.normalTexturePath;
    if(overrides.metallicRoughnessTexturePath)material.metallicRoughnessTexturePath=*overrides.metallicRoughnessTexturePath;
    if(overrides.occlusionTexturePath)material.occlusionTexturePath=*overrides.occlusionTexturePath;
    if(overrides.normalScale)material.normalScale=*overrides.normalScale;
    if(overrides.occlusionStrength)material.occlusionStrength=*overrides.occlusionStrength;
    if(overrides.textureTexCoords)material.textureTexCoords=*overrides.textureTexCoords;
    if(overrides.metallic) material.metallic=*overrides.metallic;
    if(overrides.roughness) material.roughness=*overrides.roughness;
    if(overrides.shadingModel) material.shadingModel=*overrides.shadingModel;
    if(overrides.shaderPath) material.shaderPath=*overrides.shaderPath;
    if(overrides.texturePath) material.texturePath=*overrides.texturePath;
    if(overrides.doubleSided) material.doubleSided=*overrides.doubleSided;
    if(overrides.castShadows) material.castShadows=*overrides.castShadows;
    if(overrides.receiveShadows) material.receiveShadows=*overrides.receiveShadows;
}
