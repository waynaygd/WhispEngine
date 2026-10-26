#pragma once
#include "../Component.h"
#include "../MathTypes.h"
#include <array>
#include <string>
#include <cmath>
#include <stdexcept>
namespace ecs {
enum class ParticleEmission { Continuous, Burst };
enum class ParticleBlend { Alpha, Additive };
enum class ParticleLayer { World, FirstPerson };
struct ParticleEmitterComponent : Component {
    bool enabled=true,playOnStart=true,looping=false;
    ParticleEmission emissionMode=ParticleEmission::Burst;
    float emissionRate=20,duration=1;
    int burstCount=12,maxParticles=256,seed=1;
    float lifetimeMin=.2f,lifetimeMax=.5f,speedMin=0,speedMax=1;
    Vec3 direction{0,1,0};float spreadAngle=30;
    Vec3 gravity{};float drag=0;bool localSpace=false;
    float startSize=.1f,endSize=0;
    std::array<float,4> startColor{1,1,1,1},endColor{1,1,1,0};
    std::string texture;float opacity=1;
    int billboardOrientation=0; // Face Camera; reserved for future orientations.
    ParticleBlend blendMode=ParticleBlend::Alpha;
    ParticleLayer renderLayer=ParticleLayer::World;
    bool autoDestroyOnFinish=false;
};
inline void ValidateParticleEmitter(const ParticleEmitterComponent& c){
    auto range=[](float n,float lo,float hi){return std::isfinite(n)&&n>=lo&&n<=hi;};
    bool ok=range(c.emissionRate,0,10000)&&range(c.duration,.001f,3600)&&c.burstCount>=0&&c.burstCount<=10000&&c.maxParticles>=1&&c.maxParticles<=10000&&c.seed>=0
        &&range(c.lifetimeMin,.001f,3600)&&range(c.lifetimeMax,c.lifetimeMin,3600)&&range(c.speedMin,0,10000)&&range(c.speedMax,c.speedMin,10000)
        &&range(c.spreadAngle,0,180)&&range(c.drag,0,1000)&&range(c.startSize,0,10000)&&range(c.endSize,0,10000)&&range(c.opacity,0,1)
        &&int(c.emissionMode)>=0&&int(c.emissionMode)<=1&&int(c.blendMode)>=0&&int(c.blendMode)<=1&&int(c.renderLayer)>=0&&int(c.renderLayer)<=1&&c.billboardOrientation==0;
    for(auto v:{c.direction.x,c.direction.y,c.direction.z,c.gravity.x,c.gravity.y,c.gravity.z})ok&=range(v,-10000,10000);
    for(auto v:c.startColor)ok&=range(v,0,1);for(auto v:c.endColor)ok&=range(v,0,1);
    if(!ok)throw std::runtime_error("Invalid ParticleEmitter: ranges, min/max pairs, enums and finite values are required");
}
}
