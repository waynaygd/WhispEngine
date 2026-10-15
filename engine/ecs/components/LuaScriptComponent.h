#pragma once
#include <string>
#include <nlohmann/json.hpp>
namespace ecs {
struct LuaScriptComponent {
    std::string scriptPath;
    bool enabled=true;
    nlohmann::json properties=nlohmann::json::object();
};
}
