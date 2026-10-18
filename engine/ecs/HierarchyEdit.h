#pragma once
#include "Hierarchy.h"
#include <functional>
namespace ecs {
// Value snapshots refer to the original World/generation and stable ID together.
struct HierarchyEditState {
    Entity entity;std::string id;TransformComponent local;
    std::optional<RelationshipComponent> relationship;
};
struct HierarchyEdit {
    static std::vector<HierarchyEditState> Capture(const World&,const std::vector<Entity>&);
    static std::vector<HierarchyEditState> PrepareReparent(const World&,const std::vector<Entity>&,Entity parent);
    // Candidate validation and all allocation precede the nonthrowing commit.
    // No World publication, entity recreation or changes to unrelated components.
    static void Apply(World&,const std::vector<HierarchyEditState>&,
        const std::function<void(World&)>& validate={});
};
}
