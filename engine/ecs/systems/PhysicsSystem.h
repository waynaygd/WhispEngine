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
        float defaultFriction = 0.85f)
        : m_EventBus(eventBus)
        , m_Gravity(gravity)
        , m_LinearDamping(linearDamping)
        , m_Substeps(substeps)
        , m_DefaultRestitution(defaultRestitution)
        , m_DefaultFriction(defaultFriction)
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
};
}
