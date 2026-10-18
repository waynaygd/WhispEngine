#include "HierarchyTree.h"
#include "../ecs/Hierarchy.h"
#include "../ecs/components/TagComponent.h"
#include <algorithm>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace editor {
namespace {
auto LowerName(const std::string& text){
#ifdef _WIN32
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
    if(!count)return std::wstring(text.begin(),text.end());
    std::wstring wide(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),wide.data(),count);
    const int length=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,wide.data(),count,nullptr,0,nullptr,nullptr,0);
    if(!length)return wide;std::wstring lower(length,L'\0');
    LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,wide.data(),count,lower.data(),length,nullptr,nullptr,0);return lower;
#else
    auto lower=text;for(auto& c:lower)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return lower;
#endif
}
}
std::vector<HierarchyTreeRow> BuildHierarchyTreeRows(const ecs::World& world,const ecs::HierarchyGraph& graph,HierarchyTreeState& state){
    world.AssertOwnerThread();if(graph.world!=world.GetIdentity())throw std::logic_error("Hierarchy view: foreign graph snapshot");
    if(state.world!=graph.world){state={};state.world=graph.world;}
    std::unordered_set<std::string_view> parents;parents.reserve(graph.nodes.size());
    for(const auto& node:graph.nodes)if(!node.children.empty())parents.insert(node.id);
    std::erase_if(state.collapsed,[&](const auto& id){return !parents.contains(id);});
    std::vector<std::size_t> depth(graph.nodes.size());std::vector<bool> visible(graph.nodes.size());
    const bool searching=!state.filter.empty();std::vector<bool> included(graph.nodes.size(),!searching);
    if(searching){const auto filter=LowerName(state.filter);
        for(std::size_t i=0;i<graph.nodes.size();++i){const auto& node=graph.nodes[i];auto* tag=world.GetComponent<ecs::TagComponent>(node.entity);
            const auto name=LowerName(tag&&!tag->name.empty()?tag->name:node.id);included[i]=name.find(filter)!=decltype(name)::npos;
        }
        for(auto it=graph.preorder.rbegin();it!=graph.preorder.rend();++it)if(included[*it]&&graph.nodes[*it].parent)included[*graph.nodes[*it].parent]=true;
    }
    std::vector<HierarchyTreeRow> rows;rows.reserve(graph.nodes.size());
    for(auto index:graph.preorder){const auto& node=graph.nodes[index];
        depth[index]=node.parent?depth[*node.parent]+1:0;
        visible[index]=included[index]&&(!node.parent||(visible[*node.parent]&&(searching||!state.collapsed.contains(graph.nodes[*node.parent].id))));
        if(visible[index])rows.push_back({node.entity,index,depth[index],!node.children.empty()});
    }return rows;
}
}
