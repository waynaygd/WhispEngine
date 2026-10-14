#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace editor {
struct ImportFile {std::filesystem::path source;std::string relative;std::uintmax_t bytes=0;};
struct ImportPlan {std::filesystem::path sourceRoot;std::string package,entry;std::vector<ImportFile> files;std::uintmax_t bytes=0;};
// Package publication is one directory rename. Existing packages are never replaced.
// This deliberately refuses ambiguous OBJ options and dependencies outside sourceRoot.
class AssetImport {
public:
 static std::filesystem::path ProjectRoot();
 static std::filesystem::path SafeTarget(const std::filesystem::path& root,const std::string& key);
 static bool NewFolder(const std::filesystem::path& root,const std::string& parent,const std::string& name,std::string* error=nullptr);
 static bool Scan(const std::filesystem::path& source,const std::string& package,ImportPlan&,std::string* error=nullptr,const std::filesystem::path& dependencyRoot={});
 static bool Publish(const std::filesystem::path& root,const ImportPlan&,std::string* error=nullptr);
};
}
