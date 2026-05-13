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
static float Sign(float v){ return v >= 0.0f ? 1.0f : -1.0f; }
static float Clamp(float value, float minValue, float maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}
static float LengthSq(const Vec3& v){ return Dot(v, v); }
static float Length(const Vec3& v){ return std::sqrt(LengthSq(v)); }
static Vec3 NormalizeSafe(const Vec3& v)
{
    const float len = Length(v);
    if (len <= 0.000001f)
        return Vec3{ 0.0f, 1.0f, 0.0f };
    const float inv = 1.0f / len;
    return Scale(v, inv);
}
static float SphereRadius(const ecs::ColliderComponent& c)
{
    return std::max(c.halfExtents.x, std::max(c.halfExtents.y, c.halfExtents.z));
}
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
struct BoxAxes
{
    Vec3 xAxis;
    Vec3 yAxis;
    Vec3 zAxis;
};
static BoxAxes BuildBoxAxes(const Vec3& rot)
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
    return BoxAxes{
        Vec3{ r00, r10, r20 },
        Vec3{ r01, r11, r21 },
        Vec3{ r02, r12, r22 }
    };
}
struct BodyRef
{
    ecs::Entity entity{};
    ecs::TransformComponent* transform = nullptr;
    ecs::ColliderComponent* collider = nullptr;
    ecs::RigidbodyComponent* rigidbody = nullptr;
};

enum class ContactType
{
    None,
    BoxBox,
    SphereSphere,
    BoxSphere
};
}

