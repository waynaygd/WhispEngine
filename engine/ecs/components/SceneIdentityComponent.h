#pragma once
#include <atomic>
#include <chrono>
#include <string>
namespace ecs {
inline std::string NewSceneIdentity(){
    static const auto session=std::chrono::high_resolution_clock::now().time_since_epoch().count();
    static std::atomic<unsigned long long> counter{1};
    return "entity-"+std::to_string(session)+"-"+std::to_string(counter.fetch_add(1));
}
struct SceneIdentityComponent {std::string value=NewSceneIdentity();};
}
