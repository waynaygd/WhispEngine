#pragma once

#include "ISystem.h"

#include <memory>
#include <utility>
#include <vector>

namespace ecs
{
class SystemPipeline
{
public:
    template <typename TSystem, typename... Args>
    TSystem& AddSystem(Args&&... args)
    {
        auto system = std::make_unique<TSystem>(std::forward<Args>(args)...);
        TSystem& ref = *system;
        m_Systems.push_back(std::move(system));
        return ref;
    }

    void Update(World& world, float dt);
    void Clear();

private:
    std::vector<std::unique_ptr<ISystem>> m_Systems;
};
}
