#include "Prefab.h"
#include "../core/ProjectContext.h"
#include "../core/AssetPaths.h"
#include "../ecs/ComponentRegistry.h"
#include <fstream>
#include <map>
#include <set>
#include <chrono>
#include <algorithm>

namespace scripting {
using Json=nlohmann::json;
namespace {
using Nodes=std::map<std::string,Json>;
Nodes Index(const Json& data){Nodes nodes;for(auto& n:data.at("entities"))if(!nodes.emplace(n.at("id").get<std::string>(),n).second)throw std::runtime_error("Duplicate prefab member ID");return nodes;}
std::string Escape(const std::string& s){std::string out;for(char c:s)out+=c=='~'?"~0":c=='/'?"~1":std::string(1,c);return out;}
bool Atomic(const Json& j){return !j.is_object()||j.contains("$entity");}
void Differences(const Json& before,const Json& after,const std::string& path,Json& changes){
 if(before==after)return;
 if(!Atomic(before)&&!Atomic(after)){
  for(auto& [key,value]:before.items())if(!after.contains(key))changes[path+"/"+Escape(key)]={{"remove",true},{"type",value.type_name()}};
  for(auto& [key,value]:after.items()){auto next=path+"/"+Escape(key);if(before.contains(key))Differences(before.at(key),value,next,changes);else changes[next]={{"value",value},{"absent",true}};}
 }else changes[path]={{"value",after},{"type",before.is_number()?"number":before.type_name()}};
}
void CheckDelta(const Json& d){
 if(!d.is_object()||d.value("version",0)!=3||!d.contains("parentPrefab")||!d.at("parentPrefab").is_string())throw std::runtime_error("Expected child prefab v3 and parentPrefab");
 SafeAsset(d.at("parentPrefab").get<std::string>(),".prefab.json");
 if(!d.at("overrides").is_object()||!d.at("addedEntities").is_array()||!d.at("suppressedEntities").is_array()||d.dump().size()>1024*1024)throw std::runtime_error("Invalid child prefab delta");
}
struct Stamp {std::filesystem::file_time_type time{};std::uintmax_t size=0;bool exists=false;bool operator==(const Stamp&)const=default;};
Stamp FileStamp(const std::string& key){try{auto p=SafeAsset(key,".prefab.json");return {std::filesystem::last_write_time(p),std::filesystem::file_size(p),true};}catch(...){return {};}}
struct Entry {Json resolved;std::string error;std::map<std::string,Stamp> dependencies;std::uint64_t revision=0;};
std::map<std::string,Entry> cache;std::string project;std::uint64_t revision=0;auto nextPoll=std::chrono::steady_clock::time_point{};
void Project(){auto p=ProjectManager::Active();auto identity=p?AssetPaths::ToUtf8String(p->descriptorPath):AssetPaths::ToUtf8String(AssetPaths::ResolveAssetRoot());if(identity!=project){cache.clear();project=identity;}}
Json Resolve(const std::string& key,std::vector<std::string>& chain,std::map<std::string,Stamp>& dependencies){
 if(chain.size()>=32)throw std::runtime_error("Prefab inheritance depth exceeds 32: "+key);
 if(std::find(chain.begin(),chain.end(),key)!=chain.end()){std::string text="Prefab inheritance cycle: ";for(auto& part:chain)text+=part+" -> ";throw std::runtime_error(text+key);}
 chain.push_back(key);dependencies[key]=FileStamp(key);auto raw=ReadPrefabAsset(key);Json result;
 if(raw.value("version",0)==3)result=ResolvePrefabDelta(raw,Resolve(raw.at("parentPrefab").get<std::string>(),chain,dependencies));else result=DecodePrefabAsset(raw);
 chain.pop_back();return result;
}
}
Json ReadPrefabAsset(const std::string& key){auto path=SafeAsset(key,".prefab.json");if(std::filesystem::file_size(path)>1024*1024)throw std::runtime_error("Prefab exceeds 1 MiB: "+key);std::ifstream file(path);if(!file)throw std::runtime_error("Cannot open Prefab: "+key);auto raw=Json::parse(file,[](int depth,Json::parse_event_t,Json&){if(depth>64)throw std::runtime_error("Prefab JSON nesting exceeds 64");return true;});return raw;}
Json NewChildPrefab(const std::string& parent){ReadPrefab(parent);return {{"version",3},{"parentPrefab",parent},{"overrides",Json::object()},{"addedEntities",Json::array()},{"suppressedEntities",Json::array()}};}
Json ResolvePrefabDelta(const Json& delta,const Json& parent){
 CheckDelta(delta);ValidatePrefab(parent);
 auto nodes=Index(parent);const auto root=parent.at("root").get<std::string>();std::set<std::string> suppressed;
 for(auto& value:delta.at("suppressedEntities")){auto id=value.get<std::string>();if(id==root||!nodes.contains(id)||!suppressed.insert(id).second)throw std::runtime_error("Invalid suppressed prefab member: "+id);}
 bool more=true;while(more){more=false;for(auto& [id,n]:nodes)if(n.contains("parent")&&suppressed.contains(n.at("parent").get<std::string>())&&suppressed.insert(id).second)more=true;}
 for(auto& id:suppressed)nodes.erase(id);
 for(auto& n:delta.at("addedEntities")){auto id=n.at("id").get<std::string>();if(!nodes.emplace(id,n).second||id==root)throw std::runtime_error("Added prefab member ID conflicts: "+id);}
 for(auto& [id,override]:delta.at("overrides").items()){
  if(!nodes.contains(id)||suppressed.contains(id))throw std::runtime_error("Override targets deleted prefab member: "+id);
  auto& n=nodes.at(id);if(override.contains("parent")){if(id==root)throw std::runtime_error("Cannot reparent prefab root");n["parent"]=override.at("parent");}
  for(auto& [component,edit]:override.at("components").items()){
   auto& components=n["components"];auto* descriptor=ecs::ComponentRegistry::Builtins().Find(component);
   if(!edit.is_object()||!descriptor||component=="SceneIdentity"||component=="PrefabInstance")throw std::runtime_error("Unsupported inherited component: "+component);
   if(edit.contains("added")){if(components.contains(component))throw std::runtime_error("Added component now exists in parent: "+component);components[component]=edit.at("added");continue;}
   if(!components.contains(component))throw std::runtime_error("Override targets deleted component: "+component);
   if(edit.value("removed",false)){if(!descriptor->removable||component=="Transform")throw std::runtime_error("Cannot suppress required component: "+component);components.erase(component);continue;}
   auto& values=components[component];
   if(edit.contains("script")&&(edit.at("properties").contains("/scriptPath")?edit.at("properties").at("/scriptPath").at("value").get<std::string>():values.value("scriptPath",std::string()))!=edit.at("script").get<std::string>())throw std::runtime_error("Lua field override belongs to a different Script Asset");
   for(auto& [path,change]:edit.at("properties").items()){
    if(path.empty()||path.size()>1024||path.front()!='/')throw std::runtime_error("Invalid property override path");Json::json_pointer pointer(path);auto parentPointer=pointer.parent_pointer();if(!values.contains(parentPointer)||!values.at(parentPointer).is_object()||values.at(parentPointer).contains("$entity"))throw std::runtime_error("Property overrides cannot index arrays or descend into typed references");const bool exists=values.contains(pointer);
    if(!exists&&!change.value("absent",false))throw std::runtime_error("Override targets deleted property: "+component+path);
    if(change.value("absent",false)&&!exists){auto top=path.substr(1,path.find('/',1)==std::string::npos?std::string::npos:path.find('/',1)-1);bool known=values.contains(top)||std::any_of(descriptor->properties.begin(),descriptor->properties.end(),[&](auto& p){return p.key==top;});if(!known)throw std::runtime_error("Unsupported property override: "+component+path);}
    if(exists&&change.contains("type")){auto type=change.at("type").get<std::string>();if(type!="number"&&type!=values.at(pointer).type_name()||type=="number"&&!values.at(pointer).is_number())throw std::runtime_error("Inherited property type changed: "+component+path);}
    if(change.value("remove",false)){if(!exists)throw std::runtime_error("Removed property no longer exists");auto patch=Json::array({{{"op","remove"},{"path",path}}});values=values.patch(patch);}else values[pointer]=change.at("value");
   }
  }
 }
 Json list=Json::array();std::set<std::string> visited;auto walk=[&](auto&& self,const std::string& id)->void{if(!visited.insert(id).second)throw std::runtime_error("Prefab hierarchy cycle");list.push_back(nodes.at(id));for(auto& [child,n]:nodes)if(n.contains("parent")&&n.at("parent")==id)self(self,child);};walk(walk,root);
 if(visited.size()!=nodes.size())throw std::runtime_error("Inherited hierarchy has orphaned/local members; resolve parent deletion explicitly");Json result={{"version",2},{"root",root},{"entities",list}};return DecodePrefabAsset(result);
}
Json EditPrefabDelta(const Json& original,const Json& before,const Json& after){
 CheckDelta(original);ValidatePrefab(before);ValidatePrefab(after);if(before.at("root")!=after.at("root"))throw std::runtime_error("Cannot replace inherited root identity");auto delta=original;auto old=Index(before),now=Index(after);std::set<std::string> local;for(auto& n:delta["addedEntities"])local.insert(n.at("id").get<std::string>());
 for(auto& [id,n]:old)if(!now.contains(id)){if(!local.contains(id)&&!(n.contains("parent")&&!now.contains(n.at("parent").get<std::string>())&&!local.contains(n.at("parent").get<std::string>()))&&std::find(delta["suppressedEntities"].begin(),delta["suppressedEntities"].end(),id)==delta["suppressedEntities"].end())delta["suppressedEntities"].push_back(id);delta["overrides"].erase(id);}
 Json added=Json::array();for(auto& [id,n]:now){if(local.contains(id)||!old.contains(id)){added.push_back(n);continue;}auto& prev=old.at(id);if(prev==n)continue;auto& o=delta["overrides"][id];if(o.is_null())o={{"components",Json::object()}};if(prev.value("parent",std::string())!=n.value("parent",std::string()))o["parent"]=n.at("parent");
  for(auto& [c,v]:prev.at("components").items())if(!n.at("components").contains(c)){if(o["components"].contains(c)&&o["components"][c].contains("added"))o["components"].erase(c);else o["components"][c]={{"removed",true}};}
  for(auto& [c,v]:n.at("components").items()){
   if(!prev.at("components").contains(c)){bool restored=false;if(o["components"].contains(c)&&o["components"][c].value("removed",false)){auto parentNodes=Index(ReadPrefab(original.at("parentPrefab").get<std::string>()));if(parentNodes.contains(id)&&parentNodes.at(id).at("components").contains(c)){o["components"][c]={{"properties",Json::object()}};Differences(parentNodes.at(id).at("components").at(c),v,"",o["components"][c]["properties"]);restored=true;}}if(!restored)o["components"][c]={{"added",v}};continue;}if(v==prev.at("components").at(c))continue;
   auto& edit=o["components"][c];if(edit.contains("added")){edit["added"]=v;continue;}if(!edit.contains("properties"))edit={{"properties",Json::object()}};
   Differences(prev.at("components").at(c),v,"",edit["properties"]);
   if(c=="LuaScript"&&v.contains("scriptPath"))edit["script"]=v.at("scriptPath");
  }
 }delta["addedEntities"]=std::move(added);for(auto it=delta["overrides"].begin();it!=delta["overrides"].end();)if(it.value().at("components").empty()&&!it.value().contains("parent"))it=delta["overrides"].erase(it);else ++it;return delta;
}
void PollPrefabReload(bool force){Project();auto now=std::chrono::steady_clock::now();if(!force&&now<nextPoll)return;nextPoll=now+std::chrono::milliseconds(250);for(auto it=cache.begin();it!=cache.end();){bool changed=false;for(auto& [key,stamp]:it->second.dependencies)if(FileStamp(key)!=stamp){changed=true;break;}if(changed)it=cache.erase(it);else ++it;}}
void ClearPrefabCache(){cache.clear();project.clear();nextPoll={};}
void InvalidatePrefabCache(){cache.clear();nextPoll={};}
Json ReadPrefab(const std::string& key){PollPrefabReload(true);if(auto it=cache.find(key);it!=cache.end()){if(!it->second.error.empty())throw std::runtime_error(it->second.error);return it->second.resolved;}Entry e;std::vector<std::string> chain;try{e.resolved=Resolve(key,chain,e.dependencies);}catch(const std::exception& failure){e.error=failure.what();}e.revision=++revision;cache[key]=e;if(!e.error.empty())throw std::runtime_error(e.error);return e.resolved;}
std::uint64_t PrefabRevision(const std::string& key){PollPrefabReload(false);if(auto it=cache.find(key);it!=cache.end())return it->second.revision;try{ReadPrefab(key);}catch(...){}return cache.at(key).revision;}
}
