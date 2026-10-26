#include "ParticleSystem.h"
#include "../MutationQueue.h"
#include "../../core/Logger.h"
#include <chrono>
#include <algorithm>
namespace ecs {
namespace {
const std::string DefaultTexture="defaults/texture";
Vec3 Add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 Scale(Vec3 a,float s){return {a.x*s,a.y*s,a.z*s};}
Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Vec3 Unit(Vec3 v){float n=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);return n>1e-8f?Scale(v,1/n):Vec3{0,1,0};}
float Random(unsigned& s){s^=s<<13;s^=s>>17;s^=s<<5;return float(s>>8)*(1.f/16777216.f);}
Vec3 Transform(const RenderPose& p,Vec3 v,bool point){float m[16];BuildRenderModelMatrix(m,p);return {m[0]*v.x+m[4]*v.y+m[8]*v.z+(point?m[12]:0),m[1]*v.x+m[5]*v.y+m[9]*v.z+(point?m[13]:0),m[2]*v.x+m[6]*v.y+m[10]*v.z+(point?m[14]:0)};}
RenderPose Pose(const World& w,Entity e,const CameraState& camera,const RenderInterpolation* interpolation=nullptr){if(auto p=CameraAttachmentPose(w,e,camera,interpolation))return *p;return interpolation?interpolation->ResolveWorld(w,e):RenderPose::From(w.Parent(e).IsValid()?w.WorldTransform(e):w.LocalTransform(e));}
double Ms(std::chrono::steady_clock::time_point start){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
}
void ParticleSystem::Clear(){states.clear();particles.clear();entities.clear();destruction.clear();sorted.clear();worldID=0;stats={};}
void ParticleSystem::Bind(World& w){Clear();worldID=w.GetIdentity();if(particles.capacity()<MaxParticles){particles.reserve(MaxParticles);sorted.reserve(MaxParticles);entities.reserve(MaxEmitters);destruction.reserve(MaxEmitters);stats.storageGrowths=1;}}
ParticleSystem::State& ParticleSystem::Ensure(World& w,Entity e){w.AssertOwnerThread();if(worldID!=w.GetIdentity())throw std::runtime_error("Particles: inactive/foreign World session");auto* c=w.GetComponent<ParticleEmitterComponent>(e);if(!c||!w.HasComponent<TransformComponent>(e))throw std::runtime_error("Particles: dead/foreign Entity or missing ParticleEmitter/Transform");ValidateParticleEmitter(*c);auto it=states.find(e);if(it!=states.end())return it->second;if(states.size()>=MaxEmitters)throw std::runtime_error("Particles: active emitter limit (256)");State value;value.rng=unsigned(c->seed)?unsigned(c->seed):1;value.playing=c->playOnStart;value.pendingBurst=c->playOnStart;return states.emplace(e,value).first->second;}
void ParticleSystem::Play(World& w,Entity e){auto& s=Ensure(w,e);if(s.playing)return;s.playing=true;s.completed=false;s.time=0;s.fraction=0;s.pendingBurst=true;}
void ParticleSystem::Stop(World& w,Entity e){auto& s=Ensure(w,e);s.playing=false;s.pendingBurst=false;s.pending=0;s.completed=true;}
void ParticleSystem::Reset(World& w,Entity e){auto& s=Ensure(w,e);std::erase_if(particles,[&](auto& p){return p.owner==e;});s={};auto seed=w.GetComponent<ParticleEmitterComponent>(e)->seed;s.rng=seed?unsigned(seed):1;}
void ParticleSystem::Emit(World& w,Entity e,int n){if(n<0||n>int(MaxBirthsPerFrame))throw std::runtime_error("Particles.emit count must be in [0,10000]");auto& s=Ensure(w,e);s.pending=std::min(MaxBirthsPerFrame,s.pending+unsigned(n));if(n)s.completed=true;}
bool ParticleSystem::IsPlaying(World& w,Entity e){return Ensure(w,e).playing&&w.GetComponent<ParticleEmitterComponent>(e)->enabled;}
void ParticleSystem::Sync(World& w){
    entities.clear();w.ForEach<ParticleEmitterComponent,TransformComponent>([&](Entity e,ParticleEmitterComponent&,TransformComponent&){if(entities.size()<MaxEmitters)entities.push_back(e);});
    std::sort(entities.begin(),entities.end(),[](auto a,auto b){return a.index<b.index;});
    for(auto it=states.begin();it!=states.end();)if(!w.HasComponent<ParticleEmitterComponent>(it->first)||!w.HasComponent<TransformComponent>(it->first))it=states.erase(it);else ++it;
    for(auto e:entities)if(!states.contains(e)){try{auto& s=Ensure(w,e);auto* c=w.GetComponent<ParticleEmitterComponent>(e);if(c->playOnStart)Play(w,e);}catch(const std::exception& error){if(states.size()<MaxEmitters){State rejected;rejected.valid=false;states.emplace(e,rejected);Logger::Get().Warn(error.what());}}}
    std::erase_if(particles,[&](auto& p){return !states.contains(p.owner);});
}
void ParticleSystem::Birth(World& w,Entity e,State& s,const ParticleEmitterComponent& c,unsigned n,const CameraState& camera){
    if(!n)return;unsigned accepted=std::min({n,unsigned(c.maxParticles)-std::min(unsigned(c.maxParticles),s.live),MaxParticles-unsigned(particles.size()),MaxBirthsPerFrame-unsigned(stats.births)});stats.droppedBirths+=n-accepted;stats.births+=accepted;if(!accepted)return;
    auto pose=Pose(w,e,camera);auto direction=Unit(c.direction);auto tangent=Unit(Cross(direction,std::abs(direction.y)<.9f?Vec3{0,1,0}:Vec3{1,0,0}));auto bitangent=Cross(direction,tangent);
    for(unsigned i=0;i<accepted;++i){float cosine=1-Random(s.rng)*(1-std::cos(c.spreadAngle*.01745329252f)),sine=std::sqrt(std::max(0.f,1-cosine*cosine)),angle=Random(s.rng)*6.283185307f;auto v=Add(Scale(direction,cosine),Add(Scale(tangent,sine*std::cos(angle)),Scale(bitangent,sine*std::sin(angle))));v=Scale(v,c.speedMin+(c.speedMax-c.speedMin)*Random(s.rng));Particle p;p.owner=e;p.localSpace=c.localSpace;p.position=c.localSpace?Vec3{}:pose.position;p.velocity=c.localSpace?v:Transform({{}, {1,1,1},pose.rotation},v,false);p.lifetime=c.lifetimeMin+(c.lifetimeMax-c.lifetimeMin)*Random(s.rng);p.size=c.startSize;p.color=c.startColor;particles.push_back(p);++s.live;}
}
void ParticleSystem::Update(World& w,float dt,const CameraState& camera){
    auto start=std::chrono::steady_clock::now();if(worldID!=w.GetIdentity())return;if(!std::isfinite(dt)||dt<0)return;w.AssertOwnerThread();stats.births=stats.droppedBirths=0;Sync(w);
    dt=std::min(dt,.1f);int steps=std::max(1,int(std::ceil(dt/(1.f/60))));float h=dt/steps;
    for(int step=0;step<steps;++step){
        for(auto e:entities){auto it=states.find(e);if(it==states.end())continue;auto& s=it->second;auto& c=*w.GetComponent<ParticleEmitterComponent>(e);try{ValidateParticleEmitter(c);s.valid=true;}catch(...){if(s.valid)Logger::Get().Warn("Particles: invalid runtime configuration; emitter stopped");s.valid=false;s.playing=false;s.pending=0;continue;}
            if(c.enabled){if(s.pending){Birth(w,e,s,c,s.pending,camera);s.pending=0;}if(s.playing){if(s.pendingBurst){if(c.emissionMode==ParticleEmission::Burst)Birth(w,e,s,c,unsigned(c.burstCount),camera);s.pendingBurst=false;}
                float remaining=h;unsigned cycles=0;
                do {if(s.pendingBurst){if(c.emissionMode==ParticleEmission::Burst)Birth(w,e,s,c,unsigned(c.burstCount),camera);s.pendingBurst=false;}
                    float active=std::min(remaining,std::max(0.f,c.duration-s.time));if(c.emissionMode==ParticleEmission::Continuous){s.fraction+=active*c.emissionRate;auto n=unsigned(s.fraction+1e-5f);s.fraction=std::max(0.f,s.fraction-n);Birth(w,e,s,c,n,camera);}s.time+=active;remaining-=active;
                    if(s.time>=c.duration-std::min(1e-6f,c.duration*.001f)){if(c.looping){s.time=0;s.pendingBurst=true;if(++cycles>=8){s.time=std::fmod(remaining,c.duration);break;}}else{s.playing=false;s.completed=true;break;}}else break;
                }while(remaining>1e-7f);
            }}}
        for(std::size_t i=0;i<particles.size();){auto& p=particles[i];auto* c=w.GetComponent<ParticleEmitterComponent>(p.owner);auto found=states.find(p.owner);p.age+=h;if(!c||found==states.end()||!found->second.valid||p.age>=p.lifetime){if(found!=states.end()&&found->second.live)--found->second.live;p=particles.back();particles.pop_back();continue;}auto gravity=c->gravity;float damping=std::exp(-c->drag*h);p.velocity=Scale(Add(p.velocity,Scale(gravity,h)),damping);p.position=Add(p.position,Scale(p.velocity,h));float t=std::clamp(p.age/p.lifetime,0.f,1.f);p.size=c->startSize+(c->endSize-c->startSize)*t;for(int k=0;k<4;++k)p.color[k]=c->startColor[k]+(c->endColor[k]-c->startColor[k])*t;++i;}
    }
    // Queue structural changes after component iteration; never retain component pointers.
    MutationQueue queue;for(auto e:entities){auto it=states.find(e);if(it!=states.end()){auto& c=*w.GetComponent<ParticleEmitterComponent>(e);if(c.autoDestroyOnFinish&&!c.looping&&it->second.completed&&!it->second.playing&&!it->second.pending&&!it->second.live){auto members=w.Subtree(e);bool active=false;for(auto member:members){auto child=states.find(member);if(child!=states.end()&&(child->second.playing||child->second.pending||child->second.live)){active=true;break;}}if(!active)queue.Destroy(e);}}}queue.Apply(w);
    for(auto it=states.begin();it!=states.end();)if(!w.IsAlive(it->first))it=states.erase(it);else ++it;
    stats.alive=particles.size();stats.emitters=states.size();stats.simulationMs=Ms(start);
}
void ParticleSystem::Build(World& w,const CameraState& camera,const RenderInterpolation* interpolation,const std::function<RenderTextureHandle(const std::string&)>& texture,ParticleDrawList& world,ParticleDrawList& first){
    auto start=std::chrono::steady_clock::now();world.vertices.clear();world.commands.clear();first.vertices.clear();first.commands.clear();if(worldID!=w.GetIdentity())return;sorted.clear();
    struct FrameEmitter {const ParticleEmitterComponent* config;RenderPose pose;RenderTextureHandle texture;};std::array<FrameEmitter,MaxEmitters> frame{};unsigned emitterCount=0;
    for(auto& [owner,s]:states){s.renderIndex=UINT32_MAX;auto* c=w.GetComponent<ParticleEmitterComponent>(owner);if(!s.valid||!c||!c->enabled||!w.HasComponent<TransformComponent>(owner)||!s.live)continue;s.renderIndex=emitterCount;frame[emitterCount++]={c,Pose(w,owner,camera,interpolation),texture(c->texture.empty()?DefaultTexture:c->texture)};}
    for(auto& p:particles){auto state=states.find(p.owner);if(state==states.end()||state->second.renderIndex==UINT32_MAX)continue;auto& emitter=frame[state->second.renderIndex];auto position=p.localSpace?Transform(emitter.pose,p.position,true):p.position;auto relative=Add(position,Scale(camera.pose.position,-1));float depth=relative.x*camera.forward.x+relative.y*camera.forward.y+relative.z*camera.forward.z;sorted.push_back({&p,emitter.config,{position,emitter.pose.scale,{}},emitter.texture,depth});}
    std::sort(sorted.begin(),sorted.end(),[](auto& a,auto& b){if(a.config->renderLayer!=b.config->renderLayer)return int(a.config->renderLayer)<int(b.config->renderLayer);if(a.config->blendMode!=b.config->blendMode)return int(a.config->blendMode)<int(b.config->blendMode);if(a.config->blendMode==ParticleBlend::Alpha)return a.depth>b.depth;return a.texture.value<b.texture.value;});
    for(auto& item:sorted){auto& p=*item.particle;auto& c=*item.config;if(!item.texture.IsValid()||p.size<=0)continue;auto& list=c.renderLayer==ParticleLayer::World?world:first;float size=p.size*.5f*(p.localSpace?std::max({std::abs(item.pose.scale.x),std::abs(item.pose.scale.y),std::abs(item.pose.scale.z)}):1.f);auto color=p.color;color[3]*=c.opacity;unsigned begin=unsigned(list.vertices.size());
        for(auto uv:std::array<std::array<float,2>,6>{{{0,1},{0,0},{1,0},{0,1},{1,0},{1,1}}}){auto position=Add(item.pose.position,Add(Scale(camera.right,(uv[0]*2-1)*size),Scale(camera.up,(1-uv[1]*2)*size)));ParticleVertex v;auto& m=camera.viewProjection;for(int row=0;row<4;++row)v.position[row]=m[row]*position.x+m[4+row]*position.y+m[8+row]*position.z+m[12+row];v.uv=uv;v.color=color;list.vertices.push_back(v);}
        bool additive=c.blendMode==ParticleBlend::Additive;if(!list.commands.empty()&&list.commands.back().texture==item.texture&&list.commands.back().additive==additive)list.commands.back().count+=6;else list.commands.push_back({begin,6,item.texture,additive});
    }sorted.clear();stats.buildMs=Ms(start);
}
}
