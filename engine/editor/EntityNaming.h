#pragma once
#include "../ecs/World.h"
#include "../ecs/components/TagComponent.h"
#include <charconv>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace editor {
// One index per editor operation. Reservations include earlier members of a batch.
// Successor links skip occupied suffix ranges without rescanning them for each copy.
class EntityNames {
    struct Siblings {
        std::unordered_set<std::string> used;
        std::unordered_map<std::string,std::unordered_map<std::uint64_t,std::uint64_t>> successors;
    };
    std::unordered_map<ecs::Entity,Siblings,ecs::EntityHash> parents;
public:
    explicit EntityNames(const ecs::World& world,const std::vector<ecs::Entity>& excluded={}) {
        std::unordered_set<ecs::Entity,ecs::EntityHash> skip(excluded.begin(),excluded.end());
        for(auto entity:world.Entities())if(!skip.contains(entity))
            if(auto* tag=world.GetComponent<ecs::TagComponent>(entity))parents[world.Parent(entity)].used.insert(tag->name);
    }
    std::string Claim(std::string name,ecs::Entity parent={},bool duplicate=false) {
        auto& siblings=parents[parent];
        if(!duplicate&&siblings.used.insert(name).second)return name;
        while(name.ends_with(" Copy"))name.resize(name.size()-5);
        if(name.empty())name="Entity";
        auto digits=name.find_last_not_of("0123456789");
        auto start=digits==std::string::npos?0:digits+1;
        std::uint64_t number=0;
        if(start<name.size()) {
            auto result=std::from_chars(name.data()+start,name.data()+name.size(),number);
            if(result.ec==std::errc{}&&number<std::numeric_limits<std::uint64_t>::max())name.resize(start);
            else number=0; // An unrepresentable suffix is treated as literal user text.
        }
        ++number;
        auto& jumps=siblings.successors[name];std::vector<std::uint64_t> path;
        while(siblings.used.contains(name+std::to_string(number))) {
            path.push_back(number);auto it=jumps.find(number);
            if(it!=jumps.end()&&it->second>number)number=it->second;
            else {if(number==std::numeric_limits<std::uint64_t>::max())throw std::runtime_error("Entity name suffix exhausted");++number;}
        }
        for(auto previous:path)jumps[previous]=number;
        auto result=name+std::to_string(number);siblings.used.insert(result);return result;
    }
};
}
