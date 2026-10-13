#pragma once
#include "MaterialResource.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include "../core/AssetPaths.h"
inline std::string MaterialTextureOverrideKey(const nlohmann::json& value) {
    const auto path=value.get<std::string>();if(path.empty())return {};
    const auto file=std::filesystem::u8path(path);
    if(file.is_absolute()||file.has_root_name()||path.find(':')!=std::string::npos)throw std::runtime_error("texture override must be a relative asset key");
    const auto key=AssetPaths::NormalizeAssetKey(file);if(key.empty())throw std::runtime_error("texture override escapes asset root");return key;
}
inline nlohmann::json MaterialOverridesToJson(const MaterialOverrides& o) {
    nlohmann::json j=nlohmann::json::object();
    if(o.shaderPath)j["shaderPath"]=*o.shaderPath;if(o.texturePath)j["texturePath"]=*o.texturePath;
    if(o.doubleSided)j["doubleSided"]=*o.doubleSided;if(o.castShadows)j["castShadows"]=*o.castShadows;if(o.receiveShadows)j["receiveShadows"]=*o.receiveShadows;
    if(o.metallic)j["metallic"]=*o.metallic;if(o.roughness)j["roughness"]=*o.roughness;
    if(o.shadingModel)j["shadingModel"]=*o.shadingModel==ShadingModel::PBR?"PBR":"Lambert";
    if(o.normalTexturePath)j["normalTexturePath"]=*o.normalTexturePath;
    if(o.metallicRoughnessTexturePath)j["metallicRoughnessTexturePath"]=*o.metallicRoughnessTexturePath;
    if(o.occlusionTexturePath)j["occlusionTexturePath"]=*o.occlusionTexturePath;
    if(o.normalScale)j["normalScale"]=*o.normalScale;if(o.occlusionStrength)j["occlusionStrength"]=*o.occlusionStrength;
    if(o.textureTexCoords)j["textureTexCoords"]=*o.textureTexCoords;
    return j;
}
inline MaterialOverrides MaterialOverridesFromJson(const nlohmann::json& j) {
    if(!j.is_object())throw std::runtime_error("materialOverrides must be an object");
    MaterialOverrides o;
    if(j.contains("shaderPath"))o.shaderPath=j.at("shaderPath").get<std::string>();
    if(j.contains("texturePath"))o.texturePath=j.at("texturePath").get<std::string>();
    if(j.contains("doubleSided"))o.doubleSided=j.at("doubleSided").get<bool>();
    if(j.contains("castShadows"))o.castShadows=j.at("castShadows").get<bool>();
    if(j.contains("receiveShadows"))o.receiveShadows=j.at("receiveShadows").get<bool>();
    for(const char* key:{"metallic","roughness"})if(j.contains(key)) {
        if(!j.at(key).is_number())throw std::runtime_error("material override must be numeric");
        float v=j.at(key).get<float>();if(!std::isfinite(v)||v<0||v>1)throw std::runtime_error("material override must be finite in [0,1]");
        (std::string(key)=="metallic"?o.metallic:o.roughness)=v;
    }
    if(j.contains("shadingModel")){const auto model=j.at("shadingModel").get<std::string>();if(model!="Lambert"&&model!="PBR")throw std::runtime_error("unknown shadingModel override");o.shadingModel=model=="PBR"?ShadingModel::PBR:ShadingModel::Lambert;}
    if(j.contains("normalTexturePath"))o.normalTexturePath=MaterialTextureOverrideKey(j.at("normalTexturePath"));
    if(j.contains("metallicRoughnessTexturePath"))o.metallicRoughnessTexturePath=MaterialTextureOverrideKey(j.at("metallicRoughnessTexturePath"));
    if(j.contains("occlusionTexturePath"))o.occlusionTexturePath=MaterialTextureOverrideKey(j.at("occlusionTexturePath"));
    for(const char* key:{"normalScale","occlusionStrength"})if(j.contains(key)) {
        if(!j.at(key).is_number())throw std::runtime_error("texture factor must be numeric");
        float v=j.at(key).get<float>();float upper=std::string(key)=="normalScale"?16.f:1.f;
        if(!std::isfinite(v)||v<0||v>upper)throw std::runtime_error("invalid texture factor");
        (std::string(key)=="normalScale"?o.normalScale:o.occlusionStrength)=v;
    }
    if(j.contains("textureTexCoords")) {
        auto uv=j.at("textureTexCoords");if(!uv.is_array()||uv.size()!=4)throw std::runtime_error("invalid UV sets");
        std::array<int,4> sets{};for(int i=0;i<4;++i){if(!uv[i].is_number_integer())throw std::runtime_error("invalid UV set");sets[i]=uv[i].get<int>();if(sets[i]<0||sets[i]>7)throw std::runtime_error("invalid UV set");}o.textureTexCoords=sets;
    }
    return o;
}
