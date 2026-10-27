#pragma once
#include "ResourceLoader.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <memory>
// Authored definitions are immutable snapshots. Gameplay receives value copies.
struct DataResource { std::shared_ptr<const nlohmann::json> document; std::uint64_t cpuBytes=0; };
class DataLoader {
public:
 static std::filesystem::path SafePath(const std::string&);
 static void Validate(const nlohmann::json&);
 static void ValidateSchema(const nlohmann::json&,unsigned depth=0);
 static void ValidateValue(const nlohmann::json& schema,const nlohmann::json& value,const std::string& at="value",unsigned depth=0);
 static ResourceLoadResult<DataResource> Load(const std::string&,const std::filesystem::path&);
 static bool Save(const std::string&,const nlohmann::json&,std::string* error=nullptr);
 static nlohmann::json Default(const nlohmann::json& schema);
 static std::string ExportCSV(const nlohmann::json&);
 static nlohmann::json ImportCSV(nlohmann::json,const std::string&);
};
