#include "ecs/World.h"
#include "ecs/systems/PhysicsSystem.h"
#include "ecs/components/TransformComponent.h"
#include "ecs/components/RigidbodyComponent.h"
#include "ecs/components/ColliderComponent.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <string>
#include <algorithm>
using namespace ecs;
static float Length(Vec3 v) { return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z); }
static Vec3 Sub(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
static void Require(bool yes, const char* message) { if (!yes) throw std::runtime_error(message); }
struct Scene
{
    World world;
    PhysicsSystem physics{nullptr,9.81f,0.985f,3,0.05f,0.85f,12}; // actual app configuration
    Entity Add(Vec3 pos, Vec3 half, bool fixed=false, ColliderType type=ColliderType::Box, Vec3 rotation={})
    {
        const auto e = world.CreateEntity();
        auto& t=world.AddComponent<TransformComponent>(e); t.position=pos; t.rotation=rotation;
        auto& c=world.AddComponent<ColliderComponent>(e); c.halfExtents=half; c.type=type; c.autoFitFromMesh=false;
        auto& rb=world.AddComponent<RigidbodyComponent>(e); rb.isStatic=fixed;
        return e;
    }
    Entity Ground() { return Add({0,-0.025f,0},{4,0.025f,4},true); }
    TransformComponent& T(Entity e) { return *world.GetComponent<TransformComponent>(e); }
    RigidbodyComponent& R(Entity e) { return *world.GetComponent<RigidbodyComponent>(e); }
    void Run(float seconds,float dt=1.0f/60.0f)
    {
        const int count=int(std::round(seconds/dt));
        for(int i=0;i<count;++i)
        {
            physics.Update(world,dt);
            world.ForEach<RigidbodyComponent,TransformComponent>([](Entity, RigidbodyComponent& rb,TransformComponent& t)
            {
                Require(std::isfinite(Length(t.position)) && std::isfinite(Length(rb.velocity)) && std::isfinite(Length(rb.angularVelocity)),"non-finite state");
            });
        }
    }
};
static void RestAndTilt()
{
    Scene s; s.Ground(); auto cube=s.Add({0,1,0},{.09f,.09f,.09f});
    s.Run(8);
    std::cout<<"rest y="<<s.T(cube).position.y<<" v="<<Length(s.R(cube).velocity)<<" w="<<Length(s.R(cube).angularVelocity)<<" sleeping="<<s.R(cube).sleeping<<'\n';
    Require(std::abs(s.T(cube).position.y-.09f)<.002f,"rest height");
    Require(s.R(cube).sleeping,"resting cube did not sleep");
    auto before=s.T(cube).position; s.Run(20); Require(Length(Sub(before,s.T(cube).position))<.0001f,"rest drift");
    auto tilt=s.Add({1,.12f,0},{.09f,.09f,.09f},false,ColliderType::Box,{0,0,.35f});
    s.Run(8);
    std::cout<<"tilt z="<<s.T(tilt).rotation.z<<" y="<<s.T(tilt).position.y<<'\n';
    Require(std::abs(s.T(tilt).position.y-.09f)<.002f,"tilted cube did not topple");
    Require(std::abs(s.T(tilt).rotation.z)<.03f,"tilted orientation");
}
static void Pyramid(float dt, bool disableSleep=false)
{
    Scene s; s.Ground(); std::vector<Entity> cubes; std::vector<Vec3> initial;
    for(int layer=0;layer<4;++layer) for(int i=0;i<4-layer;++i)
    {
        Vec3 p{-.5f*(3-layer)*.18f+i*.18f,.09f+layer*.18f,.45f};
        cubes.push_back(s.Add(p,{.09f,.09f,.09f})); initial.push_back(p);
        if(disableSleep) {s.R(cubes.back()).linearDampingMultiplier=0;s.R(cubes.back()).angularDampingMultiplier=0;}
    }
    for(int i=0;i<int(20/dt);++i)
    {
        if(disableSleep) for(auto e:cubes) {s.R(e).sleeping=false;s.R(e).sleepTimer=0;}
        s.physics.Update(s.world,dt);
    }
    float drift=0, speed=0; int asleep=0;
    for(std::size_t i=0;i<cubes.size();++i)
    {
        drift=std::max(drift,Length(Sub(s.T(cubes[i]).position,initial[i])));
        speed=std::max(speed,Length(s.R(cubes[i]).velocity)); asleep+=s.R(cubes[i]).sleeping;
    }
    std::cout<<"pyramid dt="<<dt<<" sleepOff="<<disableSleep<<" drift="<<drift<<" speed="<<speed<<" asleep="<<asleep<<'\n';
    Require(drift<.012f,"pyramid collapsed/drifted"); Require(speed<.02f,"pyramid residual motion");
    if(!disableSleep) Require(asleep==10,"pyramid failed to sleep together");
    const auto projectile=s.Add({-.8f,.34f,.47f},{.075f,.075f,.075f});
    s.R(projectile).velocity={14,0,0}; s.R(projectile).mass=2;
    float peakSpin=0, peakEnergy=0;
    for(int i=0;i<120;++i)
    {
        s.physics.Update(s.world,1.0f/120);
        float energy=0;
        for(auto e:cubes)
        {
            peakSpin=std::max(peakSpin,Length(s.R(e).angularVelocity));
            const float v=Length(s.R(e).velocity),w=Length(s.R(e).angularVelocity);
            energy+=.5f*v*v+.0027f*w*w+9.81f*s.T(e).position.y;
        }
        const float pv=Length(s.R(projectile).velocity),pw=Length(s.R(projectile).angularVelocity);
        energy+=pv*pv+.00375f*pw*pw+2*9.81f*s.T(projectile).position.y;
        peakEnergy=std::max(peakEnergy,energy);
    }
    std::cout<<"projectile peak spin="<<peakSpin<<" total energy="<<peakEnergy<<'\n';
    Require(peakSpin>1,"projectile failed to rotate cubes");
    Require(peakEnergy<240,"collision injected excessive energy");
    s.Run(15); float finalSpeed=0;
    for(auto e:cubes) if(std::abs(s.T(e).position.x)<3.8f && std::abs(s.T(e).position.z)<3.8f)
        finalSpeed=std::max(finalSpeed,Length(s.R(e).velocity));
    Require(finalSpeed<.05f,"post-impact bodies did not settle");
}
static void WakeAndAir()
{
    Scene s; auto floor=s.Ground(); auto cube=s.Add({0,.09f,0},{.09f,.09f,.09f}); s.Run(3);
    Require(s.R(cube).sleeping,"wake test requires resting cube");
    s.world.DestroyEntity(floor); s.Run(.25f);
    Require(!s.R(cube).sleeping && s.T(cube).position.y<-.1f,"unsupported body stayed asleep");
    Scene air; auto free=air.Add({0,3,0},{.09f,.09f,.09f}); air.R(free).sleeping=true; air.Run(.25f);
    Require(!air.R(free).sleeping && air.T(free).position.y<2.8f,"airborne sleep");
    Scene slow; slow.physics=PhysicsSystem(nullptr,.001f); auto e=slow.Add({0,3,0},{.09f,.09f,.09f}); slow.Run(3);
    Require(!slow.R(e).sleeping,"slow airborne body slept");
    Scene torque; torque.Ground(); auto t=torque.Add({0,.09f,0},{.09f,.09f,.09f}); torque.Run(3); torque.R(t).torque={0,0,1}; torque.Run(.1f);
    Require(!torque.R(t).sleeping && Length(torque.R(t).angularVelocity)>.1f,"torque failed to wake");
}
static void Spheres()
{
    Scene s; s.Ground(); auto ball=s.Add({0,1,0},{.1f,.1f,.1f},false,ColliderType::Sphere); s.Run(8);
    Require(std::abs(s.T(ball).position.y-.1f)<.002f && s.R(ball).sleeping,"sphere ground rest");
    Scene ramp; const float angle=.25f;
    ramp.Add({0,0,0},{2,.05f,1},true,ColliderType::Box,{0,0,angle});
    auto e=ramp.Add({0,.155f,0},{.1f,.1f,.1f},false,ColliderType::Sphere); ramp.Run(.8f);
    std::cout<<"ramp x="<<ramp.T(e).position.x<<" y="<<ramp.T(e).position.y<<" spin="<<Length(ramp.R(e).angularVelocity)<<'\n';
    const auto p=ramp.T(e).position;
    Require(p.x<-.1f && Length(ramp.R(e).angularVelocity)>.5f,"sphere failed to roll down ramp");
    Require(-std::sin(angle)*p.x+std::cos(angle)*p.y>.145f,"sphere penetrated ramp");
    Scene collision; auto a=collision.Add({-.3f,0,0},{.1f,.1f,.1f},false,ColliderType::Sphere); auto b=collision.Add({.3f,0,0},{.1f,.1f,.1f},false,ColliderType::Sphere);
    collision.R(a).useGravity=collision.R(b).useGravity=false; collision.R(a).velocity={2,0,0}; collision.R(b).velocity={-2,0,0};
    collision.Run(.3f); Require(collision.T(a).position.x<collision.T(b).position.x,"sphere-sphere crossing");
}
static void BroadphaseAndFast()
{
    Scene s; auto wall=s.Add({0,0,0},{.025f,2,2},true);
    auto e=s.Add({-1,0,0},{.075f,.075f,.075f}); s.R(e).useGravity=false; s.R(e).velocity={50,0,0}; s.Run(.1f);
    Require(s.T(e).position.x<0,"fast body tunneled through thin wall");
    Scene large; auto a=large.Add({-1,0,0},{.75f,.75f,.75f}); auto b=large.Add({1,0,0},{.75f,.75f,.75f});
    large.R(a).useGravity=large.R(b).useGravity=false; large.R(a).velocity={1,0,0}; large.R(b).velocity={-1,0,0}; large.Run(1);
    Require(large.T(b).position.x-large.T(a).position.x>1.49f,"large dynamic broadphase missed collision");
    Scene crossing;
    crossing.Add({0,-.1f,0},{.8f,.1f,.1f},true);
    auto bar=crossing.Add({0,.1f,0},{.1f,.1f,.8f}); crossing.Run(4);
    Require(std::abs(crossing.T(bar).position.y-.1f)<.002f,"crossed faces without contained vertices");
    Scene staticSphere;
    staticSphere.Add({0,0,0},{1,1,1},true,ColliderType::Sphere);
    auto ball=staticSphere.Add({0,1.99f,0},{1,1,1},false,ColliderType::Sphere);
    staticSphere.Run(2); Require(staticSphere.T(ball).position.y>1.99f,"large static sphere broadphase miss");
}
static void WorldAngularVelocity()
{
    Scene s; auto e=s.Add({0,0,0},{.1f,.1f,.1f},false,ColliderType::Box,{.3f,.6f,.4f});
    s.R(e).useGravity=false; s.R(e).angularDampingMultiplier=0; s.R(e).angularVelocity={0,1,0};
    auto worldYOfLocalY=[](Vec3 r) { return std::sin(r.z)*std::sin(r.y)*std::sin(r.x)+std::cos(r.z)*std::cos(r.x); };
    const float initial=worldYOfLocalY(s.T(e).rotation); s.Run(2);
    Require(std::abs(worldYOfLocalY(s.T(e).rotation)-initial)<.0002f,"omega integrated as Euler rates");
}
static void UndampedImpacts()
{
    Scene s;
    s.physics=PhysicsSystem(nullptr,0.0f,1.0f,3,0.0f,0.6f,12);
    s.Add({-1,0,0},{.025f,1,1},true);
    s.Add({1,0,0},{.025f,1,1},true);
    s.Add({0,0,-1},{1,1,.025f},true);
    s.Add({0,0,1},{1,1,.025f},true);
    std::vector<Entity> bodies;
    for(int i=0;i<4;++i)
    {
        auto e=s.Add({-.6f+.4f*i,0,0},{.09f,.09f,.09f},false,ColliderType::Box,{.1f*i,.17f*i,.21f*i});
        s.R(e).useGravity=false; s.R(e).linearDampingMultiplier=0; s.R(e).angularDampingMultiplier=0;
        s.R(e).velocity={i%2 ? -8.0f:8.0f,0,i%2 ? 3.0f:-3.0f};
        s.R(e).angularVelocity={float(i),float(i*2),float(i*3)};
        s.world.GetComponent<ColliderComponent>(e)->restitution=.3f;
        bodies.push_back(e);
    }
    auto energy=[&]() { float result=0;for(auto e:bodies) {float v=Length(s.R(e).velocity),w=Length(s.R(e).angularVelocity);result+=.5f*v*v+.0027f*w*w;}return result; };
    float initial=energy(),peak=initial;
    for(int i=0;i<1200;++i) {s.physics.Update(s.world,1.0f/120);peak=std::max(peak,energy());}
    std::cout<<"undamped impacts energy initial="<<initial<<" peak="<<peak<<" final="<<energy()<<'\n';
    Require(peak<initial*1.02f,"undamped collision energy grew");
    Require(energy()<initial*.5f,"inelastic impacts failed to dissipate energy");
}
int main()
{
    try {
        RestAndTilt(); Pyramid(1.0f/60); Pyramid(1.0f/30); Pyramid(1.0f/144); Pyramid(1.0f/60,true);
        WakeAndAir(); Spheres(); BroadphaseAndFast(); WorldAngularVelocity(); UndampedImpacts();
        std::cout<<"All physics regression scenarios passed\n"; return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
