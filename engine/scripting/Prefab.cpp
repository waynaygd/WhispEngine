#include "Prefab.h"
#include "../core/AssetPaths.h"
#include "../core/ProjectContext.h"
#include "../ecs/ComponentRegistry.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include "../ecs/components/TransformComponent.h"
#include "../ecs/TransformMath.h"
#include <fstream>
#include <cmath>
#include <unordered_set>
#include "../scene/SceneSerializer.h"
#include "../ecs/components/PrefabInstanceComponent.h"
namespace scripting {
std::filesystem::path SafeAsset(const std::string& key,const std::string& suffix){
    std::string error;
    if(key.empty()||!key.ends_with(suffix)||!ecs::ComponentRegistry::ValidateAssetKey(key,ecs::AssetKind::None,&error)||key.find('\0')!=std::string::npos)
        throw std::runtime_error("Invalid asset key: "+key);
    const auto assetRoot=AssetPaths::ResolveAssetRoot();
    if(auto project=ProjectManager::Active())return project->ResolveAsset(key);
    if(assetRoot.empty())throw std::runtime_error("Project asset root is unavailable");
    const auto root=std::filesystem::weakly_canonical(assetRoot);
    const auto path=std::filesystem::weakly_canonical(root/std::filesystem::u8path(key));
    const auto relative=path.lexically_relative(root);
    if(relative.empty()||relative.is_absolute()||*relative.begin()=="..")throw std::runtime_error("Asset escapes root: "+key);
    return path;
}
namespace {
using Json=nlohmann::json;
using Map=std::unordered_map<std::string,std::string>;
void Remap(Json& j,const Map& ids){
    if(j.is_object()&&j.size()==1&&j.contains("$entity")){
        const auto old=j.at("$entity").get<std::string>();auto it=ids.find(old);
        if(it==ids.end())throw std::runtime_error("Prefab contains external entity reference: "+old);
        j["$entity"]=it->second;
    }else if(j.is_structured())for(auto& value:j)Remap(value,ids);
}
Json Scene(const Json& nodes){return {{"version",4},{"entities",nodes}};}
}
void ValidatePrefab(const nlohmann::json& data){
    if(data.dump().size()>1024*1024)throw std::runtime_error("Prefab exceeds 1 MiB");
    if(data.at("version")!=2||!data.at("entities").is_array()||data.at("entities").empty())throw std::runtime_error("Expected prefab v2 subtree");
    const auto root=data.at("root").get<std::string>();Map ids;unsigned roots=0;
    for(const auto& n:data.at("entities")){
        auto id=n.at("id").get<std::string>();if(!ids.emplace(id,id).second)throw std::runtime_error("Duplicate prefab local ID");
        if(n.at("components").contains("PrefabInstance"))throw std::runtime_error("Nested prefabs are not supported");
        if(!n.contains("parent")){++roots;if(id!=root)throw std::runtime_error("Prefab root mismatch");}
        if(!n.at("components").contains("Transform"))throw std::runtime_error("Prefab members require Transform");
    }
    if(roots!=1)throw std::runtime_error("Prefab requires exactly one root");
    auto nodes=data.at("entities");if(!data.value("_legacySceneReferences",false))for(auto& n:nodes)Remap(n["components"],ids);
    ecs::World check;SceneSettings settings;std::string error;
    if(!SceneSerializer::RestoreWorld(Scene(nodes),check,settings,&error))throw std::runtime_error(error);
    auto e=check.Entities().front();while(check.Parent(e).IsValid())e=check.Parent(e);
    if(check.Subtree(e).size()!=nodes.size())throw std::runtime_error("Disconnected prefab hierarchy");
}
nlohmann::json DecodePrefabAsset(nlohmann::json data){
    if(data.at("version")!=1&&data.contains("_legacySceneReferences"))throw std::runtime_error("Reserved prefab compatibility field");
    if(data.at("version")==1){auto c=data.at("components");c.erase("SceneIdentity");if(!c.contains("Transform"))c["Transform"]={{"position",{0,0,0}},{"rotation",{0,0,0}},{"scale",{1,1,1}}};data={{"version",2},{"_legacySceneReferences",true},{"root","root"},{"entities",Json::array({{{"id","root"},{"components",c}}})}};}
    for(auto& n:data.at("entities"))if(n.at("components").contains("MeshRenderer")&&!n["components"]["MeshRenderer"].contains("renderMode"))n["components"]["MeshRenderer"]["renderMode"]=0;
    ValidatePrefab(data);return data;
}
ecs::Entity PrefabRoot(const ecs::World& world,ecs::Entity member){
    for(auto e=member;world.IsAlive(e);e=world.Parent(e))if(world.HasComponent<ecs::PrefabInstanceComponent>(e))return e;return {};
}
nlohmann::json CapturePrefab(ecs::World& world,ecs::Entity root){
    if(!world.IsAlive(root))throw std::runtime_error("Select a prefab root");
    Map ids;const auto subtree=world.Subtree(root);for(auto e:subtree)if(!world.HasComponent<ecs::SceneIdentityComponent>(e)){auto old=SceneSerializer::EntitySceneId(world,e);auto id=ecs::NewSceneIdentity();world.AddComponent<ecs::SceneIdentityComponent>(e).value=id;ids.emplace(old,id);}
    auto* link=world.GetComponent<ecs::PrefabInstanceComponent>(root);
    if(link)for(auto it=link->members.begin();it!=link->members.end();++it)ids.emplace(it.value().at("$entity").get<std::string>(),it.key());
    for(auto e:subtree){auto id=SceneSerializer::EntitySceneId(world,e);if(!ids.contains(id))ids.emplace(id,id);}
    Json nodes=Json::array();for(auto e:subtree){
        auto c=ecs::ComponentRegistry::Builtins().Capture(world,e);c.erase("SceneIdentity");
        if(e!=root&&c.contains("PrefabInstance"))throw std::runtime_error("Nested prefabs are not supported");c.erase("PrefabInstance");Remap(c,ids);
        Json n={{"id",ids.at(SceneSerializer::EntitySceneId(world,e))},{"components",c}};
        if(e!=root)n["parent"]=ids.at(SceneSerializer::EntitySceneId(world,world.Parent(e)));nodes.push_back(std::move(n));
    }
    Json data={{"version",2},{"root",ids.at(SceneSerializer::EntitySceneId(world,root))},{"entities",nodes}};ValidatePrefab(data);return data;
}
void LinkPrefab(ecs::World& world,ecs::Entity root,const std::string& key,const nlohmann::json& source){
    auto& link=world.AddComponent<ecs::PrefabInstanceComponent>(root);link.asset=key;link.members=Json::object();
    const auto entities=world.Subtree(root);for(std::size_t i=0;i<entities.size();++i)link.members[source.at("entities")[i].at("id").get<std::string>()]={{"$entity",SceneSerializer::EntitySceneId(world,entities[i])}};
}
ecs::Entity InstantiatePrefab(ecs::World& world,const std::string& key,const nlohmann::json& data,ecs::Vec3 position){
    world.AssertOwnerThread();if(world.IsIterating())throw std::runtime_error("Spawn prefab only at a phase boundary");ValidatePrefab(data);
    if(!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z))throw std::runtime_error("Nonfinite spawn position");
    Map ids;std::unordered_map<std::string,ecs::Entity> entities;std::vector<ecs::Entity> created;ecs::Entity root;
    for(const auto& n:data.at("entities"))ids.emplace(n.at("id").get<std::string>(),ecs::NewSceneIdentity());
    try{
        for(const auto& n:data.at("entities")){
            auto id=n.at("id").get<std::string>();auto e=world.CreateEntity();created.push_back(e);entities.emplace(id,e);
            auto c=n.at("components");if(!data.value("_legacySceneReferences",false))Remap(c,ids);ecs::ComponentRegistry::Builtins().Restore(world,e,c);world.AddComponent<ecs::SceneIdentityComponent>(e).value=ids.at(id);
        }
        std::vector<std::pair<ecs::Entity,ecs::Entity>> parents;for(const auto& n:data.at("entities"))if(n.contains("parent"))parents.emplace_back(entities.at(n.at("id").get<std::string>()),entities.at(n.at("parent").get<std::string>()));
        std::string error;if(!world.SetParents(parents,false,&error))throw std::runtime_error(error);
        root=entities.at(data.at("root").get<std::string>());auto pose=world.LocalTransform(root);pose.position=position;if(!world.SetLocalTransform(root,pose,&error))throw std::runtime_error(error);
        auto& link=world.AddComponent<ecs::PrefabInstanceComponent>(root);link.asset=key;for(const auto& [local,id]:ids)link.members[local]={{"$entity",id}};
        return root;
    }catch(...){for(auto e:created)if(world.IsAlive(e))world.DestroyEntity(e);throw;}
}
nlohmann::json InstanceSceneNodes(ecs::World& world,ecs::Entity root,const nlohmann::json& source){
    ValidatePrefab(source);auto* link=world.GetComponent<ecs::PrefabInstanceComponent>(root);if(!link)throw std::runtime_error("Select a linked prefab instance");
    Map ids;for(auto it=link->members.begin();it!=link->members.end();++it)ids.emplace(it.key(),it.value().at("$entity").get<std::string>());
    auto current=CapturePrefab(world,root);
    if(current.at("root")!=source.at("root")||current.at("entities").size()!=ids.size())throw std::runtime_error("Local child additions/deletions are not supported by Apply/Revert; undo structural overrides first");
    std::unordered_set<std::string> currentIds;for(const auto& n:current.at("entities"))currentIds.insert(n.at("id").get<std::string>());
    for(const auto& [local,id]:ids)if(!currentIds.contains(local))throw std::runtime_error("Prefab member identity changed; undo structural overrides first");
    Map next;for(const auto& n:source.at("entities")){auto local=n.at("id").get<std::string>();next.emplace(local,ids.contains(local)?ids.at(local):ecs::NewSceneIdentity());}
    Json members=Json::object();for(const auto& [local,id]:next)members[local]={{"$entity",id}};
    auto nodes=source.at("entities");
    for(auto& n:nodes){auto local=n.at("id").get<std::string>();auto& c=n["components"];if(!source.value("_legacySceneReferences",false))Remap(c,next);n["id"]=next.at(local);
        if(n.contains("parent"))n["parent"]=next.at(n.at("parent").get<std::string>());
        if(local==source.at("root").get<std::string>()){
            const auto desired=c["Transform"].value("pivot",Json::array({0,0,0}));const ecs::Vec3 pivot{desired.at(0).get<float>(),desired.at(1).get<float>(),desired.at(2).get<float>()};
            const auto pose=ecs::RebasePivot(world.LocalTransform(root),pivot);
            c["Transform"]=ecs::ComponentRegistry::Builtins().Capture(world,root).at("Transform");c["Transform"]["position"]={pose.position.x,pose.position.y,pose.position.z};
            if(ecs::HasPivot(pose))c["Transform"]["pivot"]={pivot.x,pivot.y,pivot.z};else c["Transform"].erase("pivot");
            c["PrefabInstance"]={{"asset",link->asset},{"members",members}};
            if(auto parent=world.Parent(root);parent.IsValid())n["parent"]=SceneSerializer::EntitySceneId(world,parent);
        }
    }
    for(const auto& [local,id]:ids)if(!next.contains(local))nodes.push_back({{"id",id},{"removed",true}});
    return nodes;
}
ecs::Entity SpawnPrefab(ecs::World& world,const std::string& key,ecs::Vec3 position){return InstantiatePrefab(world,key,ReadPrefab(key),position);}
}
