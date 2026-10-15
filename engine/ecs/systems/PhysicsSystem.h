#pragma once
#include "ISystem.h"
#include "../events/EventBus.h"
#include "../MathTypes.h"
#include <array>
#include <unordered_map>
#include "PhysicsBroadphaseStorage.h"

class JobSystem;

namespace ecs {
class PhysicsSystem final : public ISystem {
public:
    struct Statistics {
        std::size_t bodies = 0, sleeping = 0, pairs = 0, contacts = 0, contactPoints = 0;
        std::size_t activeBodies = 0;
        int substeps = 0, solverIterations = 0;
        double totalMs = 0, integrateMs = 0, broadphaseMs = 0, narrowphaseMs = 0;
        double solverMs = 0, poseMs = 0, projectionMs = 0;
        double dispatchMs = 0, waitMs = 0;
        std::size_t broadphaseRebuilds = 0, broadphaseReuses = 0;
        std::size_t broadphaseBufferGrowths = 0, aabbComputations = 0;
    };
    const Statistics& GetStatistics() const { return m_Statistics; }
    void ResetState() { m_ContactCache.clear(); m_Broadphase = {}; m_Statistics = {}; }
    void SetParallel(bool enabled) { m_Parallel = enabled; }
    bool IsParallel() const { return m_Parallel; }
    // Opt-in quadratic test oracle; keep disabled for performance measurements.
    void SetBroadphaseValidation(bool enabled) { m_ValidateBroadphase = enabled; }
    explicit PhysicsSystem(
        EventBus* eventBus = nullptr,
        float gravity = 9.81f,
        float linearDamping = 0.985f,
        int substeps = 2,
        float defaultRestitution = 0.05f,
        float defaultFriction = 0.85f,
        int solverIterations = 12,
        // Legacy scene-config arguments retained for source compatibility.
        // All shapes now use the same solver, without sphere speed clamps.
        float /*sphereMaxSpeed*/ = 9.0f,
        float /*spherePenetrationEpsilon*/ = 0.0005f,
        float /*sphereVelocityEpsilon*/ = 0.05f,
        float /*dynamicBoxSphereCorrectionPercent*/ = 1.0f)
        : m_EventBus(eventBus)
        , m_Gravity(gravity)
        , m_LinearDamping(linearDamping)
        , m_Substeps(substeps)
        , m_DefaultRestitution(defaultRestitution)
        , m_DefaultFriction(defaultFriction)
        , m_SolverIterations(solverIterations)
    {}
    const char* Name() const override { return "PhysicsSystem"; }
    bool IsFixedUpdate() const override { return true; }
    void Update(World& world, float dt) override;
    void SetEnabled(bool enabled) {
        if (m_Enabled != enabled) m_ContactCache.clear();
        m_Enabled = enabled;
    }

    void SetJobSystem(::JobSystem* jobSystem)
    {
        m_JobSystem = jobSystem;
    }
    bool IsEnabled() const { return m_Enabled; }
private:
    Statistics m_Statistics;
    PhysicsBroadphaseStorage m_Broadphase;
    bool m_ValidateBroadphase = false;
    bool m_Parallel = true;
    struct CachedPoint {
        Vec3 localA{}, localB{}, tangentImpulse{};
        float normalImpulse = 0.0f;
    };
    struct CachedManifold {
        Entity a{}, b{};
        Vec3 normal{}, centerA{}, centerB{}, rotationA{}, rotationB{};
        Vec3 halfA{}, halfB{};
        float massA = 0.0f, massB = 0.0f;
        bool staticA = false, staticB = false;
        int typeA = 0, typeB = 0;
        float dt = 0.0f;
        std::array<CachedPoint, 4> points{};
        int count = 0;
    };
    std::unordered_map<std::uint64_t, CachedManifold> m_ContactCache;
    ::JobSystem* m_JobSystem = nullptr;
    EventBus* m_EventBus = nullptr;
    bool m_Enabled = true;
    float m_Gravity = 9.81f;
    float m_LinearDamping = 0.985f;
    int m_Substeps = 2;
    float m_DefaultRestitution = 0.05f;
    float m_DefaultFriction = 0.85f;
    int m_SolverIterations = 12;
};
}
