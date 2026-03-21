#pragma once

#include <cstdint>
#include <functional>

namespace ecs
{
struct Entity
{
    static constexpr std::uint32_t InvalidIndex = UINT32_MAX;

    std::uint32_t index = InvalidIndex;
    std::uint32_t generation = 0;

    [[nodiscard]] bool IsValid() const
    {
        return index != InvalidIndex;
    }

    friend bool operator==(const Entity& lhs, const Entity& rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    friend bool operator!=(const Entity& lhs, const Entity& rhs)
    {
        return !(lhs == rhs);
    }
};

struct EntityHash
{
    std::size_t operator()(const Entity& entity) const noexcept
    {
        const std::size_t indexHash = std::hash<std::uint32_t>{}(entity.index);
        const std::size_t generationHash = std::hash<std::uint32_t>{}(entity.generation);
        return indexHash ^ (generationHash << 1);
    }
};
}
