#pragma once
#include "ISystem.h"
#include "../events/EventBus.h"
namespace ecs {
class PhysicsSystem final : public ISystem {
public:
    explicit PhysicsSystem(
        EventBus* eventBus = nullptr,
        float gravity = 9.81f,
        float linearDamping = 0.985f,
        int substeps = 2,
        float defaultRestitution = 0.05f,
        float defaultFriction = 0.85f,
        int solverIterations = 4,
        float sphereMaxSpeed = 9.0f,
        float spherePenetrationEpsilon = 0.0005f,
        float sphereVelocityEpsilon = 0.05f,
        float dynamicBoxSphereCorrectionPercent = 1.0f)
        : m_EventBus(eventBus)
        , m_Gravity(gravity)
        , m_LinearDamping(linearDamping)
        , m_Substeps(substeps)
        , m_DefaultRestitution(defaultRestitution)
        , m_DefaultFriction(defaultFriction)
        , m_SolverIterations(solverIterations)
        , m_SphereMaxSpeed(sphereMaxSpeed)
        , m_SpherePenetrationEpsilon(spherePenetrationEpsilon)
        , m_SphereVelocityEpsilon(sphereVelocityEpsilon)
        , m_DynamicBoxSphereCorrectionPercent(dynamicBoxSphereCorrectionPercent)
    {}
    const char* Name() const override { return "PhysicsSystem"; }
    void Update(World& world, float dt) override;
private:
    EventBus* m_EventBus = nullptr;
    float m_Gravity = 9.81f;
    float m_LinearDamping = 0.985f;
    int m_Substeps = 2;
    float m_DefaultRestitution = 0.05f;
    float m_DefaultFriction = 0.85f;
    int m_SolverIterations = 4;
    float m_SphereMaxSpeed = 9.0f;
    float m_SpherePenetrationEpsilon = 0.0005f;
    float m_SphereVelocityEpsilon = 0.05f;
    float m_DynamicBoxSphereCorrectionPercent = 1.0f;
};
}
