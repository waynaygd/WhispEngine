#pragma once
#include "../ecs/LightGather.h"
#include "../ecs/HierarchyPhysics.h"
#include "../render/EditorVisuals.h"
namespace editor {
inline ecs::Entity PrimarySun(ecs::World& world){
 ecs::Entity chosen;bool primary=false;
 auto graph=world.IsHierarchyData()?ecs::HierarchyGraph::Build(world):std::optional<ecs::HierarchyGraph>{};
 if(world.IsHierarchyData()&&!graph)throw std::runtime_error("Sun: invalid hierarchy");
 world.ForEach<ecs::TransformComponent,ecs::DirectionalLightComponent>([&](ecs::Entity e,auto&,auto& l){
  if(!l.enabled)return;const bool earlier=chosen.IsValid()&&(graph?graph->nodes[graph->entities.at(e)].id<graph->nodes[graph->entities.at(chosen)].id:e.index<chosen.index);
  if(!chosen.IsValid()||(!primary&&l.primary)||(primary==l.primary&&earlier)){chosen=e;primary=l.primary;}
 });return chosen;
}
// Metadata is a unit vector from the observer toward the source in unrotated sky space.
// Yaw follows the sky shader's world-to-texture sampling convention.
inline ecs::Vec3 MetadataSun(const SceneBackground& b,float rotation){
 auto d=b.sunDirection;float length=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);if(!std::isfinite(length)||length<1e-6f)return {0,1,0};
 float angle=rotation*.01745329252f,c=std::cos(angle),s=std::sin(angle);return {(c*d[0]-s*d[2])/length,d[1]/length,(s*d[0]+c*d[2])/length};
}
inline float DaylightFactor(float elevation){float t=std::clamp((elevation+.10f)/.25f,0.f,1.f);return t*t*(3-2*t);}
inline bool SynchronizeHierarchySun(ecs::World& world,const SceneBackground& b,float rotation){
 if(b.sunSync!=SunSyncMode::SkyDrivesLight&&!(b.background==EditorBackground::DayNight&&b.driveLightProperties))return false;
 auto entity=PrimarySun(world);if(!entity.IsValid())return false;
 const auto graph=ecs::RequireHierarchyRuntime(world);auto index=graph.entities.at(entity);
 const auto label="Hierarchy sun Entity "+graph.nodes[index].id+": ";
 // LightGather uses the authored rotation chain. Positive uniform scale makes
 // checked matrix world rotation agree with that chain; reject other affine
 // paths before modifying either the local Transform or light properties.
 for(auto current=std::optional<std::size_t>(index);current;current=graph.nodes[*current].parent){
  auto local=graph.nodes[*current].local;if(!local)continue;auto s=local->scale;
  if(!(s.x>0&&s.y>0&&s.z>0&&std::abs(s.x-s.y)<=1e-5f*s.x&&std::abs(s.x-s.z)<=1e-5f*s.x))throw std::runtime_error(label+"unsupported affine scale in sun ancestry (A3 conservative guard)");
 }
 auto t=ecs::ReadWorldTransform(world,entity);if(!t)throw std::runtime_error(label+"world orientation is not representable");
 if(b.sunSync==SunSyncMode::SkyDrivesLight&&!b.sunMetadata)throw std::runtime_error(label+"SkyDrivesLight requires sun metadata");
 auto* local=world.GetComponent<ecs::TransformComponent>(entity);const auto saved=*local;bool changed=false;
 try{
  if(b.sunSync==SunSyncMode::SkyDrivesLight){auto d=MetadataSun(b,rotation);ecs::Vec3 desired{std::asin(std::clamp(d.y,-1.f,1.f)),std::atan2(-d.x,-d.z),0};
   if(std::abs(t->rotation.x-desired.x)>1e-6f||std::abs(t->rotation.y-desired.y)>1e-6f||std::abs(t->rotation.z-desired.z)>1e-6f){
    std::string error;if(!ecs::TrySetWorldRotation(world,entity,desired,&error))throw std::runtime_error(error);
    world.RequireRuntimeWorld();t=ecs::ReadWorldTransform(world,entity);if(!t)throw std::runtime_error("checked orientation read failed");changed=true;
   }
  }
 }catch(const std::exception& e){*local=saved;throw std::runtime_error(label+e.what());}
 if(b.background==EditorBackground::DayNight&&b.driveLightProperties){auto rays=ecs::LightDirection(ecs::RenderPose::From(*t));float day=DaylightFactor(-rays.y),warm=std::clamp((-rays.y-.02f)/.35f,0.f,1.f);auto* light=world.GetComponent<ecs::DirectionalLightComponent>(entity);
  const std::array<float,3> color{1,.45f+.53f*warm,.22f+.68f*warm};float intensity=b.dayLightIntensity*day;changed|=light->color!=color||light->intensity!=intensity;light->color=color;light->intensity=intensity;
 }return changed;
}
inline bool SynchronizeSun(ecs::World& world,const SceneBackground& b,float rotation){
 if(world.IsHierarchyData())return SynchronizeHierarchySun(world,b,rotation);
 if(b.sunSync!=SunSyncMode::SkyDrivesLight&&!(b.background==EditorBackground::DayNight&&b.driveLightProperties))return false;
 auto entity=PrimarySun(world);if(!entity.IsValid())return false;auto t=ecs::ReadWorldTransform(world,entity);bool changed=false;
 if(b.sunSync==SunSyncMode::SkyDrivesLight&&b.sunMetadata){auto d=MetadataSun(b,rotation);ecs::Vec3 r{std::asin(std::clamp(d.y,-1.f,1.f)),std::atan2(-d.x,-d.z),0};
  if(std::abs(t->rotation.x-r.x)>1e-6f||std::abs(t->rotation.y-r.y)>1e-6f||std::abs(t->rotation.z-r.z)>1e-6f){if(ecs::TrySetWorldRotation(world,entity,r)){t->rotation=r;changed=true;}}
 }
 if(b.background==EditorBackground::DayNight&&b.driveLightProperties){auto rays=ecs::LightDirection(ecs::RenderPose::From(*t));float day=DaylightFactor(-rays.y),warm=std::clamp((-rays.y-.02f)/.35f,0.f,1.f);auto* light=world.GetComponent<ecs::DirectionalLightComponent>(entity);auto old=light->color;float intensity=b.dayLightIntensity*day;std::array<float,3> color{1,.45f+.53f*warm,.22f+.68f*warm};changed|=old!=color||light->intensity!=intensity;light->color=color;light->intensity=intensity;}
 return changed;
}
}
