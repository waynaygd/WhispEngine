#include "ecs/World.h"
#include "ecs/systems/PhysicsSystem.h"
#include "ecs/components/TransformComponent.h"
#include "ecs/components/RigidbodyComponent.h"
#include "ecs/components/ColliderComponent.h"
#include "jobs/JobSystem.h"
#include "ecs/PhysicsStressScene.h"
#include "core/PhysicsDiagnosticSample.h"
#include "core/FixedStepClock.h"
#include "ecs/RenderInterpolation.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <atomic>
#include <string>
#include <algorithm>
#include <chrono>

#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#else
#define ZoneScopedN(name) ((void)0)
#endif

enum class PhysicsBenchmarkMode
{
    Serial,
    Parallel
};

using namespace ecs;
static float Length(Vec3 v) { return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z); }
static Vec3 Sub(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
static bool Near(
    Vec3 a,
    Vec3 b,
    float epsilon = 0.00001f)
{
    return Length(Sub(a, b)) <= epsilon;
}
static void Require(bool yes, const char* message) { if (!yes) throw std::runtime_error(message); }
struct Scene
{
    explicit Scene(EventBus* events = nullptr) : physics(events,9.81f,0.985f,3,0.05f,0.85f,12) {}
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

static void ParallelIntegration()
{
    constexpr int bodyCount = 512;
    constexpr float dt = 1.0f / 60.0f;
    constexpr int frames = 60;

    // ---------------------------------------------------------
    // JobSystem sanity check
    // ---------------------------------------------------------

    JobSystem jobs;

    Require(
        jobs.Initialize(),
        "JobSystem failed to initialize");

    std::vector<int> dispatchHits(
        bodyCount,
        0);

    auto dispatchTest =
        jobs.Dispatch(
            bodyCount,
            64,
            [&](std::uint32_t begin,
                std::uint32_t end,
                std::uint32_t /*threadIndex*/)
            {
                for (std::uint32_t i = begin;
                    i < end;
                    ++i)
                {
                    dispatchHits[i] += 1;
                }
            });

    Require(
        dispatchTest != nullptr,
        "JobSystem Dispatch returned null");

    jobs.Wait(dispatchTest);

    for (int value : dispatchHits)
    {
        Require(
            value == 1,
            "JobSystem Dispatch processed an item incorrectly");
    }

    // ---------------------------------------------------------
    // Serial and parallel worlds
    // ---------------------------------------------------------

    Scene serial;
    Scene parallel;

    parallel.physics.SetJobSystem(
        &jobs);

    std::vector<Entity> serialBodies;
    std::vector<Entity> parallelBodies;

    serialBodies.reserve(bodyCount);
    parallelBodies.reserve(bodyCount);

    // Keep bodies far enough apart that this test measures
    // integration rather than collision solving.
    for (int i = 0; i < bodyCount; ++i)
    {
        const int xIndex =
            i % 32;

        const int zIndex =
            i / 32;

        const Vec3 position{
            static_cast<float>(xIndex) * 2.0f,
            5.0f +
                static_cast<float>(i % 7) * 0.05f,
            static_cast<float>(zIndex) * 2.0f
        };

        const Vec3 rotation{
            0.01f * static_cast<float>(i % 5),
            0.015f * static_cast<float>(i % 7),
            0.02f * static_cast<float>(i % 3)
        };

        Entity serialEntity =
            serial.Add(
                position,
                { 0.09f, 0.09f, 0.09f },
                false,
                ColliderType::Box,
                rotation);

        Entity parallelEntity =
            parallel.Add(
                position,
                { 0.09f, 0.09f, 0.09f },
                false,
                ColliderType::Box,
                rotation);

        serialBodies.push_back(
            serialEntity);

        parallelBodies.push_back(
            parallelEntity);

        const float vx =
            -0.20f +
            static_cast<float>(i % 11) *
            0.04f;

        const float vy =
            -0.10f +
            static_cast<float>(i % 5) *
            0.05f;

        const float vz =
            -0.15f +
            static_cast<float>(i % 7) *
            0.05f;

        const Vec3 velocity{
            vx,
            vy,
            vz
        };

        const Vec3 angularVelocity{
            0.10f +
                static_cast<float>(i % 3) * 0.02f,

            -0.08f +
                static_cast<float>(i % 5) * 0.015f,

            0.05f +
                static_cast<float>(i % 7) * 0.01f
        };

        const Vec3 acceleration{
            0.01f *
                static_cast<float>(i % 3),

            0.0f,

            -0.01f *
                static_cast<float>(i % 4)
        };

        const Vec3 torque{
            0.001f *
                static_cast<float>(i % 5),

            0.002f *
                static_cast<float>(i % 3),

            -0.001f *
                static_cast<float>(i % 7)
        };

        auto configure =
            [&](Scene& scene,
                Entity entity)
            {
                auto& rb =
                    scene.R(entity);

                rb.useGravity = false;

                rb.velocity =
                    velocity;

                rb.angularVelocity =
                    angularVelocity;

                rb.acceleration =
                    acceleration;

                rb.torque =
                    torque;

                // Disable damping so the comparison focuses
                // purely on the integration result.
                rb.linearDampingMultiplier =
                    0.0f;

                rb.angularDampingMultiplier =
                    0.0f;
            };

        configure(
            serial,
            serialEntity);

        configure(
            parallel,
            parallelEntity);
    }

    // ---------------------------------------------------------
    // Simulate
    // ---------------------------------------------------------

    for (int frame = 0;
        frame < frames;
        ++frame)
    {
        serial.physics.Update(
            serial.world,
            dt);

        parallel.physics.Update(
            parallel.world,
            dt);
    }

    // ---------------------------------------------------------
    // Compare results
    // ---------------------------------------------------------

    float maxPositionDifference = 0.0f;
    float maxRotationDifference = 0.0f;
    float maxVelocityDifference = 0.0f;
    float maxAngularDifference = 0.0f;

    for (int i = 0;
        i < bodyCount;
        ++i)
    {
        const auto serialEntity =
            serialBodies[i];

        const auto parallelEntity =
            parallelBodies[i];

        const auto& serialTransform =
            serial.T(serialEntity);

        const auto& parallelTransform =
            parallel.T(parallelEntity);

        const auto& serialRb =
            serial.R(serialEntity);

        const auto& parallelRb =
            parallel.R(parallelEntity);

        maxPositionDifference =
            std::max(
                maxPositionDifference,
                Length(
                    Sub(
                        serialTransform.position,
                        parallelTransform.position)));

        maxRotationDifference =
            std::max(
                maxRotationDifference,
                Length(
                    Sub(
                        serialTransform.rotation,
                        parallelTransform.rotation)));

        maxVelocityDifference =
            std::max(
                maxVelocityDifference,
                Length(
                    Sub(
                        serialRb.velocity,
                        parallelRb.velocity)));

        maxAngularDifference =
            std::max(
                maxAngularDifference,
                Length(
                    Sub(
                        serialRb.angularVelocity,
                        parallelRb.angularVelocity)));

        Require(
            Near(
                serialTransform.position,
                parallelTransform.position),
            "parallel position differs from serial");

        Require(
            Near(
                serialTransform.rotation,
                parallelTransform.rotation),
            "parallel rotation differs from serial");

        Require(
            Near(
                serialRb.velocity,
                parallelRb.velocity),
            "parallel velocity differs from serial");

        Require(
            Near(
                serialRb.angularVelocity,
                parallelRb.angularVelocity),
            "parallel angular velocity differs from serial");
    }

    std::cout
        << "parallel integration bodies="
        << bodyCount
        << " positionDiff="
        << maxPositionDifference
        << " rotationDiff="
        << maxRotationDifference
        << " velocityDiff="
        << maxVelocityDifference
        << " angularDiff="
        << maxAngularDifference
        << '\n';

    jobs.Shutdown();
}

static void ParallelNarrowphase()
{
    constexpr int pairCount = 320;
    constexpr float dt = 1.0f / 60.0f;

    JobSystem jobs;

    Require(
        jobs.Initialize(),
        "JobSystem failed to initialize for narrowphase test");

    Scene serial;
    Scene parallel;

    parallel.physics.SetJobSystem(
        &jobs);

    std::vector<Entity> serialBodies;
    std::vector<Entity> parallelBodies;

    serialBodies.reserve(pairCount * 2);
    parallelBodies.reserve(pairCount * 2);

    // 320 completely isolated collision pairs.
    // Each pair overlaps slightly, while neighbouring pairs
    // are far enough apart to never collide with one another.
    for (int pairIndex = 0;
        pairIndex < pairCount;
        ++pairIndex)
    {
        const int column =
            pairIndex % 20;

        const int row =
            pairIndex / 20;

        const float baseX =
            static_cast<float>(column) * 1.0f;

        const float baseZ =
            static_cast<float>(row) * 1.0f;

        const Vec3 posA{
            baseX - 0.075f,
            1.0f,
            baseZ
        };

        const Vec3 posB{
            baseX + 0.075f,
            1.0f,
            baseZ
        };

        const Vec3 half{
            0.1f,
            0.1f,
            0.1f
        };

        const Entity serialA =
            serial.Add(
                posA,
                half,
                false,
                ColliderType::Sphere);

        const Entity serialB =
            serial.Add(
                posB,
                half,
                false,
                ColliderType::Sphere);

        const Entity parallelA =
            parallel.Add(
                posA,
                half,
                false,
                ColliderType::Sphere);

        const Entity parallelB =
            parallel.Add(
                posB,
                half,
                false,
                ColliderType::Sphere);

        serialBodies.push_back(serialA);
        serialBodies.push_back(serialB);

        parallelBodies.push_back(parallelA);
        parallelBodies.push_back(parallelB);

        auto configurePair =
            [](Scene& scene,
                Entity a,
                Entity b)
            {
                auto& rbA =
                    scene.R(a);

                auto& rbB =
                    scene.R(b);

                rbA.useGravity = false;
                rbB.useGravity = false;

                rbA.linearDampingMultiplier = 0.0f;
                rbB.linearDampingMultiplier = 0.0f;

                rbA.angularDampingMultiplier = 0.0f;
                rbB.angularDampingMultiplier = 0.0f;

                rbA.velocity =
                { 0.5f, 0.0f, 0.0f };

                rbB.velocity =
                { -0.5f, 0.0f, 0.0f };

                rbA.sleeping = false;
                rbB.sleeping = false;
            };

        configurePair(
            serial,
            serialA,
            serialB);

        configurePair(
            parallel,
            parallelA,
            parallelB);
    }

    // One update is enough:
    // broadphase sees 320 isolated candidate pairs,
    // therefore the parallel scene must cross the
    // narrowphase parallel threshold of 256.
    serial.physics.Update(
        serial.world,
        dt);

    parallel.physics.Update(
        parallel.world,
        dt);

    float maxPositionDifference = 0.0f;
    float maxRotationDifference = 0.0f;
    float maxVelocityDifference = 0.0f;
    float maxAngularDifference = 0.0f;

    for (std::size_t i = 0;
        i < serialBodies.size();
        ++i)
    {
        const auto& serialTransform =
            serial.T(serialBodies[i]);

        const auto& parallelTransform =
            parallel.T(parallelBodies[i]);

        const auto& serialRb =
            serial.R(serialBodies[i]);

        const auto& parallelRb =
            parallel.R(parallelBodies[i]);

        maxPositionDifference =
            std::max(
                maxPositionDifference,
                Length(
                    Sub(
                        serialTransform.position,
                        parallelTransform.position)));

        maxRotationDifference =
            std::max(
                maxRotationDifference,
                Length(
                    Sub(
                        serialTransform.rotation,
                        parallelTransform.rotation)));

        maxVelocityDifference =
            std::max(
                maxVelocityDifference,
                Length(
                    Sub(
                        serialRb.velocity,
                        parallelRb.velocity)));

        maxAngularDifference =
            std::max(
                maxAngularDifference,
                Length(
                    Sub(
                        serialRb.angularVelocity,
                        parallelRb.angularVelocity)));

        Require(
            Near(
                serialTransform.position,
                parallelTransform.position),
            "parallel narrowphase position differs from serial");

        Require(
            Near(
                serialTransform.rotation,
                parallelTransform.rotation),
            "parallel narrowphase rotation differs from serial");

        Require(
            Near(
                serialRb.velocity,
                parallelRb.velocity),
            "parallel narrowphase velocity differs from serial");

        Require(
            Near(
                serialRb.angularVelocity,
                parallelRb.angularVelocity),
            "parallel narrowphase angular velocity differs from serial");
    }

    std::cout
        << "parallel narrowphase pairs="
        << pairCount
        << " bodies="
        << serialBodies.size()
        << " positionDiff="
        << maxPositionDifference
        << " rotationDiff="
        << maxRotationDifference
        << " velocityDiff="
        << maxVelocityDifference
        << " angularDiff="
        << maxAngularDifference
        << '\n';

    jobs.Shutdown();
}

struct TimingStats
{
    double medianMs = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
    double maxMs = 0.0;
};
static bool g_CollectDiagnostics = false;
static std::string g_DiagnosticPhase;
static std::vector<PhysicsDiagnosticSample> g_DiagnosticSamples;

static TimingStats CalculateTimingStats(
    std::vector<double> samples)
{
    Require(
        !samples.empty(),
        "benchmark has no timing samples");

    std::sort(
        samples.begin(),
        samples.end());

    auto percentile =
        [&](double value)
        {
            std::size_t rank =
                static_cast<std::size_t>(
                    std::ceil(
                        value *
                        static_cast<double>(
                            samples.size())));

            rank =
                std::max<std::size_t>(
                    rank,
                    1);

            rank =
                std::min(
                    rank,
                    samples.size());

            return samples[rank - 1];
        };

    return TimingStats{
        percentile(0.50),
        percentile(0.95),
        percentile(0.99), samples.back()
    };
}

static void PopulatePhysicsStressScene(
    Scene& scene)
{
    constexpr int side = 24;
    constexpr float spacing = 0.28f;

    scene.Ground();

    for (int z = 0;
        z < side;
        ++z)
    {
        for (int x = 0;
            x < side;
            ++x)
        {
            const float px =
                (static_cast<float>(x) -
                    static_cast<float>(side - 1) * 0.5f) *
                spacing;

            const float pz =
                (static_cast<float>(z) -
                    static_cast<float>(side - 1) * 0.5f) *
                spacing;

            scene.Add(
                {
                    px,
                    0.09f,
                    pz
                },
                {
                    0.09f,
                    0.09f,
                    0.09f
                });
        }
    }
}

static void WarmUpPhysicsScene(
    Scene& scene)
{
    constexpr int warmupFrames = 10;
    constexpr float dt = 1.0f / 60.0f;

    for (int frame = 0;
        frame < warmupFrames;
        ++frame)
    {
        scene.physics.Update(
            scene.world,
            dt);
    }
}

static void MeasurePhysicsScene(
    Scene& scene,
    std::vector<double>& samples,
    PhysicsBenchmarkMode mode)
{
    constexpr int frames = 30;
    constexpr float dt = 1.0f / 60.0f;

    for (int frame = 0;
        frame < frames;
        ++frame)
    {
        const auto begin =
            std::chrono::steady_clock::now();

        if (mode == PhysicsBenchmarkMode::Serial)
        {
            ZoneScopedN("PhysicsBenchmarkFrameSerial");

            scene.physics.Update(
                scene.world,
                dt);
        }
        else
        {
            ZoneScopedN("PhysicsBenchmarkFrameParallel");

            scene.physics.Update(
                scene.world,
                dt);
        }

        const auto end =
            std::chrono::steady_clock::now();

        const double milliseconds =
            std::chrono::duration<
            double,
            std::milli>(
                end - begin)
            .count();

        samples.push_back(
            milliseconds);
        if (g_CollectDiagnostics) g_DiagnosticSamples.push_back({g_DiagnosticPhase,
            mode == PhysicsBenchmarkMode::Parallel, static_cast<unsigned>(g_DiagnosticSamples.size()),
            milliseconds, scene.physics.GetStatistics()});
    }
}

static void PhysicsParallelBenchmark(
    bool waitForProfiler = false)
{
    constexpr int repetitions = 5;

    JobSystem jobs;

    Require(
        jobs.Initialize(),
        "benchmark JobSystem initialization failed");

    if (waitForProfiler)
    {
        std::cout
            << "\nTracy capture mode.\n"
            << "Connect Tracy Profiler to PhysicsRegression.exe,\n"
            << "then press Enter to start benchmark...\n";

        std::cin.get();
    }

    std::vector<double> serialSamples;
    std::vector<double> parallelSamples;

    serialSamples.reserve(
        repetitions * 30);

    parallelSamples.reserve(
        repetitions * 30);

    for (int repetition = 0;
        repetition < repetitions;
        ++repetition)
    {
        Scene serial;
        Scene parallel;
        g_DiagnosticPhase = "LegacyBenchmark_R" + std::to_string(repetition);

        PopulatePhysicsStressScene(
            serial);

        PopulatePhysicsStressScene(
            parallel);

        parallel.physics.SetJobSystem(
            &jobs);

        WarmUpPhysicsScene(serial);
        WarmUpPhysicsScene(parallel);

        // Alternate order to reduce systematic bias
        // from temperature / CPU boost / scheduler state.
        if ((repetition % 2) == 0)
        {
            {
                ZoneScopedN("PhysicsBenchmarkSerial");

                MeasurePhysicsScene(
                    serial,
                    serialSamples,
                    PhysicsBenchmarkMode::Serial);
            }

            {
                ZoneScopedN("PhysicsBenchmarkParallel");

                MeasurePhysicsScene(
                    parallel,
                    parallelSamples,
                    PhysicsBenchmarkMode::Parallel);
            }
        }
        else
        {
            {
                ZoneScopedN("PhysicsBenchmarkParallel");

                MeasurePhysicsScene(
                    parallel,
                    parallelSamples,
                    PhysicsBenchmarkMode::Parallel);
            }

            {
                ZoneScopedN("PhysicsBenchmarkSerial");

                MeasurePhysicsScene(
                    serial,
                    serialSamples,
                    PhysicsBenchmarkMode::Serial);
            }
        }
    }

    const TimingStats serial =
        CalculateTimingStats(
            serialSamples);

    const TimingStats parallel =
        CalculateTimingStats(
            parallelSamples);

    const double speedup =
        parallel.medianMs > 0.0
        ? serial.medianMs /
        parallel.medianMs
        : 0.0;

    std::cout
        << "\n=== WhispPhysics benchmark ===\n"
        << "scene: 576 dynamic boxes + 1 static ground\n"
        << "repetitions: "
        << repetitions
        << '\n'
        << "warm-up frames per repetition: 10\n"
        << "measured simulation frames per repetition: 30\n"
        << "samples per mode: "
        << serialSamples.size()
        << '\n'
        << '\n'
        << "Serial frame time:\n"
        << "  median = "
        << serial.medianMs
        << " ms\n"
        << "  p95    = "
        << serial.p95Ms
        << " ms\n"
        << "  p99    = "
        << serial.p99Ms
        << " ms\n"
        << "  max    = " << serial.maxMs << " ms\n"
        << '\n'
        << "Parallel frame time:\n"
        << "  median = "
        << parallel.medianMs
        << " ms\n"
        << "  p95    = "
        << parallel.p95Ms
        << " ms\n"
        << "  p99    = "
        << parallel.p99Ms
        << " ms\n"
        << "  max    = " << parallel.maxMs << " ms\n"
        << '\n'
        << "Median speedup: "
        << speedup
        << "x\n"
        << "==============================\n";

    jobs.Shutdown();
}

static void StressLifecycle()
{
    World world;
    std::vector<Entity> entities;
    const auto unrelated = world.CreateEntity();
    for (int count : {100, 250, 500, 1000}) {
        for (int repeat = 0; repeat < 3; ++repeat) {
            CreatePhysicsStressScene(world, entities, count);
            Require(world.GetAliveCount() == count + 2, "stress creation count");
            for (auto e : entities) {
                Require(world.HasComponent<TransformComponent>(e) && world.HasComponent<ColliderComponent>(e) &&
                    world.HasComponent<RigidbodyComponent>(e) && world.HasComponent<MeshRendererComponent>(e) &&
                    world.HasComponent<MaterialComponent>(e), "stress missing visual/physics component");
            }
            const auto old = entities.back();
            CreatePhysicsStressScene(world, entities, count);
            Require(!world.IsAlive(old), "stress restart resurrected old handle");
            ClearPhysicsStressScene(world, entities);
            Require(world.GetAliveCount() == 1 && world.IsAlive(unrelated), "stress cleanup removed unrelated entities");
        }
    }
    world.Clear();
    world.CreateEntity();
    Require(!world.IsAlive(unrelated), "World Clear resurrected stale entity");
    JobSystem jobs;
    jobs.Initialize();
    std::atomic<int> completed{0};
    for (int i = 0; i < 200; ++i) (void)jobs.Execute([&] { ++completed; });
    jobs.WaitAll();
    Require(completed == 200, "discarded task handle lost work");
    (void)jobs.Execute([&] { ++completed; });
    jobs.Shutdown();
    Require(completed == 201, "shutdown did not drain tasks");
    jobs.Initialize(); jobs.Shutdown();
}

static void VisualStressBenchmark(bool endurance)
{
    JobSystem jobs; jobs.Initialize();
    for (int count : {100, 250, 500, 1000}) {
        Scene serial, parallel;
        if (endurance && count != 500) continue;
        std::vector<Entity> a, b;
        CreatePhysicsStressScene(serial.world, a, count);
        CreatePhysicsStressScene(parallel.world, b, count);
        serial.physics.SetParallel(false);
        parallel.physics.SetJobSystem(&jobs);
        std::vector<double> st, pt;
        PhysicsSystem::Statistics sum{};
        const int frames = endurance && count == 500 ? 7200 : 600;
        g_DiagnosticPhase = "Stress_" + std::to_string(count);
        for (int frame = 0; frame < frames; ++frame) {
            // Alternate the ordering to reduce order bias.
            auto update = [&](Scene& scene, std::vector<double>& samples) {
                scene.physics.Update(scene.world, 1.0f / 60.0f);
                samples.push_back(scene.physics.GetStatistics().totalMs);
                if (g_CollectDiagnostics) g_DiagnosticSamples.push_back({g_DiagnosticPhase,
                    &scene == &parallel, static_cast<unsigned>(frame),
                    scene.physics.GetStatistics().totalMs, scene.physics.GetStatistics()});
            };
            if (frame % 2) { update(parallel, pt); update(serial, st); }
            else { update(serial, st); update(parallel, pt); }
            const auto& stats = parallel.physics.GetStatistics();
            sum.integrateMs += stats.integrateMs; sum.broadphaseMs += stats.broadphaseMs;
            sum.narrowphaseMs += stats.narrowphaseMs; sum.solverMs += stats.solverMs;
            sum.poseMs += stats.poseMs; sum.projectionMs += stats.projectionMs;
            sum.substeps = std::max(sum.substeps, stats.substeps);
            sum.contacts = std::max(sum.contacts, stats.contacts);
            if (frame % 600 == 599) {
                const auto window = CalculateTimingStats(std::vector<double>(pt.end() - 600, pt.end()));
                std::cout << "stress window count=" << count << " seconds=" << (frame+1)/60
                    << " parallel median=" << window.medianMs << " p99=" << window.p99Ms
                    << " substeps=" << stats.substeps << " sleeping=" << stats.sleeping << std::endl;
            }
        }
        for (std::size_t i = 1; i < a.size(); ++i) {
            Require(Near(serial.T(a[i]).position, parallel.T(b[i]).position), "stress serial/parallel pose mismatch");
            Require(Near(serial.R(a[i]).velocity, parallel.R(b[i]).velocity), "stress serial/parallel velocity mismatch");
            Require(Near(serial.T(a[i]).rotation, parallel.T(b[i]).rotation), "stress serial/parallel rotation mismatch");
            Require(Near(serial.R(a[i]).angularVelocity, parallel.R(b[i]).angularVelocity), "stress angular velocity mismatch");
            Require(std::isfinite(Length(parallel.T(b[i]).position)) && parallel.T(b[i]).position.y > -0.1f,
                "stress body escaped floor or became non-finite");
        }
        const auto ss = CalculateTimingStats(st), ps = CalculateTimingStats(pt);
        std::cout << "stress count=" << count << " frames=" << frames
            << " Serial median/p95/p99=" << ss.medianMs << '/' << ss.p95Ms << '/' << ss.p99Ms
            << " Parallel=" << ps.medianMs << '/' << ps.p95Ms << '/' << ps.p99Ms
            << " maxSerial/Parallel=" << ss.maxMs << '/' << ps.maxMs
            << " maxSubsteps=" << sum.substeps << " peakContacts=" << sum.contacts << '\n'
            << "parallel stage mean ms: integrate=" << sum.integrateMs/frames
            << " broadphase=" << sum.broadphaseMs/frames << " narrowphase=" << sum.narrowphaseMs/frames
            << " solver=" << sum.solverMs/frames << " pose=" << sum.poseMs/frames
            << " projection=" << sum.projectionMs/frames << std::endl;
        for (int i = 0; i < 10; ++i) {
            parallel.physics.SetEnabled(false);
            const auto before = parallel.T(b.back()).position;
            parallel.physics.Update(parallel.world, 1.0f/60);
            Require(Near(before, parallel.T(b.back()).position), "Stop did not pause physics");
            Require(parallel.physics.GetStatistics().bodies == count + 1, "paused body statistics lost live bodies");
            parallel.physics.SetEnabled(true);
            parallel.physics.Update(parallel.world, 1.0f/60);
        }
        ClearPhysicsStressScene(serial.world, a); ClearPhysicsStressScene(parallel.world, b);
        serial.physics.ResetState(); parallel.physics.ResetState();
        Require(serial.world.GetAliveCount() == 0 && parallel.world.GetAliveCount() == 0, "stress leaked entities");
    }
    jobs.Shutdown();
}

static void DiagnoseRunaway()
{
    Scene scene; scene.Ground();
    // Original stress layout extends to x=48, outside the demo floor (x=+-4).
    const auto escaped = scene.Add({48, 10, 0}, {.5f, .5f, .5f});
    for (int frame = 0; frame < 3600; ++frame) {
        scene.physics.Update(scene.world, 1.0f/60);
        if ((frame + 1) % 600 == 0)
            std::cout << "runaway seconds=" << (frame + 1)/60 << " y=" << scene.T(escaped).position.y
                << " speed=" << Length(scene.R(escaped).velocity)
                << " substeps=" << scene.physics.GetStatistics().substeps << '\n';
    }
}

static void BroadphaseCacheRegression()
{
    Scene scene;
    scene.physics.SetBroadphaseValidation(true);
    auto floor=scene.Ground(); auto cube=scene.Add({0,1,0},{.2f,.2f,.2f});
    scene.Run(4);
    scene.physics.Update(scene.world,1.0f/60);
    auto stats=scene.physics.GetStatistics();
    Require(stats.broadphaseRebuilds==0 && stats.broadphaseReuses==8,"rest broadphase was rebuilt");
    Require(stats.broadphaseBufferGrowths==0 && stats.aabbComputations==0,"rest broadphase allocates/recomputes AABB");
    scene.T(floor).position.y=-2;
    scene.physics.Update(scene.world,1.0f/60);
    Require(scene.physics.GetStatistics().broadphaseRebuilds>0,"moved static did not invalidate grid");
    Require(!scene.R(cube).sleeping,"moved support did not wake sleeping body");
    scene.world.GetComponent<ColliderComponent>(cube)->offset.x=.7f;
    scene.world.GetComponent<ColliderComponent>(cube)->halfExtents={.3f,.2f,.1f};
    scene.T(cube).rotation={.1f,.3f,.5f};
    scene.physics.Update(scene.world,1.0f/60);
    scene.world.GetComponent<ColliderComponent>(cube)->type=ColliderType::Sphere;
    scene.R(cube).isStatic=true;
    scene.physics.Update(scene.world,1.0f/60);
    scene.world.DestroyEntity(cube);
    cube=scene.Add({0,0,0},{.2f,.2f,.2f});
    scene.physics.Update(scene.world,1.0f/60);
    scene.world.Clear(); // Intentionally no ResetState: snapshots must detect generations.
    floor=scene.Ground(); cube=scene.Add({0,.2f,0},{.2f,.2f,.2f});
    scene.physics.Update(scene.world,1.0f/60);
    Require(scene.physics.GetStatistics().aabbComputations>=2,"Clear left stale geometry");
    scene.world.Clear(); scene.physics.Update(scene.world,1.0f/60);
    Require(scene.physics.GetStatistics().pairs==0,"Clear left stale pairs");
    // Dense pairs force flat seen-table growth; both occupied cells and overflow,
    // rotated boxes/spheres, negative coordinates and changing geometry hit oracle.
    for(int i=0;i<80;++i) scene.Add({float(i%5)*.15f-1,float((i/5)%4)*.15f,float(i/20)*.15f},
        i<4 ? Vec3{2,2,2} : Vec3{.13f,.17f,.21f}, i%9==0,
        i%3 ? ColliderType::Box : ColliderType::Sphere,{.1f*float(i%3),.17f,.03f*float(i%4)});
    for(int i=0;i<20;++i) scene.physics.Update(scene.world,1.0f/60);
    scene.physics.ResetState(); scene.physics.Update(scene.world,1.0f/60);
    std::cout << "Broadphase cache/oracle regression passed\n";
}
static void FixedTimestepRegression()
{
    JobSystem jobs; Require(jobs.Initialize(),"fixed jobs init");
    struct State { Vec3 p,r,v,w; bool sleeping; };
    std::vector<State> reference;
    std::vector<std::pair<Entity, Entity>> referenceEvents;
    for(bool parallel : {false,true}) for(int fps : {30,60,120,144}) {
        EventBus events; std::vector<std::pair<Entity, Entity>> collisionEvents;
        events.SubscribeCollision([&](const CollisionEvent& event){collisionEvents.emplace_back(event.a,event.b);});
        Scene scene(&events); scene.physics.SetParallel(parallel); scene.physics.SetJobSystem(&jobs);
        std::vector<Entity> entities; CreatePhysicsStressScene(scene.world,entities,300);
        FixedStepClock clock; RenderInterpolation interpolation; int ticks=0;
        for(int frame=0;frame<fps*4;++frame) {
            const auto plan=clock.Advance(1.0/fps);
            Require(plan.droppedSeconds==0,"fixed normal frame lost time");
            for(int tick=0;tick<plan.ticks;++tick) {
                interpolation.BeginTick(scene.world);
                scene.physics.Update(scene.world,float(FixedStepClock::TickSeconds));
                interpolation.EndTick(scene.world);++ticks;
            }
            interpolation.SetEnabled(fps != 30 && frame % 120 < 60);
            interpolation.SetFrame(scene.world,true,clock.Accumulator()/FixedStepClock::TickSeconds);
            for(auto e:entities) {
                const auto before=scene.T(e);const auto renderPose=interpolation.Resolve(e,before);
                Require(std::isfinite(renderPose.position.x) && Near(before.position,scene.T(e).position)&&Near(before.rotation,scene.T(e).rotation),"interpolation changed physics Transform");
            }
        }
        Require(ticks==240 && clock.Accumulator()<1e-9,"render rate changed tick count");
        std::vector<State> states;
        for(auto e:entities) states.push_back({scene.T(e).position,scene.T(e).rotation,scene.R(e).velocity,scene.R(e).angularVelocity,scene.R(e).sleeping});
        if(reference.empty()) { reference=states; referenceEvents=collisionEvents; }
        else for(std::size_t i=0;i<states.size();++i) {
            Require(Near(states[i].p,reference[i].p)&&Near(states[i].r,reference[i].r)&&Near(states[i].v,reference[i].v)&&Near(states[i].w,reference[i].w)&&states[i].sleeping==reference[i].sleeping,"fixed result depends on visual FPS/mode");
        }
        Require(collisionEvents==referenceEvents,"fixed collision event sequence depends on render FPS/mode");
        auto stall=clock.Advance(2.0);
        Require(stall.ticks==4 && std::abs(stall.droppedSeconds-(2.0-4.0/60))<1e-9,"slow frame policy");
        Require(clock.Accumulator()<FixedStepClock::TickSeconds && clock.Advance(0).ticks==0,"unbounded catch-up");
        clock.Advance(.01); clock.Advance(5,false);
        Require(clock.Accumulator()==0 && clock.Advance(.005).ticks==0,"pause accumulated time");
        clock.Reset(); Require(clock.DroppedSeconds()==0 && clock.Accumulator()==0,"restart clock reset");
        std::cout << "fixed fps="<<fps<<" parallel="<<parallel<<" ticks="<<ticks<<" events="<<collisionEvents.size()<<" passed\n";
    }
    // A callback may clear ECS after jobs finish; no cached component references.
    EventBus events; Scene scene(&events); bool cleared=false;
    events.SubscribeCollision([&](const CollisionEvent&){if(!cleared){scene.world.Clear();cleared=true;}});
    scene.Ground();scene.Add({0,.19f,0},{.2f,.2f,.2f});
    scene.physics.Update(scene.world,1.0f/60);
    Require(cleared && scene.world.GetAliveCount()==0,"collision callback clear failed");
    scene.physics.Update(scene.world,1.0f/60);
    jobs.Shutdown();
}
static void FixedSystemPhaseRegression()
{
    struct Counter : ISystem { bool fixed; int count=0; explicit Counter(bool f):fixed(f){} const char* Name() const override{return "Counter";} bool IsFixedUpdate() const override{return fixed;} void Update(World&,float) override{++count;} };
    World world;auto& fixed=world.AddSystem<Counter>(true);auto& frame=world.AddSystem<Counter>(false);
    world.UpdateFrameSystems(.01f);world.UpdateFrameSystems(.01f);world.UpdateFixedSystems(1.0f/60);
    Require(fixed.count==1 && frame.count==2,"frame/fixed stages run twice or skip systems");
}

int main(
    int argc,
    char** argv)
{
    try {
        if (argc > 1)
        {
            const std::string mode =
                argv[1];
            g_CollectDiagnostics = argc > 2;
            auto saveDiagnostics = [&] {
                if (g_CollectDiagnostics) Require(SavePhysicsDiagnostics(argv[2], g_DiagnosticSamples), "cannot save diagnostics CSV");
            };
            if (mode == "--diagnose-runaway") { DiagnoseRunaway(); return 0; }

            if (mode == "--benchmark")
            {
                PhysicsParallelBenchmark(false);
                saveDiagnostics();
                return 0;
            }
            if (mode == "--stress" || mode == "--stress-long") {
                StressLifecycle();
                VisualStressBenchmark(mode == "--stress-long");
                saveDiagnostics();
                return 0;
            }

            if (mode == "--benchmark-tracy")
            {
                PhysicsParallelBenchmark(true);
                return 0;
            }
        }

        BroadphaseCacheRegression(); FixedTimestepRegression(); FixedSystemPhaseRegression();
        RestAndTilt(); Pyramid(1.0f/60); Pyramid(1.0f/30); Pyramid(1.0f/144); Pyramid(1.0f/60,true);
        WakeAndAir(); Spheres(); BroadphaseAndFast(); WorldAngularVelocity(); UndampedImpacts(); ParallelIntegration(); ParallelNarrowphase(); StressLifecycle();
        std::cout<<"All physics regression scenarios passed\n"; return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
