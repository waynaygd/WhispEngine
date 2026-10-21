#pragma once
#include "AssetImport.h"
#include "../core/ProjectContext.h"
#include <map>
#include <chrono>
#include <ctime>
namespace editor {
struct SceneEntry {std::string key,name,modified;};
class SceneCatalog {
 std::vector<SceneEntry> entries;
 std::uint64_t revision=0;
public:
 const auto& Entries()const{return entries;}
 auto Revision()const{return revision;}
 void Refresh(const std::vector<std::filesystem::path>& roots){
  namespace fs=std::filesystem;std::map<std::string,SceneEntry> indexed;
  const auto project=ProjectManager::Active();
  const auto search=project?std::vector<fs::path>{project->root}:roots;
  for(const auto& root:search){auto folder=project?project->SceneRoot():root/"scenes";if(!fs::is_directory(folder))continue;
   for(fs::recursive_directory_iterator it(folder,fs::directory_options::skip_permission_denied),end;it!=end;++it){
    auto encoded=it->path().lexically_relative(root).generic_u8string();std::string key(encoded.begin(),encoded.end());
    try{(void)AssetImport::SafeTarget(root,key);}catch(...){if(it->is_directory())it.disable_recursion_pending();continue;}
    if(!it->is_regular_file()||it->path().extension()!=".json")continue;
    std::string modified;std::error_code ec;auto time=it->last_write_time(ec);if(!ec){auto sys=std::chrono::system_clock::now()+std::chrono::duration_cast<std::chrono::system_clock::duration>(time-fs::file_time_type::clock::now());auto seconds=std::chrono::system_clock::to_time_t(sys);if(auto* tm=std::localtime(&seconds)){char text[32];if(std::strftime(text,sizeof(text),"%Y-%m-%d %H:%M",tm))modified=text;}}
    auto encodedName=it->path().stem().u8string();indexed.try_emplace(key,SceneEntry{key,std::string(encodedName.begin(),encodedName.end()),modified});
   }
  }
  std::vector<SceneEntry> ready;for(auto& [key,entry]:indexed)ready.push_back(std::move(entry));entries=std::move(ready);++revision;
 }
};
}
