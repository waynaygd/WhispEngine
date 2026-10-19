#pragma once
#include <string>
#include <nlohmann/json.hpp>
namespace ecs {
// Root-only link; typed references also participate in the normal duplicate remapper.
struct PrefabInstanceComponent {
    std::string asset;
    nlohmann::json members=nlohmann::json::object();
};
}
