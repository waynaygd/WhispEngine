#pragma once

namespace ecs
{
class World;

class ISystem
{
public:
    virtual ~ISystem() = default;

    virtual const char* Name() const = 0;
    virtual void Update(World& world, float dt) = 0;
};
}
