#pragma once
#include "AssetCatalog.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
namespace editor {
// Presentation records refer to the current catalog revision, never to World handles.
struct AssetContentItem {
 std::string identity,name,key;std::size_t assetIndex=0;bool folder=false;
 static std::string FolderId(const std::string& path){return "folder:"+path;}
};
struct AssetNavigation {
 std::map<std::string,std::vector<std::string>> children;std::map<std::string,std::size_t> counts;
 std::map<std::string,std::vector<AssetContentItem>> content;std::vector<AssetContentItem> allAssets;
 std::vector<std::string> history{std::string()};std::size_t cursor=0;std::uint64_t revision=~std::uint64_t(0);bool projectSearch=false;
 void Build(const std::set<std::string>& folders,const std::vector<AssetEntry>& files,std::uint64_t version){if(revision==version)return;revision=version;children.clear();counts.clear();content.clear();allAssets.clear();
  for(auto& path:folders){auto slash=path.find_last_of('/');auto parent=slash==std::string::npos?std::string():path.substr(0,slash);children[parent].push_back(path);content[parent].push_back({AssetContentItem::FolderId(path),path.substr(parent.empty()?0:parent.size()+1),path,0,true});}
  for(std::size_t i=0;i<files.size();++i){const auto& file=files[i];const auto parent=Parent(file.key);++counts[parent];AssetContentItem item{file.key,file.key.substr(parent.empty()?0:parent.size()+1),file.key,i,false};content[parent].push_back(item);allAssets.push_back(std::move(item));}
  auto less=[](const auto& a,const auto& b){if(a.folder!=b.folder)return a.folder;return a.name!=b.name?a.name<b.name:a.key<b.key;};
  for(auto& [path,items]:content)std::sort(items.begin(),items.end(),less);std::sort(allAssets.begin(),allAssets.end(),less);
 }
 void Go(const std::string& folder){if(folder==history[cursor])return;history.resize(cursor+1);history.push_back(folder);cursor=history.size()-1;}
 std::string Back(){if(cursor)--cursor;return history[cursor];}std::string Forward(){if(cursor+1<history.size())++cursor;return history[cursor];}
 static std::string Parent(const std::string& path){auto i=path.find_last_of('/');return i==std::string::npos?std::string():path.substr(0,i);}
 static bool InFolder(const std::string& key,const std::string& folder){return Parent(key)==folder;}
};
}
