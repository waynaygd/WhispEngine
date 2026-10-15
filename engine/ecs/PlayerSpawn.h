#pragma once
#include "World.h"
#include "components/TransformComponent.h"
#include "components/PlayerStartComponent.h"
#include "components/PlayerComponent.h"
#include "components/CharacterMovementComponent.h"
#include "components/RigidbodyComponent.h"
#include "components/ColliderComponent.h"
#include "components/TagComponent.h"
#include "components/LuaScriptComponent.h"
#include "events/EventBus.h"
#include <cmath>
#include <algorithm>
namespace ecs {
// PlayerStart is the feet position. Body dimensions are world sized, independent
// of marker scale. The FPS body stays upright; pitch belongs to the camera.
inline constexpr Vec3 PlayerHalfExtents{.3f,.85f,.3f};
inline constexpr float PlayerMaximumPitch=1.55334306f;
inline Vec3 PlayerLookDirection(const TransformComponent& start){
 const float pitch=std::clamp(start.rotation.x,-PlayerMaximumPitch,PlayerMaximumPitch);
 return {std::sin(start.rotation.y)*std::cos(pitch),std::sin(pitch),std::cos(start.rotation.y)*std::cos(pitch)};
}
inline Vec3 PlayerBodyCenter(Vec3 feet){feet.y+=PlayerHalfExtents.y;return feet;}
inline Vec3 PlayerEyePosition(Vec3 center,float eyeHeight){center.y+=eyeHeight-PlayerHalfExtents.y;return center;}
inline Vec3 PlayerBoxPoint(const TransformComponent& start,Vec3 local){
 const float c=std::cos(start.rotation.y),s=std::sin(start.rotation.y);
 auto center=PlayerBodyCenter(start.position);
 return {center.x+c*local.x+s*local.z,center.y+local.y,center.z-s*local.x+c*local.z};
}
inline Entity SpawnPlayer(World& world,TransformComponent* spawnPoint=nullptr){
 Entity chosen;TransformComponent spawn;std::string controllerScript;
 // Collect handles before adding/removing entities.
 auto markers=world.Entities();
 for(auto e:markers){auto* s=world.GetComponent<PlayerStartComponent>(e);auto* t=world.GetComponent<TransformComponent>(e);
  if(!chosen.IsValid()&&s&&s->enabled&&t){chosen=e;spawn=*t;controllerScript=s->controllerScript;}}
 Entity player;
 if(chosen.IsValid()){
  if(spawnPoint)*spawnPoint=spawn;
  player=world.CreateEntity();auto& t=world.AddComponent<TransformComponent>(player);
  t.position=PlayerBodyCenter(spawn.position);t.rotation={0,spawn.rotation.y,0};
  world.AddComponent<PlayerComponent>(player);world.AddComponent<CharacterMovementComponent>(player);
  auto& b=world.AddComponent<RigidbodyComponent>(player);b.mass=70;b.lockRotation=true;
  auto& c=world.AddComponent<ColliderComponent>(player);c.halfExtents=PlayerHalfExtents;c.autoFitFromMesh=false;c.restitution=0;c.friction=.8f;
  world.AddComponent<TagComponent>(player).name="Player";
  if(!controllerScript.empty()){auto& script=world.AddComponent<LuaScriptComponent>(player);script.scriptPath=controllerScript;script.inputEnabled=true;script.cameraEnabled=true;}
 }
 // All runtime markers are editor-only, including disabled/legacy physical ones.
 for(auto e:markers)if(world.HasComponent<PlayerStartComponent>(e))world.DestroyEntity(e);
 return player;
}
inline void SampleCharacterInput(CharacterMovementComponent& m,Vec3 direction,bool jumpDown,bool active){
 if(active&&jumpDown&&!m.jumpWasDown)m.jumpRequested=true;
 m.jumpWasDown=jumpDown;m.desiredDirection={};
 if(!active){m.jumpRequested=false;return;}
 const float length=std::sqrt(direction.x*direction.x+direction.z*direction.z);
 if(length>.0001f)m.desiredDirection={direction.x/length,0,direction.z/length};
}
inline void ApplyCharacterMovement(World& world,Entity e){
 auto* m=world.GetComponent<CharacterMovementComponent>(e);auto* b=world.GetComponent<RigidbodyComponent>(e);
 if(!m||!b)return;
 b->lockRotation=true;b->angularVelocity={};b->torque={};
 b->velocity.x=m->desiredDirection.x*m->moveSpeed;b->velocity.z=m->desiredDirection.z*m->moveSpeed;
 if(m->jumpRequested&&m->grounded){b->velocity.y=m->jumpSpeed;m->grounded=false;}
 if(b->velocity.x!=0||b->velocity.z!=0||m->jumpRequested){b->sleeping=false;b->sleepTimer=0;}
 m->jumpRequested=false;m->grounded=false;
}
inline void GroundCharacter(World& world,Entity player,const CollisionEvent& e){
 auto* m=world.GetComponent<CharacterMovementComponent>(player);auto* b=world.GetComponent<RigidbodyComponent>(player);
 if(m&&b&&b->velocity.y<=.15f&&((e.a==player&&e.normal.y>.5f)||(e.b==player&&e.normal.y<-.5f)))m->grounded=true;
}
}
