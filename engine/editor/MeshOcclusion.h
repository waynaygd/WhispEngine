#pragma once
#include "../ecs/MathTypes.h"
#include "../resources/MeshData.h"
#include <array>
#include <vector>
namespace editor {
// Local-space, exact triangle occlusion for editor overlays. Cached per resource version,
// not per entity: transformed/instanced meshes share the same immutable tree.
class MeshOcclusion {
 struct Node {ecs::Vec3 lo,hi;unsigned first=0,count=0,left=0,right=0;};
 std::vector<Node> nodes;
 std::vector<unsigned> triangles;
public:
 explicit MeshOcclusion(const MeshData&);
 // Keep the inverse-transformed direction unnormalized: t remains a world distance.
 bool AnyHit(const MeshData&,ecs::Vec3 origin,ecs::Vec3 direction,float maximum,
             std::size_t* triangleTests=nullptr)const;
};
}
