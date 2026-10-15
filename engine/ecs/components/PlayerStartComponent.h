
#pragma once
#include <string>

namespace ecs {

    struct PlayerStartComponent
    {
        bool enabled = true;
        std::string controllerScript = "scripts/FirstPersonController.lua";
    };

}
