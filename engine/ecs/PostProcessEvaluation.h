#pragma once
#include "World.h"
#include "components/PostProcessVolumeComponent.h"
#include <optional>
namespace ecs {
struct PostProcessBox {Vec3 center;std::array<Vec3,3> axes;std::array<float,3> half;};
std::optional<PostProcessBox> GetPostProcessBox(const World&,Entity,const PostProcessVolumeComponent&);
float PostProcessSpatialWeight(Vec3,const PostProcessBox&,float radius);
struct EvaluatedPostProcess : PostProcessSettings {unsigned activeVolumes=0,invalidVolumes=0;};
EvaluatedPostProcess EvaluatePostProcess(Vec3 cameraPosition,const World&,const PostProcessSettings& baseSettings);
}
