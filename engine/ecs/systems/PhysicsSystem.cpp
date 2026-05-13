#include "PhysicsSystem.h"
#include "../World.h"
#include "../components/ColliderComponent.h"
#include "../components/RigidbodyComponent.h"
#include "../components/TransformComponent.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
using ecs::Vec3;
static Vec3 Add(const Vec3&a,const Vec3&b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static Vec3 Sub(const Vec3&a,const Vec3&b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static Vec3 Scale(const Vec3&v,float s){return {v.x*s,v.y*s,v.z*s};}
static float Dot(const Vec3&a,const Vec3&b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static float Abs(float v){ return v >= 0.0f ? v : -v; }
static Vec3 RotatedAabbHalfExtents(const Vec3& localHalf, const Vec3& rot)
{
    const float cx = std::cos(rot.x), sx = std::sin(rot.x);
    const float cy = std::cos(rot.y), sy = std::sin(rot.y);
    const float cz = std::cos(rot.z), sz = std::sin(rot.z);
    const float r00 = cz * cy;
    const float r01 = cz * sy * sx - sz * cx;
    const float r02 = cz * sy * cx + sz * sx;
    const float r10 = sz * cy;
    const float r11 = sz * sy * sx + cz * cx;
    const float r12 = sz * sy * cx - cz * sx;
    const float r20 = -sy;
    const float r21 = cy * sx;
    const float r22 = cy * cx;
    return Vec3{
        Abs(r00) * localHalf.x + Abs(r01) * localHalf.y + Abs(r02) * localHalf.z,
        Abs(r10) * localHalf.x + Abs(r11) * localHalf.y + Abs(r12) * localHalf.z,
        Abs(r20) * localHalf.x + Abs(r21) * localHalf.y + Abs(r22) * localHalf.z
    };
}
struct BodyRef
{
    ecs::Entity entity{};
    ecs::TransformComponent* transform = nullptr;
    ecs::ColliderComponent* collider = nullptr;
    ecs::RigidbodyComponent* rigidbody = nullptr;
};
}

namespace ecs {
void PhysicsSystem::Update(World& world, float dt)
{
    if (dt <= 0.0f)
        return;
    const float gravity = m_Gravity;
    const float linearDamping = m_LinearDamping;
    const float baseStep = 1.0f / 120.0f;
    const int dynamicSubsteps = static_cast<int>(std::ceil(dt / baseStep));
    const int configuredSubsteps = m_Substeps > 0 ? m_Substeps : 1;
    const int substeps = std::max(configuredSubsteps, std::min(dynamicSubsteps, 16));
    const float stepDt = dt / static_cast<float>(substeps);
    for (int step = 0; step < substeps; ++step)
    {
    world.ForEach<TransformComponent, RigidbodyComponent>([&](Entity, TransformComponent& t, RigidbodyComponent& rb){
        if (rb.isStatic || !rb.simulatePhysics) return;
        if (rb.useGravity) rb.velocity.y -= gravity * stepDt;
        rb.velocity.x *= linearDamping;
        rb.velocity.z *= linearDamping;
        if (Abs(rb.velocity.x) < 0.0005f) rb.velocity.x = 0.0f;
        if (Abs(rb.velocity.z) < 0.0005f) rb.velocity.z = 0.0f;
        rb.velocity = Add(rb.velocity, Scale(rb.acceleration, stepDt));
        t.position = Add(t.position, Scale(rb.velocity, stepDt));
    });

    std::vector<BodyRef> bodies;
    bodies.reserve(128);
    world.ForEach<ColliderComponent, TransformComponent, RigidbodyComponent>([&](Entity e, ColliderComponent& c, TransformComponent& t, RigidbodyComponent& rb){
        if (c.type == ColliderType::Box)
            bodies.push_back(BodyRef{ e, &t, &c, &rb });
    });

    for (std::size_t i = 0; i < bodies.size(); ++i)
    {
        for (std::size_t j = i + 1; j < bodies.size(); ++j)
        {
            BodyRef& a = bodies[i];
            BodyRef& b = bodies[j];
            if ((!a.rigidbody->simulatePhysics && !a.rigidbody->isStatic) ||
                (!b.rigidbody->simulatePhysics && !b.rigidbody->isStatic))
                continue;
            if (a.rigidbody->isStatic && b.rigidbody->isStatic)
                continue;

            Vec3 ac = Add(a.transform->position, a.collider->offset);
            Vec3 bc = Add(b.transform->position, b.collider->offset);
            Vec3 d = Sub(ac, bc);
            const Vec3 aHalf = RotatedAabbHalfExtents(a.collider->halfExtents, a.transform->rotation);
            const Vec3 bHalf = RotatedAabbHalfExtents(b.collider->halfExtents, b.transform->rotation);
            float ox = (aHalf.x + bHalf.x) - Abs(d.x);
            float oy = (aHalf.y + bHalf.y) - Abs(d.y);
            float oz = (aHalf.z + bHalf.z) - Abs(d.z);
            if (ox <= 0 || oy <= 0 || oz <= 0)
                continue;

            float minPen = ox; int axis = 0;
            if (oy < minPen) { minPen = oy; axis = 1; }
            if (oz < minPen) { minPen = oz; axis = 2; }

            Vec3 sep{};
            if (axis == 0) sep.x = d.x >= 0 ? minPen : -minPen;
            if (axis == 1) sep.y = d.y >= 0 ? minPen : -minPen;
            if (axis == 2) sep.z = d.z >= 0 ? minPen : -minPen;

            const float invMassA = (a.rigidbody->isStatic || a.rigidbody->mass <= 0.0001f) ? 0.0f : (1.0f / a.rigidbody->mass);
            const float invMassB = (b.rigidbody->isStatic || b.rigidbody->mass <= 0.0001f) ? 0.0f : (1.0f / b.rigidbody->mass);
            const float invMassSum = invMassA + invMassB;
            if (invMassSum <= 0.0f)
                continue;

            const float slop = 0.001f;
            const float percent = 0.8f;
            const float correctionScale = std::max(minPen - slop, 0.0f) * percent / invMassSum;
            const Vec3 correction = Scale(sep, correctionScale / (minPen > 0.0f ? minPen : 1.0f));
            if (invMassA > 0.0f)
                a.transform->position = Add(a.transform->position, Scale(correction, invMassA));
            if (invMassB > 0.0f)
                b.transform->position = Sub(b.transform->position, Scale(correction, invMassB));

            const float restitution = (a.collider->restitution + b.collider->restitution) > 0.0f
                ? (a.collider->restitution + b.collider->restitution) * 0.5f
                : m_DefaultRestitution;
            const float friction = (a.collider->friction + b.collider->friction) > 0.0f
                ? (a.collider->friction + b.collider->friction) * 0.5f
                : m_DefaultFriction;
            Vec3 normal{};
            if (axis == 0) normal.x = (d.x >= 0.0f) ? 1.0f : -1.0f;
            if (axis == 1) normal.y = (d.y >= 0.0f) ? 1.0f : -1.0f;
            if (axis == 2) normal.z = (d.z >= 0.0f) ? 1.0f : -1.0f;
            const Vec3 rv = Sub(a.rigidbody->velocity, b.rigidbody->velocity);
            const float velAlongNormal = Dot(rv, normal);
            if (velAlongNormal < 0.0f)
            {
                const float j = -(1.0f + restitution) * velAlongNormal / invMassSum;
                const Vec3 impulse = Scale(normal, j);
                if (invMassA > 0.0f)
                    a.rigidbody->velocity = Add(a.rigidbody->velocity, Scale(impulse, invMassA));
                if (invMassB > 0.0f)
                    b.rigidbody->velocity = Sub(b.rigidbody->velocity, Scale(impulse, invMassB));
            }

            const Vec3 rvAfter = Sub(a.rigidbody->velocity, b.rigidbody->velocity);
            Vec3 tangent = Sub(rvAfter, Scale(normal, Dot(rvAfter, normal)));
            const float tangentLenSq = Dot(tangent, tangent);
            if (tangentLenSq > 0.000001f)
            {
                const float invTangentLen = 1.0f / std::sqrt(tangentLenSq);
                tangent = Scale(tangent, invTangentLen);
                const float jt = -Dot(rvAfter, tangent) / invMassSum;
                const float maxFriction = friction * minPen * 25.0f;
                const float clampedJt = std::max(-maxFriction, std::min(jt, maxFriction));
                const Vec3 frictionImpulse = Scale(tangent, clampedJt);
                if (invMassA > 0.0f)
                    a.rigidbody->velocity = Add(a.rigidbody->velocity, Scale(frictionImpulse, invMassA));
                if (invMassB > 0.0f)
                    b.rigidbody->velocity = Sub(b.rigidbody->velocity, Scale(frictionImpulse, invMassB));
            }
            if (m_EventBus) m_EventBus->PublishCollision(CollisionEvent{ a.entity, b.entity });
        }
    }}
}
}