namespace ecs {
void PhysicsSystem::Update(World& world, float dt)
{
    if (dt <= 0.0f)
        return;
    if (dt > 0.05f)
        dt = 0.05f;
    const float gravity = m_Gravity;
    const float baseStep = 1.0f / 240.0f;
    const int dynamicSubsteps = static_cast<int>(std::ceil(dt / baseStep));
    const int configuredSubsteps = m_Substeps > 0 ? m_Substeps : 1;
    const int substeps = std::max(configuredSubsteps, std::min(dynamicSubsteps, 64));
    const float stepDt = dt / static_cast<float>(substeps);
    const float dampingPerStep = std::pow(std::max(m_LinearDamping, 0.0f), stepDt * 60.0f);
    std::vector<BodyRef> bodies;
    bodies.reserve(128);
    world.ForEach<ColliderComponent, TransformComponent, RigidbodyComponent>([&](Entity e, ColliderComponent& c, TransformComponent& t, RigidbodyComponent& rb){
        bodies.push_back(BodyRef{ e, &t, &c, &rb });
    });

    for (int step = 0; step < substeps; ++step)
    {
    world.ForEach<TransformComponent, RigidbodyComponent>([&](Entity, TransformComponent& t, RigidbodyComponent& rb){
        if (rb.isStatic || !rb.simulatePhysics) return;
        if (rb.useGravity) rb.velocity.y -= gravity * stepDt;
        rb.velocity.x *= dampingPerStep;
        rb.velocity.z *= dampingPerStep;
        if (Abs(rb.velocity.x) < 0.0005f) rb.velocity.x = 0.0f;
        if (Abs(rb.velocity.z) < 0.0005f) rb.velocity.z = 0.0f;
        rb.velocity = Add(rb.velocity, Scale(rb.acceleration, stepDt));
        t.position = Add(t.position, Scale(rb.velocity, stepDt));
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
            float minPen = 0.0f;
            int axis = 1;
            Vec3 normal{ 0.0f, 1.0f, 0.0f };
            ContactType contactType = ContactType::None;

            if (a.collider->type == ColliderType::Box && b.collider->type == ColliderType::Box)
            {
                float ox = (aHalf.x + bHalf.x) - Abs(d.x);
                float oy = (aHalf.y + bHalf.y) - Abs(d.y);
                float oz = (aHalf.z + bHalf.z) - Abs(d.z);
                if (ox <= 0 || oy <= 0 || oz <= 0)
                    continue;
                minPen = ox; axis = 0;
                if (oy < minPen) { minPen = oy; axis = 1; }
                if (oz < minPen) { minPen = oz; axis = 2; }
                if (axis == 0) normal = Vec3{ (d.x >= 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f };
                if (axis == 1) normal = Vec3{ 0.0f, (d.y >= 0.0f) ? 1.0f : -1.0f, 0.0f };
                if (axis == 2) normal = Vec3{ 0.0f, 0.0f, (d.z >= 0.0f) ? 1.0f : -1.0f };
                contactType = ContactType::BoxBox;
            }
            else if (a.collider->type == ColliderType::Sphere && b.collider->type == ColliderType::Sphere)
            {
                const float ra = SphereRadius(*a.collider);
                const float rb = SphereRadius(*b.collider);
                const float distSq = LengthSq(d);
                const float radiusSum = ra + rb;
                if (distSq >= radiusSum * radiusSum)
                    continue;
                const float distance = std::sqrt(std::max(distSq, 0.0f));
                normal = (distance > 0.000001f) ? Scale(d, 1.0f / distance) : Vec3{ 1.0f, 0.0f, 0.0f };
                minPen = radiusSum - distance;
                contactType = ContactType::SphereSphere;
            }
            else
            {
                BodyRef* box = &a;
                BodyRef* sphere = &b;
                Vec3 boxCenter = ac;
                Vec3 sphereCenter = bc;
                Vec3 boxHalf = box->collider->halfExtents;
                if (a.collider->type == ColliderType::Sphere)
                {
                    box = &b;
                    sphere = &a;
                    boxCenter = bc;
                    sphereCenter = ac;
                    boxHalf = box->collider->halfExtents;
                }
                const float radius = SphereRadius(*sphere->collider);
                const BoxAxes axes = BuildBoxAxes(box->transform->rotation);
                const Vec3 boxToSphere = Sub(sphereCenter, boxCenter);
                const float localX = Dot(boxToSphere, axes.xAxis);
                const float localY = Dot(boxToSphere, axes.yAxis);
                const float localZ = Dot(boxToSphere, axes.zAxis);
                const float clampedX = Clamp(localX, -boxHalf.x, boxHalf.x);
                const float clampedY = Clamp(localY, -boxHalf.y, boxHalf.y);
                const float clampedZ = Clamp(localZ, -boxHalf.z, boxHalf.z);
                Vec3 closest = Add(
                    Add(
                        Add(boxCenter, Scale(axes.xAxis, clampedX)),
                        Scale(axes.yAxis, clampedY)),
                    Scale(axes.zAxis, clampedZ));
                Vec3 delta = Sub(sphereCenter, closest);
                const float distSq = LengthSq(delta);
                if (distSq > radius * radius)
                    continue;
                const float distance = std::sqrt(std::max(distSq, 0.0f));
                if (distance > 0.000001f)
                {
                    normal = NormalizeSafe(delta);
                    minPen = radius - distance;
                }
                else
                {
                    const float px = boxHalf.x - Abs(localX);
                    const float py = boxHalf.y - Abs(localY);
                    const float pz = boxHalf.z - Abs(localZ);
                    minPen = std::max(0.0f, radius + std::min(px, std::min(py, pz)));
                    if (px <= py && px <= pz) normal = Scale(axes.xAxis, (localX >= 0.0f) ? 1.0f : -1.0f);
                    else if (py <= pz) normal = Scale(axes.yAxis, (localY >= 0.0f) ? 1.0f : -1.0f);
                    else normal = Scale(axes.zAxis, (localZ >= 0.0f) ? 1.0f : -1.0f);
                }
                if (a.collider->type == ColliderType::Sphere)
                    normal = Scale(normal, -1.0f);
                contactType = ContactType::BoxSphere;
            }

            Vec3 sep = Scale(normal, minPen);
            if (minPen <= 0.0f)
                continue;

            const float invMassA = (a.rigidbody->isStatic || a.rigidbody->mass <= 0.0001f) ? 0.0f : (1.0f / a.rigidbody->mass);
            const float invMassB = (b.rigidbody->isStatic || b.rigidbody->mass <= 0.0001f) ? 0.0f : (1.0f / b.rigidbody->mass);
            const float invMassSum = invMassA + invMassB;
            if (invMassSum <= 0.0f)
                continue;

            const bool singleStaticContact = (invMassA == 0.0f) != (invMassB == 0.0f);
            const float slop = singleStaticContact ? 0.0f : 0.001f;
            const float percent = singleStaticContact ? 1.0f : 0.8f;
            const float correctionScale = std::max(minPen - slop, 0.0f) * percent / invMassSum;
            const Vec3 correction = Scale(sep, correctionScale / (minPen > 0.0f ? minPen : 1.0f));
            if (invMassA > 0.0f)
                a.transform->position = Add(a.transform->position, Scale(correction, invMassA));
            if (invMassB > 0.0f)
                b.transform->position = Sub(b.transform->position, Scale(correction, invMassB));

            // Keep dynamic spheres on the surface of static boxes to avoid deep embedding
            // on sloped ramps when frame time spikes.
            if (contactType == ContactType::BoxSphere && singleStaticContact)
            {
                BodyRef* dynamicBody = invMassA > 0.0f ? &a : &b;
                BodyRef* staticBody = invMassA > 0.0f ? &b : &a;
                if (dynamicBody->collider->type == ColliderType::Sphere && staticBody->collider->type == ColliderType::Box)
                {
                    const float radius = SphereRadius(*dynamicBody->collider);
                    const float skin = 0.001f;
                    const Vec3 staticCenter = Add(staticBody->transform->position, staticBody->collider->offset);
                    const BoxAxes staticAxes = BuildBoxAxes(staticBody->transform->rotation);
                    const Vec3 dynCenter = Add(dynamicBody->transform->position, dynamicBody->collider->offset);
                    const Vec3 toDyn = Sub(dynCenter, staticCenter);
                    const float lx = Dot(toDyn, staticAxes.xAxis);
                    const float ly = Dot(toDyn, staticAxes.yAxis);
                    const float lz = Dot(toDyn, staticAxes.zAxis);
                    const Vec3 half = staticBody->collider->halfExtents;
                    const float clx = Clamp(lx, -half.x, half.x);
                    const float cly = Clamp(ly, -half.y, half.y);
                    const float clz = Clamp(lz, -half.z, half.z);
                    Vec3 closestPoint = Add(Add(Add(staticCenter, Scale(staticAxes.xAxis, clx)), Scale(staticAxes.yAxis, cly)), Scale(staticAxes.zAxis, clz));
                    const Vec3 snappedCenter = Add(closestPoint, Scale(normal, radius + skin));
                    dynamicBody->transform->position = Sub(snappedCenter, dynamicBody->collider->offset);
                }
            }

            const float restitution = (a.collider->restitution + b.collider->restitution) > 0.0f
                ? (a.collider->restitution + b.collider->restitution) * 0.5f
                : m_DefaultRestitution;
            const float friction = (a.collider->friction + b.collider->friction) > 0.0f
                ? (a.collider->friction + b.collider->friction) * 0.5f
                : m_DefaultFriction;
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

            if (invMassA > 0.0f && invMassB == 0.0f)
            {
                const float vnA = Dot(a.rigidbody->velocity, normal);
                if (vnA < 0.0f)
                    a.rigidbody->velocity = Sub(a.rigidbody->velocity, Scale(normal, vnA));
            }
            else if (invMassB > 0.0f && invMassA == 0.0f)
            {
                const float vnB = Dot(b.rigidbody->velocity, normal);
                if (vnB > 0.0f)
                    b.rigidbody->velocity = Sub(b.rigidbody->velocity, Scale(normal, vnB));
            }

            // Simple center-of-mass support check for tower-like tipping:
            // when an object stands on another and its projected center leaves support footprint,
            // add lateral velocity so it starts falling off the edge.
            if (contactType == ContactType::BoxBox && axis == 1)
            {
                BodyRef* top = nullptr;
                BodyRef* bottom = nullptr;
                Vec3 topHalf{};
                Vec3 bottomHalf{};
                if (a.transform->position.y >= b.transform->position.y)
                {
                    top = &a; bottom = &b; topHalf = aHalf; bottomHalf = bHalf;
                }
                else
                {
                    top = &b; bottom = &a; topHalf = bHalf; bottomHalf = aHalf;
                }

                if (!top->rigidbody->isStatic && top->rigidbody->simulatePhysics)
                {
                    const Vec3 topCenter = Add(top->transform->position, top->collider->offset);
                    const Vec3 bottomCenter = Add(bottom->transform->position, bottom->collider->offset);
                    const float dx = topCenter.x - bottomCenter.x;
                    const float dz = topCenter.z - bottomCenter.z;
                    const float supportMarginX = std::max(bottomHalf.x - topHalf.x * 0.5f, 0.0f);
                    const float supportMarginZ = std::max(bottomHalf.z - topHalf.z * 0.5f, 0.0f);
                    const float overhangX = Abs(dx) - supportMarginX;
                    const float overhangZ = Abs(dz) - supportMarginZ;
                    const float tipStrength = 2.25f;
                    if (overhangX > 0.0f)
                        top->rigidbody->velocity.x += Sign(dx) * std::min(overhangX * tipStrength, 4.0f) * stepDt;
                    if (overhangZ > 0.0f)
                        top->rigidbody->velocity.z += Sign(dz) * std::min(overhangZ * tipStrength, 4.0f) * stepDt;
                }
            }
            if (m_EventBus) m_EventBus->PublishCollision(CollisionEvent{ a.entity, b.entity });
        }
    }}
}
}
