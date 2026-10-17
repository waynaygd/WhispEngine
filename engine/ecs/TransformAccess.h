#pragma once
#include "World.h"
#include "TransformMath.h"
#include "Hierarchy.h"
#include <string>
namespace ecs {
// TransformComponent stores authored local TRS. Gameplay Worlds remain root-only;
// data-only hierarchy Worlds resolve checked world values through the graph.
// Reads are values, so callers do not retain a pointer to a future world cache.
inline std::optional<TransformComponent> ReadWorldTransform(const World& world,Entity e){
    if(world.IsHierarchyData())return ReadHierarchyTransform(world,e);
    if(auto* t=world.GetComponent<TransformComponent>(e))return *t;return {};
}
inline std::optional<Vec3> ReadWorldPosition(const World& w,Entity e){if(w.IsHierarchyData()){auto m=EvaluateHierarchyMatrix(w,e);if(!m)return {};Vec3 value{float((*m)[12]),float((*m)[13]),float((*m)[14])};return Finite(value)?std::optional(value):std::nullopt;}auto t=ReadWorldTransform(w,e);return t?std::optional(t->position):std::nullopt;}
inline std::optional<Vec3> ReadWorldRotation(const World& w,Entity e){auto t=ReadWorldTransform(w,e);return t?std::optional(t->rotation):std::nullopt;}
inline std::optional<Vec3> ReadWorldScale(const World& w,Entity e){auto t=ReadWorldTransform(w,e);return t?std::optional(t->scale):std::nullopt;}
inline std::optional<Matrix4> ReadWorldMatrix(const World& w,Entity e){if(w.IsHierarchyData())return EvaluateHierarchyMatrix(w,e);auto t=ReadWorldTransform(w,e);return t?TryComposeTRS(*t):std::nullopt;}
inline bool TransformFailure(std::string* error,const char* message){if(error)*error=message;return false;}
inline bool TrySetWorldTransform(World& w,Entity e,const TransformComponent& value,std::string* error=nullptr){
    if(w.IsHierarchyData())return WriteHierarchyTransform(w,e,value,error);
    w.AssertOwnerThread();auto* target=w.GetComponent<TransformComponent>(e);
    if(!target)return TransformFailure(error,"Dead/stale/foreign entity or absent Transform");
    if(!Finite(value))return TransformFailure(error,"Nonfinite world Transform");
    *target=value;return true;
}
// Explicit finite TRS may contain zero scale, as legacy scenes/Lua allow it.
// Matrix conversion/inversion separately reject singular/near-singular TRS.
inline bool TrySetWorldPosition(World& w,Entity e,Vec3 value,std::string* error=nullptr){
    w.AssertOwnerThread();
    if(w.IsHierarchyData()){
        auto* local=w.GetComponent<TransformComponent>(e);auto* relation=w.GetComponent<RelationshipComponent>(e);
        if(local&&(!relation||!relation->parent)){auto next=*local;next.position=value;return WriteHierarchyTransform(w,e,next,error);}
        auto m=ReadWorldMatrix(w,e);if(!m||!Finite(value))return TransformFailure(error,"Invalid hierarchy world position");(*m)[12]=value.x;(*m)[13]=value.y;(*m)[14]=value.z;return WriteHierarchyMatrix(w,e,*m,error);
    }
    auto t=ReadWorldTransform(w,e);if(!t)return TransformFailure(error,"Dead/stale/foreign entity or absent Transform");t->position=value;return TrySetWorldTransform(w,e,*t,error);
}
inline bool TrySetWorldRotation(World& w,Entity e,Vec3 value,std::string* error=nullptr){
    w.AssertOwnerThread();
    auto t=ReadWorldTransform(w,e);if(!t)return TransformFailure(error,"Dead/stale/foreign entity or absent Transform");t->rotation=value;return TrySetWorldTransform(w,e,*t,error);
}
inline bool TrySetWorldScale(World& w,Entity e,Vec3 value,std::string* error=nullptr){
    w.AssertOwnerThread();
    auto t=ReadWorldTransform(w,e);if(!t)return TransformFailure(error,"Dead/stale/foreign entity or absent Transform");t->scale=value;return TrySetWorldTransform(w,e,*t,error);
}
inline bool TrySetWorldMatrix(World& w,Entity e,const Matrix4& value,std::string* error=nullptr){
    if(w.IsHierarchyData())return WriteHierarchyMatrix(w,e,value,error);
    w.AssertOwnerThread();auto current=ReadWorldTransform(w,e);if(!current)return TransformFailure(error,"Dead/stale/foreign entity or absent Transform");
    auto next=DecomposeTRS(value,current->scale);if(!next)return TransformFailure(error,"World matrix is nonfinite, singular, out of TRS range or contains shear");
    return TrySetWorldTransform(w,e,*next,error);
}
// Explicit compatibility boundary for the in-place root-only solver. No cache,
// synchronization or per-substep validation is introduced. Never use for Lua/UI.
inline TransformComponent* BorrowRootWorldTransform(World& w,Entity e){w.AssertOwnerThread();w.RequireGameplayWorld();return w.GetComponent<TransformComponent>(e);}
}
