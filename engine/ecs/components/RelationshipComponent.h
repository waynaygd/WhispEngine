#pragma once
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
namespace ecs {
// Sole authoritative relationship. Child lists are derived, never serialized.
struct RelationshipComponent {
    std::optional<std::string> parent;
    std::uint64_t order=0;
};
inline constexpr auto MaximumSiblingOrder=std::uint64_t(std::numeric_limits<std::int64_t>::max());
inline bool ValidSceneIdentity(const std::string& id){return !id.empty()&&id.size()<=128&&id.find('\0')==std::string::npos;}
}
