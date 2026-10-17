#pragma once
#include "World.h"
#include "TransformAccess.h"
#include "components/RigidbodyComponent.h"
#include "components/ColliderComponent.h"
#include <algorithm>
#include <cmath>
#include <vector>
#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#define WHISP_INTERPOLATION_ZONE(name) ZoneScopedN(name)
#else
#define WHISP_INTERPOLATION_ZONE(name) ((void)0)
#endif

namespace ecs {
class RenderInterpolation {
    struct Record { Entity entity{}; TransformComponent source{}; RenderPose previous{},current{}; std::uint64_t stamp=0;bool valid=false; };
    std::vector<Record> m_Records;
    std::uint64_t m_Stamp=0;
    bool m_Enabled=true,m_Active=false;
    float m_Alpha=0;
    std::size_t m_Count=0,m_Growths=0;
    static bool Same(Vec3 a,Vec3 b) { return a.x==b.x&&a.y==b.y&&a.z==b.z; }
    static bool Same(const TransformComponent& a,const TransformComponent& b) { return Same(a.position,b.position)&&Same(a.rotation,b.rotation)&&Same(a.scale,b.scale); }
    void Capture(World& world,bool completedTick) {
        WHISP_INTERPOLATION_ZONE("RenderPoseCapture");
        // Explicit MVP policy: the entire hierarchy World renders current
        // affine transforms. Never interpolate a parent separately from children.
        if(world.IsHierarchyData()){m_Records.clear();m_Count=0;return;}
        if(m_Records.size()<world.GetCapacity()) { if(m_Records.capacity()<world.GetCapacity())++m_Growths;m_Records.resize(world.GetCapacity()); }
        ++m_Stamp;m_Count=0;
        world.ForEach<RigidbodyComponent,TransformComponent,ColliderComponent>([&](Entity e,RigidbodyComponent& rb,TransformComponent&,ColliderComponent&) {
            const auto t=*ReadWorldTransform(world,e);
            if(rb.isStatic||!rb.simulatePhysics)return;
            auto& r=m_Records[e.index];
            const bool changed=!r.valid||r.entity!=e||!Same(t,r.source);
            const auto pose=changed?RenderPose::From(t):r.current;
            if(!r.valid||r.entity!=e||(!completedTick&&!Same(t,r.source)))r.previous=pose;
            r.current=pose;r.source=t;r.entity=e;r.valid=true;r.stamp=m_Stamp;++m_Count;
        });
        for(auto& r:m_Records)if(r.stamp!=m_Stamp)r.valid=false;
    }
public:
    // Main-thread only. No component pointers or render-resource ownership.
    void Clear() { m_Records.clear();m_Count=0;m_Alpha=0; }
    void SetEnabled(bool v) { m_Enabled=v; } // Continue history while OFF for safe comparison.
    bool IsEnabled()const{return m_Enabled;}
    void SetFrame(World& world,bool active,double alpha) {
        WHISP_INTERPOLATION_ZONE("RenderInterpolationSync");
        m_Active=active;m_Alpha=std::isfinite(alpha)?float(std::clamp(alpha,0.0,std::nextafter(1.0,0.0))):0;
        m_Alpha=std::min(m_Alpha,std::nextafter(1.f,0.f));
        Capture(world,false);
        if(!active)for(auto& r:m_Records)if(r.valid)r.previous=r.current;
    }
    void BeginTick(World& world) { Capture(world,false);for(auto& r:m_Records)if(r.valid)r.previous=r.current; }
    void EndTick(World& world) { Capture(world,true); }
    void ResetEntity(World& world,Entity e) {
        if(world.IsHierarchyData())return;
        const auto t=ReadWorldTransform(world,e);
        if(!t||e.index>=m_Records.size())return;
        auto& r=m_Records[e.index];r.entity=e;r.source=*t;r.previous=r.current=RenderPose::From(*t);r.valid=true;
    }
    float Alpha()const{return m_Alpha;}
    std::size_t Count()const{return m_Enabled&&m_Active?m_Count:0;}
    std::size_t BufferGrowths()const{return m_Growths;}
    RenderPose Resolve(Entity e,const TransformComponent& physical)const {
        if(!m_Enabled||!m_Active||e.index>=m_Records.size())return RenderPose::From(physical);
        const auto& r=m_Records[e.index];
        // Also handles edits made by a frame system after SetFrame and stale generation.
        if(!r.valid||r.entity!=e||!Same(physical,r.source))return RenderPose::From(physical);
        const auto lerp=[&](Vec3 a,Vec3 b){return Vec3{a.x+(b.x-a.x)*m_Alpha,a.y+(b.y-a.y)*m_Alpha,a.z+(b.z-a.z)*m_Alpha};};
        return {lerp(r.previous.position,r.current.position),lerp(r.previous.scale,r.current.scale),RenderSlerp(r.previous.rotation,r.current.rotation,m_Alpha)};
    }
};
}

#undef WHISP_INTERPOLATION_ZONE
