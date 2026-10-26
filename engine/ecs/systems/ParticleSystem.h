#pragma once
#include "../CameraAttachments.h"
#include "../components/ParticleEmitterComponent.h"
#include "../../render/Particles.h"
#include <unordered_map>
#include <functional>
namespace ecs {
// Session-owned transient state. No authored component pointers or GPU ownership.
class ParticleSystem {
public:
    static constexpr unsigned MaxEmitters=256,MaxParticles=10000,MaxBirthsPerFrame=10000;
    struct Particle {Entity owner;Vec3 position,velocity;float age=0,lifetime=1,size=1;std::array<float,4> color{1,1,1,1};bool localSpace=false;};
    struct Statistics {std::size_t alive=0,emitters=0,births=0,droppedBirths=0,storageGrowths=0;double simulationMs=0,buildMs=0;};
    void Bind(World&);void Clear();
    void Play(World&,Entity);void Stop(World&,Entity);void Reset(World&,Entity);void Emit(World&,Entity,int);
    bool IsPlaying(World&,Entity);
    void Update(World&,float dt,const CameraState& camera=BuildCameraState({},CameraComponent{},1));
    void Build(World&,const CameraState&,const RenderInterpolation*,const std::function<RenderTextureHandle(const std::string&)>&,ParticleDrawList&,ParticleDrawList&);
    const auto& Particles()const{return particles;}const auto& GetStatistics()const{return stats;}
private:
    struct State {bool playing=false,pendingBurst=false,completed=false,valid=true;float time=0,fraction=0;unsigned rng=1,pending=0,live=0,renderIndex=UINT32_MAX;};
    std::unordered_map<Entity,State,EntityHash> states;
    std::vector<Particle> particles;
    std::vector<Entity> entities,destruction;
    struct Item {const Particle* particle;const ParticleEmitterComponent* config;RenderPose pose;RenderTextureHandle texture;float depth;};
    std::vector<Item> sorted;
    std::uint64_t worldID=0;Statistics stats;
    State& Ensure(World&,Entity);void Sync(World&);void Birth(World&,Entity,State&,const ParticleEmitterComponent&,unsigned,const CameraState&);
};
}
