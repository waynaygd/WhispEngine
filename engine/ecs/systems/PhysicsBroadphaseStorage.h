#pragma once
#include "../Entity.h"
#include "../MathTypes.h"
#include <vector>
#include <cstdint>
#include <utility>

namespace ecs {
// Numeric snapshots only: never retain component/BodyRef pointers across ticks.
// Flat storage holds the current spatial hash, not historical world cells.
struct PhysicsBroadphaseStorage {
    struct Geometry {
        Entity entity;
        Vec3 center{}, rotation{}, dimensions{}, aabb{};
        int shape = 0;
        bool immovable = false;
    };
    struct Entry { std::uint64_t cell; std::size_t body; };
    std::vector<Geometry> geometry;
    std::vector<Entry> entries;
    std::vector<std::size_t> large;
    std::vector<std::uint64_t> seen;
    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    std::size_t seenCount = 0;
    bool valid = false;
};
}
