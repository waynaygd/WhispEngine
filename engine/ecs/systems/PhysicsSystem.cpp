#include "PhysicsSystem.h"
#include "../World.h"
#include "../components/ColliderComponent.h"
#include "../components/RigidbodyComponent.h"
#include "../components/TransformComponent.h"
#include <cmath>
#include <vector>

namespace {
using ecs::Vec3;
static Vec3 Add(const Vec3&a,const Vec3&b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static Vec3 Sub(const Vec3&a,const Vec3&b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static Vec3 Scale(const Vec3&v,float s){return {v.x*s,v.y*s,v.z*s};}
static float Abs(float v){ return v >= 0.0f ? v : -v; }
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
    const float gravity = m_Gravity;
    const float linearDamping = m_LinearDamping;
    const int substeps = m_Substeps > 0 ? m_Substeps : 1;
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
            float ox = (a.collider->halfExtents.x + b.collider->halfExtents.x) - Abs(d.x);
            float oy = (a.collider->halfExtents.y + b.collider->halfExtents.y) - Abs(d.y);
            float oz = (a.collider->halfExtents.z + b.collider->halfExtents.z) - Abs(d.z);
            if (ox <= 0 || oy <= 0 || oz <= 0)
                continue;

            float minPen = ox; int axis = 0;
            if (oy < minPen) { minPen = oy; axis = 1; }
            if (oz < minPen) { minPen = oz; axis = 2; }

            Vec3 sep{};
            if (axis == 0) sep.x = d.x >= 0 ? minPen : -minPen;
            if (axis == 1) sep.y = d.y >= 0 ? minPen : -minPen;
            if (axis == 2) sep.z = d.z >= 0 ? minPen : -minPen;

            if (a.rigidbody->isStatic)
                b.transform->position = Sub(b.transform->position, sep);
            else if (b.rigidbody->isStatic)
                a.transform->position = Add(a.transform->position, sep);
            else
            {
                a.transform->position = Add(a.transform->position, Scale(sep, 0.5f));
                b.transform->position = Sub(b.transform->position, Scale(sep, 0.5f));
            }

            const float restitution = (a.collider->restitution + b.collider->restitution) > 0.0f
                ? (a.collider->restitution + b.collider->restitution) * 0.5f
                : m_DefaultRestitution;
            const float friction = (a.collider->friction + b.collider->friction) > 0.0f
                ? (a.collider->friction + b.collider->friction) * 0.5f
                : m_DefaultFriction;
            if (axis == 0)
            {
                const float va = a.rigidbody->velocity.x;
                const float vb = b.rigidbody->velocity.x;
                a.rigidbody->velocity.x = vb * restitution;
                b.rigidbody->velocity.x = va * restitution;
            }
            if (axis == 1)
            {
                a.rigidbody->velocity.y = 0.0f;
                b.rigidbody->velocity.y = 0.0f;
                a.rigidbody->velocity.x *= friction;
                a.rigidbody->velocity.z *= friction;
                b.rigidbody->velocity.x *= friction;
                b.rigidbody->velocity.z *= friction;
            }
            if (axis == 2)
            {
                const float va = a.rigidbody->velocity.z;
                const float vb = b.rigidbody->velocity.z;
                a.rigidbody->velocity.z = vb * restitution;
                b.rigidbody->velocity.z = va * restitution;
            }
            if (m_EventBus) m_EventBus->PublishCollision(CollisionEvent{ a.entity, b.entity });
        }
    }}
}
}
