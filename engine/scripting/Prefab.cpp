#include "Prefab.h"
#include "../core/AssetPaths.h"
#include "../ecs/ComponentRegistry.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include "../ecs/components/TransformComponent.h"
#include <fstream>
#include <cmath>
namespace scripting {
std::filesystem::path SafeAsset(const std::string& key,const std::string& suffix){
    std::string error;
    if(key.empty()||!key.ends_with(suffix)||!ecs::ComponentRegistry::ValidateAssetKey(key,ecs::AssetKind::None,&error)||key.find('\0')!=std::string::npos)
        throw std::runtime_error("Invalid asset key: "+key);
    const auto assetRoot=AssetPaths::ResolveAssetRoot();
    if(assetRoot.empty())throw std::runtime_error("Project asset root is unavailable");
    const auto root=std::filesystem::weakly_canonical(assetRoot);
    const auto path=std::filesystem::weakly_canonical(root/std::filesystem::u8path(key));
    const auto relative=path.lexically_relative(root);
    if(relative.empty()||relative.is_absolute()||*relative.begin()=="..")throw std::runtime_error("Asset escapes root: "+key);
    return path;
}
ecs::Entity SpawnPrefab(ecs::World& world,const std::string& key,ecs::Vec3 position){
    world.AssertOwnerThread();
    if(world.IsIterating())throw std::logic_error("Spawn prefab only at a phase boundary");
    if(!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z))throw std::runtime_error("Nonfinite spawn position");
    const auto path=SafeAsset(key,".prefab.json");
    if(std::filesystem::file_size(path)>1024*1024)throw std::runtime_error("Prefab exceeds 1 MiB");
    std::ifstream stream(path);nlohmann::json data;stream>>data;
    if(data.at("version")!=1)throw std::runtime_error("Expected prefab version 1");
    auto components=data.at("components");components.erase("SceneIdentity");
    // Validate completely in a temporary World before touching the destination.
    ecs::World staged;auto test=staged.CreateEntity();auto& registry=ecs::ComponentRegistry::Builtins();registry.Restore(staged,test,components);
    auto entity=world.CreateEntity();
    try{
        registry.Restore(world,entity,components);
        world.AddComponent<ecs::SceneIdentityComponent>(entity);
        auto* transform=world.GetComponent<ecs::TransformComponent>(entity);
        if(!transform)transform=&world.AddComponent<ecs::TransformComponent>(entity);
        transform->position=position;return entity;
    }catch(...){world.DestroyEntity(entity);throw;}
}
}
