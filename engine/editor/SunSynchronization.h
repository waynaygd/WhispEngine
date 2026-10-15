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
inline float DaylightFactor(float elevation){float t=std::clamp((elevation+.10f)/.25f,0.f,1.f);return t*t*(3-2*t);}
inline bool SynchronizeSun(ecs::World& world,const SceneBackground& b,float rotation){
 if(b.sunSync!=SunSyncMode::SkyDrivesLight&&!(b.background==EditorBackground::DayNight&&b.driveLightProperties))return false;
 auto entity=PrimarySun(world);if(!entity.IsValid())return false;auto* t=world.GetComponent<ecs::TransformComponent>(entity);bool changed=false;
 if(b.sunSync==SunSyncMode::SkyDrivesLight&&b.sunMetadata){auto d=MetadataSun(b,rotation);ecs::Vec3 r{std::asin(std::clamp(d.y,-1.f,1.f)),std::atan2(-d.x,-d.z),0};
  if(std::abs(t->rotation.x-r.x)>1e-6f||std::abs(t->rotation.y-r.y)>1e-6f||std::abs(t->rotation.z-r.z)>1e-6f){t->rotation=r;changed=true;}
 }
 if(b.background==EditorBackground::DayNight&&b.driveLightProperties){auto rays=ecs::LightDirection(ecs::RenderPose::From(*t));float day=DaylightFactor(-rays.y),warm=std::clamp((-rays.y-.02f)/.35f,0.f,1.f);auto* light=world.GetComponent<ecs::DirectionalLightComponent>(entity);auto old=light->color;float intensity=b.dayLightIntensity*day;std::array<float,3> color{1,.45f+.53f*warm,.22f+.68f*warm};changed|=old!=color||light->intensity!=intensity;light->color=color;light->intensity=intensity;}
 return changed;
}
}
