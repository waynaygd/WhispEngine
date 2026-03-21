#pragma once

#include "../Component.h"

#include <string>

namespace ecs
{
struct TagComponent : Component
{
    std::string name;
};
}
