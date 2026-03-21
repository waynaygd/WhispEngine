#pragma once

#include "ISystem.h"

namespace ecs
{
class BoundsBounceSystem final : public ISystem
{
public:
    const char* Name() const override { return "BoundsBounceSystem"; }
    void Update(World& world, float dt) override;
};
}
