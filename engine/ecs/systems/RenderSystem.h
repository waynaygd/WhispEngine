#pragma once

#include "ISystem.h"

class IRenderAdapter;

namespace ecs
{
class RenderSystem final : public ISystem
{
public:
    const char* Name() const override { return "RenderSystem"; }
    void Update(World& world, float dt) override;
    void Render(World& world, IRenderAdapter& renderer) const;
};
}
