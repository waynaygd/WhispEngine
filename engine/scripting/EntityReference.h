#pragma once
#include "../ecs/World.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include <nlohmann/json.hpp>
namespace scripting {
// Scene-local persistent key, never an ECS slot, generation or pointer.
inline bool ValidEntityReference(const nlohmann::json& value){
    return value.is_null()||(value.is_object()&&value.size()==1&&value.contains("$entity")&&value["$entity"].is_string()&&!value["$entity"].get_ref<const std::string&>().empty()&&value["$entity"].get_ref<const std::string&>().size()<=1024&&value["$entity"].get_ref<const std::string&>().find('\0')==std::string::npos);
}
inline ecs::Entity ResolveEntityReference(const ecs::World& world,const nlohmann::json& value){
    if(!ValidEntityReference(value)||value.is_null())return {};
    ecs::Entity result{};
    for(auto e:world.Entities())if(auto* id=world.GetComponent<ecs::SceneIdentityComponent>(e);id&&id->value==value["$entity"].get_ref<const std::string&>()){
        if(result.IsValid())return {}; // Ambiguous identities fail closed.
        result=e;
    }
    return result;
}
}
