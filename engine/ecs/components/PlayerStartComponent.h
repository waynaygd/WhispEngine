
#pragma once
#include <string>

namespace ecs {

    struct PlayerStartComponent
    {
        bool enabled = true;
        std::string controllerScript = "scripts/FirstPersonController.lua";
        float characterHeight = 1.7f;
        float eyeHeight = 1.7f; // From the feet; never above characterHeight.
    };

}
