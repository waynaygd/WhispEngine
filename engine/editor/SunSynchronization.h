#pragma once
#include "../ecs/LightGather.h"
#include "../render/EditorVisuals.h"
namespace editor {
inline ecs::Entity PrimarySun(ecs::World& world){
 ecs::Entity chosen;bool primary=false;
 world.ForEach<ecs::TransformComponent,ecs::DirectionalLightComponent>([&](ecs::Entity e,auto&,auto& l){
  if(!l.enabled)return;if(!chosen.IsValid()||(!primary&&l.primary)||(primary==l.primary&&e.index<chosen.index)){chosen=e;primary=l.primary;}
 });return chosen;
}
// Metadata is a unit vector from the observer toward the source in unrotated sky space.
// Yaw follows the sky shader's world-to-texture sampling convention.
inline ecs::Vec3 MetadataSun(const SceneBackground& b,float rotation){
 auto d=b.sunDirection;float length=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);if(!std::isfinite(length)||length<1e-6f)return {0,1,0};
 float angle=rotation*.01745329252f,c=std::cos(angle),s=std::sin(angle);return {(c*d[0]-s*d[2])/length,d[1]/length,(s*d[0]+c*d[2])/length};
}
inline bool SynchronizeSun(ecs::World& world,const SceneBackground& b,float rotation){
 if(b.sunSync!=SunSyncMode::SkyDrivesLight||!b.sunMetadata)return false;auto entity=PrimarySun(world);if(!entity.IsValid())return false;
 auto d=MetadataSun(b,rotation);auto* t=world.GetComponent<ecs::TransformComponent>(entity);
 ecs::Vec3 r{std::asin(std::clamp(d.y,-1.f,1.f)),std::atan2(-d.x,-d.z),0};
 if(std::abs(t->rotation.x-r.x)<1e-6f&&std::abs(t->rotation.y-r.y)<1e-6f&&std::abs(t->rotation.z-r.z)<1e-6f)return false;t->rotation=r;return true;
}
}
