#pragma once
#include "../ecs/ComponentRegistry.h"
#include <filesystem>
#include <set>
#include <optional>
namespace editor {
struct AssetType {
    std::string name,icon,folder;
    ecs::AssetKind kind=ecs::AssetKind::None;
    std::vector<std::string> extensions;
    bool shaderRoot=false;
};
struct AssetEntry {std::string key;std::size_t type=0;std::optional<std::uintmax_t> bytes;};
struct AssetDependencyAudit {std::vector<std::string> confirmed,dynamic,incomplete;};
// Register additional types at startup, then Refresh. Script/Prefab can provide
// their own activation/creation handlers later without changing browser layout.
class AssetCatalog {
    std::vector<AssetType> types;
    std::vector<AssetEntry> entries;
    std::set<std::string> folders,created,hiddenFallbacks;
    std::uint64_t revision=0;
public:
    static std::string DirectoryKey(const std::filesystem::path& p){auto text=p.generic_u8string();return {text.begin(),text.end()};}
    AssetCatalog();
    void Register(AssetType);
    void Refresh();
    const auto& Types()const{return types;}
    const auto& Entries()const{return entries;}
    const auto& Folders()const{return folders;}
    auto Revision()const{return revision;}
    bool CreatedHere(const std::string& key)const{return created.contains(key);}
    bool CreateMaterial(const std::string& key,std::string* error=nullptr);
    bool RenameMaterial(const std::string& from,const std::string& to,std::string* error=nullptr);
    bool DeleteMaterial(const std::string& key,std::string* error=nullptr);
    static bool ContainsReference(const nlohmann::json&,const std::string&);
    static std::filesystem::path SafeMaterialPath(const std::string&);
    // File operations affect this active-root path only. Referenced assets are
    // deliberately blocked; there is no blind rewriting of project documents.
    static std::filesystem::path EditablePath(const std::string&);
    static std::string RenameDestination(const std::string& key,const std::string& name);
    static std::string FileSuffix(const std::string& key);
    static std::vector<std::string> Dependencies(const std::string& key);
    static AssetDependencyAudit AuditDependencies(const std::string& key);
    bool RenameAssetFile(const std::string& from,const std::string& to,std::string* error=nullptr,bool acceptDynamicRisk=false);
    bool MoveAssetFile(const std::string& from,const std::string& to,std::string* error=nullptr,bool acceptDynamicRisk=false);
    bool DeleteAssetFile(const std::string& key,std::string* error=nullptr,bool acceptDynamicRisk=false);
};
}
