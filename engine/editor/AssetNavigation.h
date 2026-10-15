#pragma once
#include "AssetCatalog.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
namespace editor {
struct AssetNavigation {
 std::map<std::string,std::vector<std::string>> children;std::map<std::string,std::size_t> counts;
 std::vector<std::string> history{std::string()};std::size_t cursor=0;std::uint64_t revision=~std::uint64_t(0);bool projectSearch=false;
 void Build(const std::set<std::string>& folders,const std::vector<AssetEntry>& files,std::uint64_t version){if(revision==version)return;revision=version;children.clear();counts.clear();
  for(auto& path:folders){auto slash=path.find_last_of('/');auto parent=slash==std::string::npos?std::string():path.substr(0,slash);children[parent].push_back(path);}
  for(auto& file:files){auto slash=file.key.find_last_of('/');++counts[slash==std::string::npos?std::string():file.key.substr(0,slash)];}
 }
 void Go(const std::string& folder){if(folder==history[cursor])return;history.resize(cursor+1);history.push_back(folder);cursor=history.size()-1;}
 std::string Back(){if(cursor)--cursor;return history[cursor];}std::string Forward(){if(cursor+1<history.size())++cursor;return history[cursor];}
 static std::string Parent(const std::string& path){auto i=path.find_last_of('/');return i==std::string::npos?std::string():path.substr(0,i);}
 static bool InFolder(const std::string& key,const std::string& folder){return Parent(key)==folder;}
};
}
