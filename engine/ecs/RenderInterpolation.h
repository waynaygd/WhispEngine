#pragma once
#include "World.h"
#include "components/TransformComponent.h"
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
struct RenderQuaternion { float x=0, y=0, z=0, w=1; };
inline RenderQuaternion NormalizeQuaternion(RenderQuaternion q) {
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(n<1e-12f) return {};
    return {q.x/n,q.y/n,q.z/n,q.w/n};
}
// Matches the existing model matrix Rz * Ry * Rx, column vectors.
inline RenderQuaternion RenderRotation(Vec3 r) {
    const float x=std::sin(r.x*.5f), X=std::cos(r.x*.5f);
    const float y=std::sin(r.y*.5f), Y=std::cos(r.y*.5f);
    const float z=std::sin(r.z*.5f), Z=std::cos(r.z*.5f);
    return NormalizeQuaternion({x*Y*Z-X*y*z,X*y*Z+x*Y*z,X*Y*z-x*y*Z,X*Y*Z+x*y*z});
}
inline RenderQuaternion RenderSlerp(RenderQuaternion a,RenderQuaternion b,float t) {
    a=NormalizeQuaternion(a); b=NormalizeQuaternion(b);
    float d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(d<0) { b={-b.x,-b.y,-b.z,-b.w}; d=-d; }
    d=std::clamp(d,-1.f,1.f);
    float p=1-t,q=t;
    if(d<.9995f) { const float angle=std::acos(d),s=std::sin(angle);p=std::sin((1-t)*angle)/s;q=std::sin(t*angle)/s; }
    return NormalizeQuaternion({p*a.x+q*b.x,p*a.y+q*b.y,p*a.z+q*b.z,p*a.w+q*b.w});
}
struct RenderPose {
    Vec3 position{},scale{1,1,1}; RenderQuaternion rotation{};
    static RenderPose From(const TransformComponent& t) { return {t.position,t.scale,RenderRotation(t.rotation)}; }
};
inline void BuildRenderModelMatrix(float* m,const RenderPose& p) {
    const auto q=p.rotation;
    m[0]=(1-2*(q.y*q.y+q.z*q.z))*p.scale.x; m[1]=2*(q.x*q.y+q.w*q.z)*p.scale.x; m[2]=2*(q.x*q.z-q.w*q.y)*p.scale.x; m[3]=0;
    m[4]=2*(q.x*q.y-q.w*q.z)*p.scale.y; m[5]=(1-2*(q.x*q.x+q.z*q.z))*p.scale.y; m[6]=2*(q.y*q.z+q.w*q.x)*p.scale.y; m[7]=0;
    m[8]=2*(q.x*q.z+q.w*q.y)*p.scale.z; m[9]=2*(q.y*q.z-q.w*q.x)*p.scale.z; m[10]=(1-2*(q.x*q.x+q.y*q.y))*p.scale.z; m[11]=0;
    m[12]=p.position.x;m[13]=p.position.y;m[14]=p.position.z;m[15]=1;
}
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
        if(m_Records.size()<world.GetCapacity()) { if(m_Records.capacity()<world.GetCapacity())++m_Growths;m_Records.resize(world.GetCapacity()); }
        ++m_Stamp;m_Count=0;
        world.ForEach<RigidbodyComponent,TransformComponent,ColliderComponent>([&](Entity e,RigidbodyComponent& rb,TransformComponent& t,ColliderComponent&) {
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
        const auto* t=world.GetComponent<TransformComponent>(e);
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
