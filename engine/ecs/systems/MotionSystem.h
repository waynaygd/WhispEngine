#pragma once

#include "ISystem.h"

namespace ecs
{
class MotionSystem final : public ISystem
{
public:
    const char* Name() const override { return "MotionSystem"; }
    void Update(World& world, float dt) override;
};
}
