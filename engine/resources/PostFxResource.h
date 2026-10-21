#pragma once
#include <nlohmann/json.hpp>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <filesystem>
#include <string>
#include <vector>
#include "ResourceLoader.h"
struct PostFxParameter {
    std::string name,type; nlohmann::json initial; float minimum=0,maximum=1;
    unsigned Components()const;
    bool Valid(const nlohmann::json&)const;
};
struct PostFxDefinition {
    std::string key,origin,id,name,description,shader,source;int order=0;
    std::vector<PostFxParameter> parameters;
};
struct PostFxResource {std::shared_ptr<const PostFxDefinition> definition;};
struct PostFxOverride {
    std::string resource,id;bool enabled=true;float weight=1;
    nlohmann::json parameters=nlohmann::json::object();
};
void to_json(nlohmann::json&,const PostFxOverride&);
void from_json(const nlohmann::json&,PostFxOverride&);
struct PostFxLayer {PostFxOverride effect;float influence=1;};
struct PostFxRuntimeOverride {std::optional<float> weight; nlohmann::json parameters=nlohmann::json::object();};
using PostFxRuntimeOverrides=std::map<std::string,PostFxRuntimeOverride>;
struct PostFxPass {std::shared_ptr<const PostFxDefinition> definition;float weight=0;std::map<std::string,nlohmann::json> parameters;};
struct PostFxFrame {std::vector<PostFxPass> passes;float time=0,delta=0;};
class ResourceManager;
PostFxFrame ResolvePostFx(const std::vector<PostFxLayer>&,ResourceManager&,const PostFxRuntimeOverrides*,float,float);
class PostFxLoader {
public:
    static std::filesystem::path SafePath(const std::string&);
    static ResourceLoadResult<PostFxResource> Load(const std::string&,const std::filesystem::path&);
};
