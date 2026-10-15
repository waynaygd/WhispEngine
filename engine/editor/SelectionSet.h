#pragma once
#include "../ecs/World.h"
#include <algorithm>
#include <vector>
namespace editor {
// Ordered selection; full World/generation handles, never bare entity indices.
struct SelectionSet {
    std::vector<ecs::Entity> items;
    ecs::Entity active{},anchor{};
    bool Contains(ecs::Entity e)const{return std::find(items.begin(),items.end(),e)!=items.end();}
    void Clear(){items.clear();active=anchor={};}
    void Replace(ecs::Entity e){Clear();if(e.IsValid()){items.push_back(e);active=anchor=e;}}
    void Add(ecs::Entity e){if(!e.IsValid())return;if(!Contains(e))items.push_back(e);active=anchor=e;}
    void Toggle(ecs::Entity e){if(!Contains(e)){Add(e);return;}std::erase(items,e);anchor=e;if(active==e)active=items.empty()?ecs::Entity{}:items.back();}
    void Prune(const ecs::World& world){std::erase_if(items,[&](auto e){return !world.IsAlive(e);});if(!Contains(active))active=items.empty()?ecs::Entity{}:items.back();if(!world.IsAlive(anchor))anchor={};}
    void Range(const std::vector<ecs::Entity>& order,ecs::Entity e,bool append){
        auto end=std::find(order.begin(),order.end(),e);if(end==order.end())return;
        auto begin=std::find(order.begin(),order.end(),anchor);if(begin==order.end())begin=end;
        auto savedAnchor=*begin;if(!append)items.clear();auto lo=std::min(begin,end),hi=std::max(begin,end);
        for(auto i=lo;i<=hi;++i)if(!Contains(*i))items.push_back(*i);active=e;anchor=savedAnchor;
    }
    void Remap(ecs::Entity from,ecs::Entity to){for(auto& e:items)if(e==from)e=to;if(active==from)active=to;if(anchor==from)anchor=to;}
};
}
