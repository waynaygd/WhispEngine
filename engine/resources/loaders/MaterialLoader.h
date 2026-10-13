#pragma once
#include "../MaterialResource.h"
#include "../ResourceLoader.h"
#include <filesystem>
#include <nlohmann/json.hpp>
class MaterialLoader {
public:
    static ResourceLoadResult<MaterialResource> Load(const std::string&,const std::filesystem::path&);
    static ResourceLoadResult<MaterialResource> Parse(const std::string&,const nlohmann::json&);
    static nlohmann::json Serialize(const MaterialResource&);
    static bool Save(const std::string& key,const MaterialResource&,std::string* error=nullptr);
    static MaterialResource CreateDefault();
};
