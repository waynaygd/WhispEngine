#include "HierarchyEdit.h"
#include "components/SceneIdentityComponent.h"
#include <unordered_set>
namespace ecs {
namespace {
void RequireEditable(const World& world){
    world.AssertOwnerThread();
    if(!world.IsHierarchyData()||world.IsIterating()||world.IsRetiringHierarchy()||!world.CanAttachHierarchyLifecycle(nullptr))
        throw std::runtime_error("Hierarchy edit requires an unowned authoring World at a phase boundary");
}
}
std::vector<HierarchyEditState> HierarchyEdit::Capture(const World& world,const std::vector<Entity>& entities){
    std::vector<HierarchyEditState> result;result.reserve(entities.size());
    for(auto e:entities){
        auto* t=world.GetComponent<TransformComponent>(e);auto* id=world.GetComponent<SceneIdentityComponent>(e);
        if(!t||!id)throw std::runtime_error("Hierarchy edit contains a stale/foreign Entity or absent Transform/identity");
        HierarchyEditState state{e,id->value,*t,{}};
        if(auto* r=world.GetComponent<RelationshipComponent>(e))state.relationship=*r;
        result.push_back(std::move(state));
    }return result;
}
std::vector<HierarchyEditState> HierarchyEdit::PrepareReparent(const World& world,const std::vector<Entity>& selected,Entity parent){
    RequireEditable(world);std::string error;auto graph=HierarchyGraph::Build(world,&error);if(!graph)throw std::runtime_error(error);
    std::unordered_set<Entity,EntityHash> included;
    for(auto e:selected){if(!graph->entities.contains(e))throw std::runtime_error("Stale/foreign drag source");included.insert(e);}
    if(parent.IsValid()&&(!graph->entities.contains(parent)||!graph->nodes[graph->entities.at(parent)].matrix))throw std::runtime_error("Stale/foreign target or absent Transform");
    std::vector<Entity> roots;
    std::vector<bool> ancestor(graph->nodes.size());
    for(auto i:graph->preorder){const auto& n=graph->nodes[i];ancestor[i]=n.parent&&(ancestor[*n.parent]||included.contains(graph->nodes[*n.parent].entity));if(included.contains(n.entity)&&!ancestor[i])roots.push_back(n.entity);}
    // Reject targets anywhere inside any selected effective subtree, even if
    // sequential edits could temporarily hide the cycle.
    if(parent.IsValid())for(auto i=std::optional(graph->entities.at(parent));i;i=graph->nodes[*i].parent)
        if(included.contains(graph->nodes[*i].entity))throw std::runtime_error("Target is inside selected subtree (self parent/cycle)");
    auto result=Capture(world,roots);std::vector<HierarchyEditState> changed;
    auto candidate=world.CloneData();
    const auto target=parent.IsValid()?candidate->LocalEntity(parent.index):Entity{};
    for(auto& state:result){
        const auto& n=graph->nodes[graph->entities.at(state.entity)];
        const auto oldParent=n.parent?graph->nodes[*n.parent].entity:Entity{};
        if(oldParent==parent)continue;
        auto e=candidate->LocalEntity(state.entity.index);
        if(!(target.IsValid()?SetParent(*candidate,e,target,&error):ClearParent(*candidate,e,&error)))throw std::runtime_error(state.id+": "+error);
        auto next=Capture(*candidate,{e}).front();next.entity=state.entity;changed.push_back(std::move(next));
    }
    auto finalGraph=HierarchyGraph::Build(*candidate,&error);if(!finalGraph)throw std::runtime_error(error);
    for(const auto& node:graph->nodes)if(node.matrix){
        const auto& after=finalGraph->nodes[finalGraph->entities.at(candidate->LocalEntity(node.entity.index))].matrix;
        if(!after)throw std::runtime_error(node.id+": lost world matrix");
        for(int i=0;i<16;++i)if(std::abs((*after)[i]-(*node.matrix)[i])>2e-5*std::max({1.,std::abs((*after)[i]),std::abs((*node.matrix)[i])}))
            throw std::runtime_error(node.id+": Keep World matrix reconstruction exceeds tolerance");
    }
    return changed;
}
void HierarchyEdit::Apply(World& world,const std::vector<HierarchyEditState>& states,const std::function<void(World&)>& validate){
    RequireEditable(world);if(states.empty())return;
    auto candidate=world.CloneData();std::unordered_set<Entity,EntityHash> unique;
    std::vector<TransformComponent*> targets;targets.reserve(states.size());
    for(const auto& state:states){
        auto* id=world.GetComponent<SceneIdentityComponent>(state.entity);auto* t=world.GetComponent<TransformComponent>(state.entity);
        if(!id||id->value!=state.id||!t||!unique.insert(state.entity).second)throw std::runtime_error("Stale/foreign/duplicate graph transaction Entity: "+state.id);
        targets.push_back(t);auto e=candidate->LocalEntity(state.entity.index);
        *candidate->GetComponent<TransformComponent>(e)=state.local;
        candidate->RemoveComponent<RelationshipComponent>(e);
        if(state.relationship)candidate->AddComponent<RelationshipComponent>(e,*state.relationship);
    }
    candidate->RequireRuntimeWorld();if(validate)validate(*candidate);candidate->RequireRuntimeWorld();
    // Clone only the Relationship storage for publication. Original Transform
    // addresses and all other storage/handles/World identity remain unchanged.
    auto prepared=candidate->GetOrCreateStorage<RelationshipComponent>().Clone();
    world.GetOrCreateStorage<RelationshipComponent>(); // Last potentially allocating operation.
    auto& slot=world.m_ComponentStorages.at(std::type_index(typeid(RelationshipComponent)));
    static_assert(std::is_nothrow_copy_assignable_v<TransformComponent>);
    // Owner-thread, outside iteration/lifecycle, no callbacks or allocations.
    // Intermediate graph cannot be observed by any runtime consumer.
    slot.swap(prepared);
    for(std::size_t i=0;i<states.size();++i)*targets[i]=states[i].local;
}
}
