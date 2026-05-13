#pragma once
#include "ISystem.h"
#include "../events/EventBus.h"
namespace ecs {
class PhysicsSystem final : public ISystem {
public:
    explicit PhysicsSystem(EventBus* eventBus = nullptr) : m_EventBus(eventBus) {}
    const char* Name() const override { return "PhysicsSystem"; }
    void Update(World& world, float dt) override;
private:
    EventBus* m_EventBus = nullptr;
};
}
