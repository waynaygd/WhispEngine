#include "PhysicsSystem.h"
#include "../World.h"
#include "../components/ColliderComponent.h"
#include "../components/RigidbodyComponent.h"
#include "../components/TransformComponent.h"
#include <cmath>

namespace {
using ecs::Vec3;
static Vec3 Add(const Vec3&a,const Vec3&b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static Vec3 Sub(const Vec3&a,const Vec3&b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static Vec3 Scale(const Vec3&v,float s){return {v.x*s,v.y*s,v.z*s};}
}

namespace ecs {
void PhysicsSystem::Update(World& world, float dt)
{
    constexpr float gravity = 9.81f;
    world.ForEach<TransformComponent, RigidbodyComponent>([&](Entity, TransformComponent& t, RigidbodyComponent& rb){
        if (rb.isStatic) return;
        if (rb.useGravity) rb.velocity.y -= gravity * dt;
        rb.velocity = Add(rb.velocity, Scale(rb.acceleration, dt));
        t.position = Add(t.position, Scale(rb.velocity, dt));
    });

    world.ForEach<ColliderComponent, TransformComponent, RigidbodyComponent>([&](Entity ea, ColliderComponent& ca, TransformComponent& ta, RigidbodyComponent& ra){
        if (ca.type != ColliderType::Box) return;
        world.ForEach<ColliderComponent, TransformComponent, RigidbodyComponent>([&](Entity eb, ColliderComponent& cb, TransformComponent& tb, RigidbodyComponent& rb){
            if (ea.index >= eb.index || cb.type != ColliderType::Box) return;
            Vec3 ac = Add(ta.position, ca.offset); Vec3 bc = Add(tb.position, cb.offset);
            Vec3 d = Sub(ac, bc);
            float ox = (ca.halfExtents.x + cb.halfExtents.x) - std::fabs(d.x);
            float oy = (ca.halfExtents.y + cb.halfExtents.y) - std::fabs(d.y);
            float oz = (ca.halfExtents.z + cb.halfExtents.z) - std::fabs(d.z);
            if (ox <= 0 || oy <= 0 || oz <= 0) return;
            float minPen = ox; int axis = 0;
            if (oy < minPen) { minPen = oy; axis = 1; }
            if (oz < minPen) { minPen = oz; axis = 2; }
            Vec3 sep{};
            if (axis == 0) sep.x = d.x >= 0 ? minPen : -minPen;
            if (axis == 1) sep.y = d.y >= 0 ? minPen : -minPen;
            if (axis == 2) sep.z = d.z >= 0 ? minPen : -minPen;
            if (!ra.isStatic) ta.position = Add(ta.position, Scale(sep, 0.5f));
            if (!rb.isStatic) tb.position = Sub(tb.position, Scale(sep, 0.5f));
            if (axis == 0) { ra.velocity.x *= -0.2f; rb.velocity.x *= -0.2f; }
            if (axis == 1) { ra.velocity.y = 0.0f; rb.velocity.y = 0.0f; }
            if (axis == 2) { ra.velocity.z *= -0.2f; rb.velocity.z *= -0.2f; }
            if (m_EventBus) m_EventBus->PublishCollision(CollisionEvent{ea, eb});
        });
    });
}
}
