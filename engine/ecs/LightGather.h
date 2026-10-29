#pragma once
#include "components/LightComponent.h"
#include "RenderInterpolation.h"
#include "../render/Lighting.h"
#include "../render/SpotShadows.h"
#include <chrono>
namespace ecs
{
struct GatheredLights
{
    std::vector<GpuSceneLight> lights;
    LightingSettings lighting;
    SceneLightDiagnostics diagnostics;
    bool primaryCastShadows=true;
    std::vector<SpotShadowFrame> spotShadows;
    unsigned spotShadowRequests=0;
};
inline Vec3 LightDirection(const RenderPose& pose)
{
    // Local +Z is the direction of outgoing rays. Rotation only, scale never flips rays.
    auto q=pose.rotation;
    return {2*(q.x*q.z+q.w*q.y),2*(q.y*q.z-q.w*q.x),1-2*(q.x*q.x+q.y*q.y)};
}
inline GatheredLights GatherSceneLights(World& world,const LightingSettings& legacy,
                                      const LightCamera& camera,RenderInterpolation* interpolation=nullptr)
{
#if defined(TRACY_ENABLE)
    ZoneScopedN("LightGather");
#endif
    auto start=std::chrono::steady_clock::now();GatheredLights out;out.lighting=legacy;
    struct Entry { Entity entity;LightType type;LightParameters p;RenderPose pose;bool primary; };
    std::vector<Entry> entries;entries.reserve(32);
    auto add=[&](Entity e,const TransformComponent& t,const LightParameters& p,LightType type,bool primary){
        ++out.diagnostics.total;
        if(type==LightType::Directional)++out.diagnostics.directional;
        else if(type==LightType::Point)++out.diagnostics.point;else ++out.diagnostics.spot;
        auto safe=SanitizeLight(p);if(!safe.enabled)return;
        auto pose=interpolation?interpolation->ResolveWorld(world,e):RenderPose::From(world.WorldTransform(e));
        if(!std::isfinite(pose.position.x)||!std::isfinite(pose.position.y)||!std::isfinite(pose.position.z))return;
        // Invalid Euler/quaternion data never reaches light buffers.
        if(!std::isfinite(pose.rotation.x)||!std::isfinite(pose.rotation.y)||!std::isfinite(pose.rotation.z)||!std::isfinite(pose.rotation.w))pose.rotation={};
        ++out.diagnostics.enabled;entries.push_back({e,type,safe,pose,primary});
    };
    world.ForEach<TransformComponent,DirectionalLightComponent>([&](Entity e,auto& t,auto& p){add(e,t,p,LightType::Directional,p.primary);});
    world.ForEach<TransformComponent,PointLightComponent>([&](Entity e,auto& t,auto& p){add(e,t,p,LightType::Point,false);});
    world.ForEach<TransformComponent,SpotLightComponent>([&](Entity e,auto& t,auto& p){add(e,t,p,LightType::Spot,false);});
    std::sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.entity.index<b.entity.index||(a.entity.index==b.entity.index&&a.entity.generation<b.entity.generation);});
    const Entry* primary=nullptr;
    for(const auto& e:entries)if(e.type==LightType::Directional&&(!primary||(!primary->primary&&e.primary)))primary=&e;
    if(primary){if(legacy.enabled&&primary->p.intensity>0)++out.diagnostics.influencing;auto d=LightDirection(primary->pose);out.lighting.directional.direction={d.x,d.y,d.z};
        out.lighting.directional.color=primary->p.color;out.lighting.directional.intensity=primary->p.intensity;out.lighting.directional.enabled=true;
        out.primaryCastShadows=primary->p.castShadows;out.diagnostics.primaryEcs=true;
        out.diagnostics.primaryIndex=primary->entity.index;out.diagnostics.primaryGeneration=primary->entity.generation;}
    struct Candidate{double score;SpotShadowFrame frame;};std::vector<Candidate> candidates;
    for(const auto& e:entries){if(&e==primary||!legacy.enabled||e.p.intensity<=0)continue;
        GpuSceneLight l;auto d=LightDirection(e.pose);
        l.positionRange={e.pose.position.x,e.pose.position.y,e.pose.position.z,e.p.range};
        l.directionType={d.x,d.y,d.z,float(e.type)};l.colorIntensity={e.p.color[0],e.p.color[1],e.p.color[2],e.p.intensity};
        l.coneAttenuation={std::cos(e.p.innerAngle*.0174532925199433f),std::cos(e.p.outerAngle*.0174532925199433f),e.p.attenuation,1};
        l.screenBounds=ProjectLightBounds(l,camera);if(l.screenBounds[0]<=1&&l.screenBounds[1]<=1&&l.screenBounds[2]>=0&&l.screenBounds[3]>=0)++out.diagnostics.influencing;
        if(e.type==LightType::Spot&&e.p.castShadows&&e.p.shadowStrength>0){
            const auto& b=l.screenBounds;double area=std::max(0.f,std::min(1.f,b[2])-std::max(0.f,b[0]))*std::max(0.f,std::min(1.f,b[3])-std::max(0.f,b[1]));
            if(area>0){++out.spotShadowRequests;const auto q=e.pose.rotation;ShadowVector up{2*(q.x*q.y-q.w*q.z),1-2*(q.x*q.x+q.z*q.z),2*(q.y*q.z+q.w*q.x)};
                if(auto f=BuildSpotShadowFrame(unsigned(out.lights.size()),l,e.p,up))candidates.push_back({area*e.p.intensity*std::max({e.p.color[0],e.p.color[1],e.p.color[2]}),*f});}
        }
        out.lights.push_back(l);
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.score>b.score;});
    for(size_t i=0;i<std::min<size_t>(candidates.size(),MaxSpotShadows);++i)out.spotShadows.push_back(candidates[i].frame);
    out.diagnostics.gatherMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return out;
}
}
