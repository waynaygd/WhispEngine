#pragma once
#include "../ecs/Entity.h"
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>
namespace ecs {class World;struct HierarchyGraph;}
namespace editor {
// Presentation only. Parent/order/children remain exclusively in ECS/graph.
struct HierarchyTreeState {
    std::uint64_t world=0;
    std::unordered_set<std::string> collapsed;
    std::string filter;
};
struct HierarchyTreeRow {ecs::Entity entity;std::size_t node=0,depth=0;bool children=false;};
std::vector<HierarchyTreeRow> BuildHierarchyTreeRows(const ecs::World&,const ecs::HierarchyGraph&,HierarchyTreeState&);
}
