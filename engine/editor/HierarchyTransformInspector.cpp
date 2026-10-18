#include "HierarchyTransformInspector.h"
#include "../ecs/components/TagComponent.h"
#include <tuple>
namespace editor {
bool SameLocalTransform(const ecs::TransformComponent& a,const ecs::TransformComponent& b){
    return std::tie(a.position.x,a.position.y,a.position.z,a.rotation.x,a.rotation.y,a.rotation.z,a.scale.x,a.scale.y,a.scale.z)==
           std::tie(b.position.x,b.position.y,b.position.z,b.rotation.x,b.rotation.y,b.rotation.z,b.scale.x,b.scale.y,b.scale.z);
}
void ValidateLocalTransform(const ecs::TransformComponent& value){
    if(!ecs::Finite(value))throw std::runtime_error("Local Transform must contain finite values");
    // Same finite vector range as existing persisted Transform properties.
    for(auto v:{value.position,value.rotation,value.scale})
        for(float n:{v.x,v.y,v.z})if(std::abs(n)>1e6f)throw std::runtime_error("Local Transform exceeds the supported numeric range (+/-1e6)");
}
std::optional<HierarchyTransformView> ReadHierarchyTransformView(const ecs::World& world,ecs::Entity entity,std::string* error){
    auto graph=ecs::HierarchyGraph::Build(world,error);if(!graph)return {};
    auto at=graph->entities.find(entity);
    if(at==graph->entities.end()||!graph->nodes[at->second].local||!graph->nodes[at->second].matrix){if(error)*error="Select a live Entity with Transform";return {};}
    const auto& node=graph->nodes[at->second];HierarchyTransformView view;
    view.entity=entity;view.id=node.id;view.local=*node.local;view.matrix=*node.matrix;
    auto name=[&](const ecs::HierarchyGraph::Node& n){auto* tag=world.GetComponent<ecs::TagComponent>(n.entity);return tag&&!tag->name.empty()?tag->name:n.id;};
    view.name=name(node);view.parentName="Scene Root";
    if(node.parent){const auto& parent=graph->nodes[*node.parent];view.parent=parent.entity;view.parentName=name(parent);}
    view.worldTrs=ecs::DecomposeTRS(view.matrix,view.local.scale);return view;
}
}
