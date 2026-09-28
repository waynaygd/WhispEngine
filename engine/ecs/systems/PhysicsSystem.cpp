#include "PhysicsSystem.h"
#include "../World.h"
#include "../components/ColliderComponent.h"
#include "../components/RigidbodyComponent.h"
#include "../components/TransformComponent.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <array>
#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#else
#define ZoneScopedN(name) ((void)0)
#endif

namespace {
using ecs::Vec3;
static Vec3 Add(const Vec3&a,const Vec3&b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static Vec3 Sub(const Vec3&a,const Vec3&b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static Vec3 Scale(const Vec3&v,float s){return {v.x*s,v.y*s,v.z*s};}
static float Dot(const Vec3&a,const Vec3&b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static Vec3 Cross(const Vec3&a,const Vec3&b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
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
static float BoundingRadius(const ecs::ColliderComponent& c)
{
    if (c.type == ecs::ColliderType::Sphere)
        return SphereRadius(c);
    return std::sqrt(c.halfExtents.x * c.halfExtents.x +
                     c.halfExtents.y * c.halfExtents.y +
                     c.halfExtents.z * c.halfExtents.z);
}
static int CellCoord(float v, float cellSize)
{
    return static_cast<int>(std::floor(v / cellSize));
}
static std::uint64_t CellKey(int x, int y, int z)
{
    const std::uint64_t ux = static_cast<std::uint32_t>(x) & 0x1fffffu;
    const std::uint64_t uy = static_cast<std::uint32_t>(y) & 0x1fffffu;
    const std::uint64_t uz = static_cast<std::uint32_t>(z) & 0x1fffffu;
    return (ux << 42) | (uy << 21) | uz;
}
static std::uint64_t PairKey(std::size_t a, std::size_t b)
{
    const std::uint64_t lo = static_cast<std::uint64_t>(std::min(a, b));
    const std::uint64_t hi = static_cast<std::uint64_t>(std::max(a, b));
    return (hi << 32) ^ lo;
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
static Vec3 InverseInertiaLocal(
    const ecs::RigidbodyComponent& rb,
    const ecs::ColliderComponent& collider)
{
    if (rb.isStatic || rb.mass <= 0.0001f)
        return Vec3{};

    if (collider.type == ecs::ColliderType::Sphere)
    {
        const float radius = SphereRadius(collider);
        const float inertia =
            (2.0f / 5.0f) * rb.mass * radius * radius;

        if (inertia <= 0.000001f)
            return Vec3{};

        const float inv = 1.0f / inertia;
        return Vec3{ inv, inv, inv };
    }

    const float width = collider.halfExtents.x * 2.0f;
    const float height = collider.halfExtents.y * 2.0f;
    const float depth = collider.halfExtents.z * 2.0f;

    const float ix =
        (rb.mass / 12.0f) *
        (height * height + depth * depth);

    const float iy =
        (rb.mass / 12.0f) *
        (width * width + depth * depth);

    const float iz =
        (rb.mass / 12.0f) *
        (width * width + height * height);

    return Vec3{
        ix > 0.000001f ? 1.0f / ix : 0.0f,
        iy > 0.000001f ? 1.0f / iy : 0.0f,
        iz > 0.000001f ? 1.0f / iz : 0.0f
    };
}

static Vec3 ApplyInverseInertiaWorld(
    const BodyRef& body,
    const Vec3& worldVector)
{
    if (body.rigidbody->isStatic ||
        body.rigidbody->mass <= 0.0001f)
    {
        return Vec3{};
    }

    const Vec3 invI =
        InverseInertiaLocal(
            *body.rigidbody,
            *body.collider);

    if (body.collider->type == ecs::ColliderType::Sphere)
    {
        return Vec3{
            worldVector.x * invI.x,
            worldVector.y * invI.y,
            worldVector.z * invI.z
        };
    }

    const BoxAxes axes =
        BuildBoxAxes(body.transform->rotation);

    const Vec3 local{
        Dot(worldVector, axes.xAxis),
        Dot(worldVector, axes.yAxis),
        Dot(worldVector, axes.zAxis)
    };

    const Vec3 localResult{
        local.x * invI.x,
        local.y * invI.y,
        local.z * invI.z
    };

    return Add(
        Add(
            Scale(axes.xAxis, localResult.x),
            Scale(axes.yAxis, localResult.y)),
        Scale(axes.zAxis, localResult.z));
}


// Contact normal always points from B to A. Collider dimensions/offset retain
// the engine's existing world-sized collider convention.
constexpr float contactMargin = 0.001f;
constexpr float penetrationSlop = 0.0005f;
static Vec3 Center(const BodyRef& b) { return Add(b.transform->position, b.collider->offset); }
static float InvMass(const BodyRef& b)
{
    const auto& rb = *b.rigidbody;
    return !rb.isStatic && rb.simulatePhysics && rb.mass > 0.0001f ? 1.0f / rb.mass : 0.0f;
}
static std::array<Vec3, 3> Axes(const BodyRef& b)
{
    const auto axes = BuildBoxAxes(b.transform->rotation);
    return { axes.xAxis, axes.yAxis, axes.zAxis };
}
static float Component(const Vec3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }
static Vec3 ToLocal(const BodyRef& b, const Vec3& r)
{
    const auto axes = Axes(b);
    return { Dot(r, axes[0]), Dot(r, axes[1]), Dot(r, axes[2]) };
}
static Vec3 ToWorld(const BodyRef& b, const Vec3& r)
{
    const auto axes = Axes(b);
    return Add(Add(Scale(axes[0], r.x), Scale(axes[1], r.y)), Scale(axes[2], r.z));
}
static Vec3 PointVelocity(const BodyRef& b, const Vec3& r)
{
    if (InvMass(b) == 0.0f) return {};
    return Add(b.rigidbody->velocity, Cross(b.rigidbody->angularVelocity, r));
}
static void ApplyImpulse(BodyRef& b, const Vec3& impulse, const Vec3& r)
{
    if (InvMass(b) == 0.0f) return;
    auto& rb = *b.rigidbody;
    rb.velocity = Add(rb.velocity, Scale(impulse, InvMass(b)));
    rb.angularVelocity = Add(rb.angularVelocity, ApplyInverseInertiaWorld(b, Cross(r, impulse)));
}

// World-space omega is not a vector of Euler angle derivatives. Apply the
// exponential rotation to the basis, then convert back to the editor's ZYX Euler storage.
static void Rotate(BodyRef& b, const Vec3& rotationVector)
{
    const float angle = Length(rotationVector);
    if (angle < 1.0e-9f) return;
    const Vec3 axis = Scale(rotationVector, 1.0f / angle);
    auto axes = Axes(b);
    for (auto& v : axes)
        v = Add(Add(Scale(v, std::cos(angle)), Scale(Cross(axis, v), std::sin(angle))),
            Scale(axis, Dot(axis, v) * (1.0f - std::cos(angle))));
    Vec3 euler;
    euler.y = std::asin(Clamp(-axes[0].z, -1.0f, 1.0f));
    if (Abs(std::cos(euler.y)) > 1.0e-5f)
    {
        euler.x = std::atan2(axes[1].z, axes[2].z);
        euler.z = std::atan2(axes[0].y, axes[0].x);
    }
    else
    {
        euler.x = std::atan2(-axes[2].y, axes[1].y);
        euler.z = 0.0f;
    }
    b.transform->rotation = euler;
}
struct ContactPoint
{
    Vec3 point{}, rA{}, rB{}, localA{}, localB{};
    float separation = 0.0f;
    float normalImpulse = 0.0f, tangent1 = 0.0f, tangent2 = 0.0f;
    float normalMass = 0.0f, tangentMass1 = 0.0f, tangentMass2 = 0.0f, tangentMassCross = 0.0f;
    float targetVelocity = 0.0f;
};
struct Manifold
{
    std::size_t a = 0, b = 0;
    Vec3 normal{}, t1{}, t2{};
    float friction = 0.0f;
    std::array<ContactPoint, 4> points{};
    int count = 0;
};
static float ProjectedRadius(const BodyRef& b, const Vec3& axis)
{
    const auto axes = Axes(b);
    return Abs(Dot(axes[0], axis)) * b.collider->halfExtents.x +
        Abs(Dot(axes[1], axis)) * b.collider->halfExtents.y +
        Abs(Dot(axes[2], axis)) * b.collider->halfExtents.z;
}
static std::vector<Vec3> Clip(const std::vector<Vec3>& polygon, const Vec3& normal, float offset)
{
    std::vector<Vec3> result;
    if (polygon.empty()) return result;
    Vec3 previous = polygon.back();
    float previousDistance = Dot(previous, normal) - offset;
    for (const Vec3& current : polygon)
    {
        const float distance = Dot(current, normal) - offset;
        if ((distance <= 0.0f) != (previousDistance <= 0.0f))
            result.push_back(Add(previous, Scale(Sub(current, previous),
                previousDistance / (previousDistance - distance))));
        if (distance <= 0.0f) result.push_back(current);
        previous = current;
        previousDistance = distance;
    }
    return result;
}
static void AddPoint(Manifold& m, const Vec3& point, float separation)
{
    if (m.count < 4)
    {
        auto& p = m.points[m.count++];
        p.point = point;
        p.separation = separation;
    }
}
static void ClosestSegments(Vec3 p, Vec3 q, Vec3 u, Vec3 v, Vec3& outA, Vec3& outB)
{
    const Vec3 d1 = Sub(q, p), d2 = Sub(v, u), r = Sub(p, u);
    const float a = Dot(d1, d1), e = Dot(d2, d2), b = Dot(d1, d2);
    const float c = Dot(d1, r), f = Dot(d2, r);
    const float denominator = a * e - b * b;
    float s = denominator > 1.0e-12f ? Clamp((b * f - c * e) / denominator, 0.0f, 1.0f) : 0.0f;
    float t = e > 1.0e-12f ? (b * s + f) / e : 0.0f;
    if (t < 0.0f) { t = 0.0f; s = a > 1.0e-12f ? Clamp(-c / a, 0.0f, 1.0f) : 0.0f; }
    if (t > 1.0f) { t = 1.0f; s = a > 1.0e-12f ? Clamp((b - c) / a, 0.0f, 1.0f) : 0.0f; }
    outA = Add(p, Scale(d1, s)); outB = Add(u, Scale(d2, t));
}
static bool BoxBox(const BodyRef& a, const BodyRef& b, Manifold& m)
{
    const auto aa = Axes(a), ab = Axes(b);
    const Vec3 ac = Center(a), bc = Center(b), delta = Sub(ac, bc);
    float bestOverlap = 1.0e30f;
    int feature = -1;
    auto testAxis = [&](Vec3 axis, int id)
    {
        if (LengthSq(axis) < 1.0e-8f) return true;
        axis = NormalizeSafe(axis);
        const float overlap = ProjectedRadius(a, axis) + ProjectedRadius(b, axis) - Abs(Dot(delta, axis));
        if (overlap < -contactMargin) return false;
        // Prefer a face when an almost parallel edge axis has essentially the
        // same depth; all axes still participate in the separation test.
        const float tolerance = feature < 0 ? 0.0f : (id >= 6 ? 0.0005f : 0.00005f);
        if (overlap < bestOverlap - tolerance)
        {
            bestOverlap = overlap; feature = id;
            m.normal = Scale(axis, Sign(Dot(delta, axis)));
        }
        return true;
    };
    for (int i = 0; i < 3; ++i) if (!testAxis(aa[i], i)) return false;
    for (int i = 0; i < 3; ++i) if (!testAxis(ab[i], i + 3)) return false;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (!testAxis(Cross(aa[i], ab[j]), 6 + i * 3 + j)) return false;
    if (feature >= 6)
    {
        const int ea = (feature - 6) / 3, eb = (feature - 6) % 3;
        Vec3 ca = ac, cb = bc;
        for (int i = 0; i < 3; ++i)
        {
            if (i != ea) ca = Add(ca, Scale(aa[i], -Sign(Dot(aa[i], m.normal)) * Component(a.collider->halfExtents, i)));
            if (i != eb) cb = Add(cb, Scale(ab[i], Sign(Dot(ab[i], m.normal)) * Component(b.collider->halfExtents, i)));
        }
        const Vec3 da = Scale(aa[ea], Component(a.collider->halfExtents, ea));
        const Vec3 db = Scale(ab[eb], Component(b.collider->halfExtents, eb));
        Vec3 pa, pb;
        ClosestSegments(Sub(ca, da), Add(ca, da), Sub(cb, db), Add(cb, db), pa, pb);
        AddPoint(m, Scale(Add(pa, pb), 0.5f), -bestOverlap);
        return true;
    }
    const bool referenceA = feature < 3;
    const auto& ref = referenceA ? a : b;
    const auto& inc = referenceA ? b : a;
    const auto ar = Axes(ref), ai = Axes(inc);
    const Vec3 outward = Scale(m.normal, referenceA ? -1.0f : 1.0f);
    const int face = feature % 3;
    const Vec3 faceCenter = Add(Center(ref), Scale(outward, Component(ref.collider->halfExtents, face)));
    int incident = 0;
    for (int i = 1; i < 3; ++i)
        if (Abs(Dot(ai[i], outward)) > Abs(Dot(ai[incident], outward))) incident = i;
    const Vec3 incidentCenter = Add(Center(inc), Scale(ai[incident],
        -Sign(Dot(ai[incident], outward)) * Component(inc.collider->halfExtents, incident)));
    const Vec3 u = Scale(ai[(incident + 1) % 3], Component(inc.collider->halfExtents, (incident + 1) % 3));
    const Vec3 v = Scale(ai[(incident + 2) % 3], Component(inc.collider->halfExtents, (incident + 2) % 3));
    std::vector<Vec3> polygon{ Add(Add(incidentCenter, u), v), Add(Sub(incidentCenter, u), v),
        Sub(Sub(incidentCenter, u), v), Sub(Add(incidentCenter, u), v) };
    for (int i = 0; i < 3; ++i)
    {
        if (i == face) continue;
        const float half = Component(ref.collider->halfExtents, i);
        polygon = Clip(polygon, ar[i], Dot(Center(ref), ar[i]) + half);
        polygon = Clip(polygon, Scale(ar[i], -1.0f), -Dot(Center(ref), ar[i]) + half);
    }
    std::vector<ContactPoint> candidates;
    for (const Vec3& p : polygon)
    {
        const float separation = Dot(Sub(p, faceCenter), outward);
        if (separation > contactMargin) continue;
        ContactPoint cp;
        cp.point = Sub(p, Scale(outward, 0.5f * separation));
        cp.separation = separation;
        bool duplicate = false;
        for (const auto& old : candidates) duplicate |= LengthSq(Sub(old.point, cp.point)) < 1.0e-10f;
        if (!duplicate) candidates.push_back(cp);
    }
    // Preserve the deepest point, then maximize coverage of the contact patch.
    // Clipping can produce up to eight vertices; taking the first four biases torque.
    while (!candidates.empty() && m.count < 4)
    {
        std::size_t best = 0;
        float bestScore = -1.0e30f;
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            float score = -candidates[i].separation;
            if (m.count > 0)
            {
                score = 1.0e30f;
                for (int j = 0; j < m.count; ++j)
                    score = std::min(score, LengthSq(Sub(candidates[i].point, m.points[j].point)));
                if (m.count == 2)
                    score = LengthSq(Cross(Sub(m.points[1].point, m.points[0].point),
                        Sub(candidates[i].point, m.points[0].point)));
            }
            if (score > bestScore) { bestScore = score; best = i; }
        }
        m.points[m.count++] = candidates[best];
        candidates.erase(candidates.begin() + best);
    }
    return m.count > 0;
}
static bool Collide(const BodyRef& a, const BodyRef& b, Manifold& m)
{
    if (a.collider->type == ecs::ColliderType::Box && b.collider->type == ecs::ColliderType::Box)
        return BoxBox(a, b, m);
    const Vec3 ac = Center(a), bc = Center(b);
    if (a.collider->type == ecs::ColliderType::Sphere && b.collider->type == ecs::ColliderType::Sphere)
    {
        const float ra = SphereRadius(*a.collider), rb = SphereRadius(*b.collider);
        const Vec3 d = Sub(ac, bc);
        const float distance = Length(d);
        if (distance > ra + rb + contactMargin) return false;
        m.normal = distance > 1.0e-6f ? Scale(d, 1.0f / distance) : Vec3{1, 0, 0};
        AddPoint(m, Scale(Add(Sub(ac, Scale(m.normal, ra)), Add(bc, Scale(m.normal, rb))), 0.5f), distance - ra - rb);
        return true;
    }
    const bool boxA = a.collider->type == ecs::ColliderType::Box;
    const auto& box = boxA ? a : b;
    const auto& sphere = boxA ? b : a;
    const Vec3 local = ToLocal(box, Sub(Center(sphere), Center(box)));
    const Vec3 half = box.collider->halfExtents;
    Vec3 closestLocal{ Clamp(local.x, -half.x, half.x), Clamp(local.y, -half.y, half.y), Clamp(local.z, -half.z, half.z) };
    Vec3 closest = Add(Center(box), ToWorld(box, closestLocal));
    Vec3 delta = Sub(Center(sphere), closest);
    float distance = Length(delta);
    const float radius = SphereRadius(*sphere.collider);
    if (distance > radius + contactMargin) return false;
    Vec3 outward;
    if (distance > 1.0e-6f) outward = Scale(delta, 1.0f / distance);
    else
    {
        const Vec3 depths{ half.x - Abs(local.x), half.y - Abs(local.y), half.z - Abs(local.z) };
        int face = 0;
        for (int i = 1; i < 3; ++i) if (Component(depths, i) < Component(depths, face)) face = i;
        outward = Scale(Axes(box)[face], Sign(Component(local, face)));
        distance = -Component(depths, face);
        closest = Sub(Center(sphere), Scale(outward, distance));
    }
    m.normal = Scale(outward, boxA ? -1.0f : 1.0f);
    AddPoint(m, Scale(Add(closest, Sub(Center(sphere), Scale(outward, radius))), 0.5f), distance - radius);
    return true;
}
static float EffectiveMass(const BodyRef& a, const BodyRef& b, const Vec3& ra, const Vec3& rb, const Vec3& axis)
{
    const float k = InvMass(a) + InvMass(b) + Dot(Cross(ra, axis), ApplyInverseInertiaWorld(a, Cross(ra, axis))) +
        Dot(Cross(rb, axis), ApplyInverseInertiaWorld(b, Cross(rb, axis)));
    return k > 1.0e-9f ? 1.0f / k : 0.0f;
}
static void PairImpulse(BodyRef& a, BodyRef& b, const ContactPoint& p, const Vec3& impulse)
{
    ApplyImpulse(a, impulse, p.rA);
    ApplyImpulse(b, Scale(impulse, -1.0f), p.rB);
}
static void Wake(BodyRef& b)
{
    if (InvMass(b) == 0.0f) return;
    b.rigidbody->sleeping = false;
    b.rigidbody->sleepTimer = 0.0f;
}
static Vec3 AabbHalf(const BodyRef& b)
{
    if (b.collider->type == ecs::ColliderType::Sphere)
    {
        const float r = SphereRadius(*b.collider);
        return {r, r, r};
    }
    return RotatedAabbHalfExtents(b.collider->halfExtents, b.transform->rotation);
}
static std::vector<std::pair<std::size_t, std::size_t>> Broadphase(const std::vector<BodyRef>& bodies)
{
    // Insert every occupied AABB cell, not just the center. Large colliders use
    // an overflow list, avoiding both missed pairs and huge ground-plane grids.
    constexpr float cellSize = 0.6f;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> grid;
    std::vector<std::size_t> large;
    std::unordered_set<std::uint64_t> seen;
    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    auto add = [&](std::size_t i, std::size_t j)
    {
        if (i == j || (InvMass(bodies[i]) == 0.0f && InvMass(bodies[j]) == 0.0f)) return;
        const Vec3 d = Sub(Center(bodies[i]), Center(bodies[j]));
        const Vec3 h = Add(AabbHalf(bodies[i]), AabbHalf(bodies[j]));
        if (Abs(d.x) > h.x + contactMargin || Abs(d.y) > h.y + contactMargin || Abs(d.z) > h.z + contactMargin) return;
        if (seen.insert(PairKey(i, j)).second) pairs.emplace_back(std::min(i,j), std::max(i,j));
    };
    for (std::size_t i = 0; i < bodies.size(); ++i)
    {
        const Vec3 h = Add(AabbHalf(bodies[i]), Vec3{contactMargin, contactMargin, contactMargin});
        const Vec3 lo = Sub(Center(bodies[i]), h), hi = Add(Center(bodies[i]), h);
        const int x0 = CellCoord(lo.x, cellSize), x1 = CellCoord(hi.x, cellSize);
        const int y0 = CellCoord(lo.y, cellSize), y1 = CellCoord(hi.y, cellSize);
        const int z0 = CellCoord(lo.z, cellSize), z1 = CellCoord(hi.z, cellSize);
        const double cells = double(x1 - x0 + 1) * double(y1 - y0 + 1) * double(z1 - z0 + 1);
        if (cells > 256) { large.push_back(i); continue; }
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z)
                {
                    auto& entries = grid[CellKey(x,y,z)];
                    for (auto j : entries) add(i,j);
                    entries.push_back(i);
                }
    }
    for (auto i : large) for (std::size_t j = 0; j < bodies.size(); ++j) add(i,j);
    std::sort(pairs.begin(), pairs.end());
    return pairs;
}
} // namespace

namespace ecs {
void PhysicsSystem::Update(World& world, float dt)
{
    ZoneScopedN("PhysicsSystem");
    if (!m_Enabled || dt <= 0.0f || !std::isfinite(dt)) return;
    dt = std::min(dt, 0.05f);
    std::vector<BodyRef> bodies;
    world.ForEach<ColliderComponent, TransformComponent, RigidbodyComponent>(
        [&](Entity e, ColliderComponent& c, TransformComponent& t, RigidbodyComponent& rb)
        {
            if (rb.simulatePhysics || rb.isStatic) bodies.push_back({e, &t, &c, &rb});
        });
    std::sort(bodies.begin(), bodies.end(), [](const BodyRef& a, const BodyRef& b) { return a.entity.index < b.entity.index; });
    // Preserve the 240 Hz maximum step and additionally limit travel by body
    // thickness. This is adaptive substepping, not a velocity clamp or full CCD.
    float maxStep = 1.0f / 240.0f;
    for (const auto& b : bodies)
    {
        if (InvMass(b) == 0.0f) continue;
        const auto h = b.collider->halfExtents;
        const float extent = std::max(0.001f, std::min({h.x, h.y, h.z}));
        const float speed = Length(b.rigidbody->velocity) + Length(b.rigidbody->angularVelocity) * BoundingRadius(*b.collider) + Abs(m_Gravity) * dt;
        if (speed > 0.0f) maxStep = std::min(maxStep, extent * 0.5f / speed);
    }
    const int substeps = std::max(std::max(m_Substeps, 1), std::min(256, static_cast<int>(std::ceil(dt / maxStep))));
    const float h = dt / substeps;
    const int iterations = std::max(m_SolverIterations, 1);
    std::vector<CollisionEvent> events;
    for (int step = 0; step < substeps; ++step)
    {
        ZoneScopedN("PhysicsStep");
        std::vector<Manifold> contacts;
        const auto pairs = Broadphase(bodies);
        for (auto [i,j] : pairs)
        {
            Manifold m; m.a = i; m.b = j;
            if (Collide(bodies[i], bodies[j], m)) contacts.push_back(m);
        }
        // Dynamic contact islands are also used for waking/sleeping. Static
        // bodies anchor an island but must never merge unrelated resting bodies.
        std::vector<std::size_t> parent(bodies.size());
        std::vector<bool> supported(bodies.size(), false);
        for (std::size_t i = 0; i < bodies.size(); ++i) { parent[i] = i; supported[i] = InvMass(bodies[i]) == 0.0f; }
        auto root = [&](std::size_t i) { while (parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; } return i; };
        for (const auto& m : contacts)
            if (InvMass(bodies[m.a]) > 0 && InvMass(bodies[m.b]) > 0) parent[root(m.b)] = root(m.a);
        // Directed support propagation: a side wall or a free-falling cluster
        // is not support against gravity.
        for (std::size_t pass = 0; pass < bodies.size(); ++pass)
        {
            bool changed = false;
            for (const auto& m : contacts)
            {
                const float up = m.normal.y * (m_Gravity >= 0 ? 1.0f : -1.0f);
                if (up > 0.25f && supported[m.b] && !supported[m.a]) { supported[m.a] = true; changed = true; }
                if (up < -0.25f && supported[m.a] && !supported[m.b]) { supported[m.b] = true; changed = true; }
            }
            if (!changed) break;
        }
        std::vector<bool> active(bodies.size(), false);
        for (std::size_t i = 0; i < bodies.size(); ++i)
        {
            auto& b = bodies[i]; auto& rb = *b.rigidbody;
            if (InvMass(b) == 0.0f) continue;
            if (!supported[i] || (rb.sleeping && (LengthSq(rb.velocity) > 1.0e-10f || LengthSq(rb.angularVelocity) > 1.0e-10f ||
                LengthSq(rb.acceleration) > 0.0f || LengthSq(rb.torque) > 0.0f))) Wake(b);
            active[root(i)] = active[root(i)] || !rb.sleeping;
        }
        for (std::size_t i = 0; i < bodies.size(); ++i)
            if (InvMass(bodies[i]) > 0 && active[root(i)] && bodies[i].rigidbody->sleeping) Wake(bodies[i]);
        for (auto& b : bodies)
        {
            auto& rb = *b.rigidbody;
            if (InvMass(b) == 0.0f || rb.sleeping) continue;
            Vec3 acceleration = rb.acceleration;
            if (rb.useGravity) acceleration.y -= m_Gravity;
            rb.velocity = Add(rb.velocity, Scale(acceleration, h));
            const float damping = std::pow(std::max(m_LinearDamping, 0.0f), h * 60.0f * std::max(rb.linearDampingMultiplier, 0.0f));
            rb.velocity.x *= damping; rb.velocity.z *= damping;
            rb.angularVelocity = Add(rb.angularVelocity, Scale(ApplyInverseInertiaWorld(b, rb.torque), h));
            rb.angularVelocity = Scale(rb.angularVelocity, std::pow(0.995f, h * 60.0f * std::max(rb.angularDampingMultiplier, 0.0f)));
        }
        // Prepare every restitution target BEFORE any warm-start impulses.
        for (auto& m : contacts)
        {
            auto& a = bodies[m.a]; auto& b = bodies[m.b];
            auto material = [](float value, float fallback) { return value >= 0.0f ? value : std::max(fallback,0.0f); };
            const float restitution = Clamp((material(a.collider->restitution,m_DefaultRestitution) +
                material(b.collider->restitution,m_DefaultRestitution)) * 0.5f, 0.0f, 1.0f);
            m.friction = std::sqrt(material(a.collider->friction,m_DefaultFriction) * material(b.collider->friction,m_DefaultFriction));
            m.t1 = NormalizeSafe(Cross(m.normal, Abs(m.normal.x) < 0.577f ? Vec3{1,0,0} : Vec3{0,1,0}));
            m.t2 = Cross(m.normal, m.t1);
            const auto cached = m_ContactCache.find(PairKey(a.entity.index, b.entity.index));
            const CachedManifold* old = cached == m_ContactCache.end() ? nullptr : &cached->second;
            auto same = [](Vec3 x, Vec3 y) { return LengthSq(Sub(x,y)) < 1.0e-10f; };
            if (old && (old->a != a.entity || old->b != b.entity || Dot(old->normal, m.normal) < 0.95f ||
                !same(old->centerA, Center(a)) || !same(old->centerB, Center(b)) ||
                !same(old->rotationA, a.transform->rotation) || !same(old->rotationB, b.transform->rotation) ||
                !same(old->halfA, a.collider->halfExtents) || !same(old->halfB, b.collider->halfExtents) ||
                old->massA != a.rigidbody->mass || old->massB != b.rigidbody->mass ||
                old->staticA != a.rigidbody->isStatic || old->staticB != b.rigidbody->isStatic ||
                old->typeA != int(a.collider->type) || old->typeB != int(b.collider->type))) old = nullptr;
            std::array<bool,4> used{};
            for (int k = 0; k < m.count; ++k)
            {
                auto& p = m.points[k];
                p.rA = Sub(p.point, Center(a)); p.rB = Sub(p.point, Center(b));
                p.localA = ToLocal(a, p.rA); p.localB = ToLocal(b, p.rB);
                p.normalMass = EffectiveMass(a,b,p.rA,p.rB,m.normal);
                p.tangentMass1 = EffectiveMass(a,b,p.rA,p.rB,m.t1);
                p.tangentMass2 = EffectiveMass(a,b,p.rA,p.rB,m.t2);
                const float k12 = Dot(Cross(p.rA,m.t1),ApplyInverseInertiaWorld(a,Cross(p.rA,m.t2))) +
                    Dot(Cross(p.rB,m.t1),ApplyInverseInertiaWorld(b,Cross(p.rB,m.t2)));
                const float k11 = 1.0f / p.tangentMass1, k22 = 1.0f / p.tangentMass2;
                const float determinant = k11 * k22 - k12 * k12;
                if (determinant > 1.0e-9f)
                {
                    p.tangentMass1 = k22 / determinant;
                    p.tangentMass2 = k11 / determinant;
                    p.tangentMassCross = -k12 / determinant;
                }
                const float vn = Dot(Sub(PointVelocity(a,p.rA), PointVelocity(b,p.rB)), m.normal);
                p.targetVelocity = p.separation > 0.0f ? -p.separation / h : 0.0f;
                if (vn < -0.5f) p.targetVelocity = std::max(p.targetVelocity, -restitution * vn);
                if (!old || old->dt <= 0.0f) continue;
                int match = -1;
                const float radius = std::min(BoundingRadius(*a.collider), BoundingRadius(*b.collider));
                float best = std::pow(std::min(0.02f, radius * 0.2f), 2.0f);
                for (int q = 0; q < old->count; ++q)
                {
                    const float distance = LengthSq(Sub(p.localA,old->points[q].localA)) + LengthSq(Sub(p.localB,old->points[q].localB));
                    if (!used[q] && distance < best) { best = distance; match = q; }
                }
                if (match < 0) continue;
                used[match] = true;
                const float ratio = h / old->dt;
                p.normalImpulse = old->points[match].normalImpulse * ratio;
                const Vec3 tangent = Scale(old->points[match].tangentImpulse, ratio);
                p.tangent1 = Dot(tangent,m.t1); p.tangent2 = Dot(tangent,m.t2);
                const float length = std::hypot(p.tangent1,p.tangent2), limit = m.friction * p.normalImpulse;
                if (length > limit && length > 0) { p.tangent1 *= limit / length; p.tangent2 *= limit / length; }
            }
        }
        auto awake = [&](const Manifold& m) { return active[root(m.a)] || active[root(m.b)]; };
        for (auto& m : contacts) if (awake(m))
            for (int k = 0; k < m.count; ++k)
            {
                const auto& p = m.points[k];
                PairImpulse(bodies[m.a], bodies[m.b], p, Add(Scale(m.normal,p.normalImpulse), Add(Scale(m.t1,p.tangent1),Scale(m.t2,p.tangent2))));
            }
        for (int iteration = 0; iteration < iterations; ++iteration)
            for (auto& m : contacts) if (awake(m))
            {
                auto& a = bodies[m.a]; auto& b = bodies[m.b];
                for (int k = 0; k < m.count; ++k)
                {
                    auto& p = m.points[k];
                    const float vn = Dot(Sub(PointVelocity(a,p.rA),PointVelocity(b,p.rB)),m.normal);
                    const float previous = p.normalImpulse;
                    p.normalImpulse = std::max(0.0f, previous + (p.targetVelocity - vn) * p.normalMass);
                    PairImpulse(a,b,p,Scale(m.normal,p.normalImpulse - previous));
                    // Two fixed tangents, with a Coulomb disk on the TOTAL friction impulse.
                    const float old1 = p.tangent1, old2 = p.tangent2;
                    const Vec3 velocity = Sub(PointVelocity(a,p.rA),PointVelocity(b,p.rB));
                    const float vt1 = Dot(velocity,m.t1), vt2 = Dot(velocity,m.t2);
                    p.tangent1 -= vt1 * p.tangentMass1 + vt2 * p.tangentMassCross;
                    p.tangent2 -= vt2 * p.tangentMass2 + vt1 * p.tangentMassCross;
                    const float length = std::hypot(p.tangent1,p.tangent2), limit = m.friction * p.normalImpulse;
                    if (length > limit && length > 0) { p.tangent1 *= limit / length; p.tangent2 *= limit / length; }
                    PairImpulse(a,b,p,Add(Scale(m.t1,p.tangent1-old1),Scale(m.t2,p.tangent2-old2)));
                }
            }
        for (auto& b : bodies)
        {
            if (InvMass(b) == 0.0f || b.rigidbody->sleeping) continue;
            b.transform->position = Add(b.transform->position, Scale(b.rigidbody->velocity,h));
            Rotate(b,Scale(b.rigidbody->angularVelocity,h));
        }
        // Nonlinear position projection uses angular effective mass and fresh
        // contact geometry. It changes poses only, never physical velocities.
        const auto positionPairs = Broadphase(bodies);
        for (int iteration = 0; iteration < 4; ++iteration)
            for (auto [i,j] : positionPairs)
            {
                auto& a = bodies[i]; auto& b = bodies[j];
                if ((InvMass(a) == 0 || a.rigidbody->sleeping) && (InvMass(b) == 0 || b.rigidbody->sleeping)) continue;
                Manifold m;
                if (!Collide(a,b,m)) continue;
                for (int k = 0; k < m.count; ++k)
                {
                    const auto& p = m.points[k];
                    const Vec3 ra = Sub(p.point,Center(a)), rb = Sub(p.point,Center(b));
                    const float correction = std::max(-p.separation - penetrationSlop,0.0f) * 0.2f;
                    const Vec3 impulse = Scale(m.normal,correction * EffectiveMass(a,b,ra,rb,m.normal));
                    if (InvMass(a) > 0 && !a.rigidbody->sleeping)
                    {
                        a.transform->position = Add(a.transform->position,Scale(impulse,InvMass(a)));
                        Rotate(a,ApplyInverseInertiaWorld(a,Cross(ra,impulse)));
                    }
                    if (InvMass(b) > 0 && !b.rigidbody->sleeping)
                    {
                        b.transform->position = Sub(b.transform->position,Scale(impulse,InvMass(b)));
                        Rotate(b,Scale(ApplyInverseInertiaWorld(b,Cross(rb,impulse)),-1.0f));
                    }
                }
            }
        // All members must remain quiet and supported for the entire interval.
        std::vector<bool> quiet(bodies.size(), true);
        std::vector<float> timer(bodies.size(), 1.0e30f);
        for (std::size_t i = 0; i < bodies.size(); ++i)
        {
            auto& rb = *bodies[i].rigidbody;
            if (InvMass(bodies[i]) == 0) continue;
            const bool stable = supported[i] && LengthSq(rb.velocity) < 0.01f * 0.01f &&
                LengthSq(rb.angularVelocity) < 0.025f * 0.025f && LengthSq(rb.acceleration) == 0 && LengthSq(rb.torque) == 0;
            quiet[root(i)] = quiet[root(i)] && stable;
            timer[root(i)] = std::min(timer[root(i)],rb.sleepTimer);
        }
        for (const auto& m : contacts)
            for (int k = 0; k < m.count; ++k)
                if (m.points[k].separation < -0.002f || m.points[k].separation > penetrationSlop)
                { quiet[root(m.a)] = false; quiet[root(m.b)] = false; }
        for (std::size_t i = 0; i < bodies.size(); ++i)
        {
            auto& rb = *bodies[i].rigidbody;
            if (InvMass(bodies[i]) == 0) continue;
            rb.sleepTimer = quiet[root(i)] ? timer[root(i)] + h : 0.0f;
            rb.sleeping = rb.sleepTimer >= 0.75f;
            if (rb.sleeping) { rb.velocity = {}; rb.angularVelocity = {}; }
        }
        std::unordered_map<std::uint64_t,CachedManifold> nextCache;
        for (const auto& m : contacts)
        {
            const auto& a = bodies[m.a]; const auto& b = bodies[m.b];
            CachedManifold c;
            c.a = a.entity; c.b = b.entity; c.normal = m.normal; c.dt = h; c.count = m.count;
            c.centerA = Center(a); c.centerB = Center(b);
            c.rotationA = a.transform->rotation; c.rotationB = b.transform->rotation;
            c.halfA = a.collider->halfExtents; c.halfB = b.collider->halfExtents;
            c.massA = a.rigidbody->mass; c.massB = b.rigidbody->mass;
            c.staticA = a.rigidbody->isStatic; c.staticB = b.rigidbody->isStatic;
            c.typeA = int(a.collider->type); c.typeB = int(b.collider->type);
            for (int k = 0; k < m.count; ++k)
            {
                const auto& p = m.points[k];
                c.points[k] = {p.localA,p.localB,Add(Scale(m.t1,p.tangent1),Scale(m.t2,p.tangent2)),p.normalImpulse};
            }
            nextCache.emplace(PairKey(a.entity.index,b.entity.index),c);
        }
        m_ContactCache = std::move(nextCache);
        // Defer callbacks until all substeps finish: listeners can mutate World.
        if (m_EventBus) for (const auto& m : contacts)
            events.push_back({bodies[m.a].entity,bodies[m.b].entity});
    }
    if (m_EventBus) for (const auto& event : events) m_EventBus->PublishCollision(event);
}
} // namespace ecs
