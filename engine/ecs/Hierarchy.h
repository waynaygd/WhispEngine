#pragma once
#include "World.h"
#include "TransformMath.h"
#include "components/RelationshipComponent.h"
#include <unordered_map>
namespace ecs {
// Immutable, short-lived value snapshot, built anew after mutations. No cache.
struct HierarchyGraph {
    struct Node {
        Entity entity{};std::string id;std::optional<std::size_t> parent;
        std::uint64_t order=0;std::vector<std::size_t> children;
        std::optional<TransformComponent> local;std::optional<Matrix4> matrix;
        bool relationship=false;
    };
    std::uint64_t world=0;
    std::vector<Node> nodes;
    std::vector<std::size_t> roots,preorder;
    std::unordered_map<Entity,std::size_t,EntityHash> entities;
    static std::optional<HierarchyGraph> Build(const World&,std::string* error=nullptr,
        Entity overrideEntity={},const TransformComponent* overrideLocal=nullptr,
        const RelationshipComponent* overrideRelationship=nullptr);
    std::optional<std::vector<Entity>> Closure(const std::vector<Entity>&,bool postorder=false,std::string* error=nullptr)const;
};
bool ValidateHierarchy(const World&,std::string* error=nullptr);
Entity GetParent(const World&,Entity,std::string* error=nullptr);
std::optional<std::vector<Entity>> GetChildren(const World&,Entity,std::string* error=nullptr);
std::optional<bool> IsAncestor(const World&,Entity ancestor,Entity descendant,std::string* error=nullptr);
inline std::optional<bool> IsDescendant(const World& w,Entity child,Entity ancestor,std::string* error=nullptr){return IsAncestor(w,ancestor,child,error);}
bool SetParent(World&,Entity child,Entity parent,std::string* error=nullptr); // Keep World only
bool ClearParent(World&,Entity child,std::string* error=nullptr); // Keep World only
std::optional<Matrix4> EvaluateHierarchyMatrix(const World&,Entity,std::string* error=nullptr);
std::optional<TransformComponent> ReadHierarchyTransform(const World&,Entity);
bool WriteHierarchyMatrix(World&,Entity,const Matrix4&,std::string* error=nullptr);
bool WriteHierarchyTransform(World&,Entity,const TransformComponent&,std::string* error=nullptr);
struct HierarchyClonePlan {
    std::vector<Entity> sources;
    std::unordered_map<std::string,std::string> identities;
};
std::optional<HierarchyClonePlan> PrepareHierarchyClone(const World&,const std::vector<Entity>&,std::string* error=nullptr);
RelationshipComponent RemapRelationship(RelationshipComponent,const std::unordered_map<std::string,std::string>&);
}
