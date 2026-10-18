#pragma once
#include "../ecs/HierarchyEdit.h"
namespace editor {
// Short-lived value view. No borrowed components or second transform cache.
struct HierarchyTransformView {
    ecs::Entity entity,parent;
    std::string id,name,parentName;
    ecs::TransformComponent local;
    ecs::Matrix4 matrix;
    std::optional<ecs::TransformComponent> worldTrs;
};
std::optional<HierarchyTransformView> ReadHierarchyTransformView(const ecs::World&,ecs::Entity,std::string* error=nullptr);
bool SameLocalTransform(const ecs::TransformComponent&,const ecs::TransformComponent&);
void ValidateLocalTransform(const ecs::TransformComponent&);
}
