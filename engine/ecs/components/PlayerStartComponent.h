
#pragma once
#include <string>

namespace ecs {

    struct PlayerStartComponent
    {
        bool enabled = true;
        int spawnMode = 0; // 0 compatibility (empty=legacy, assigned=override), 1 legacy, 2 override, 3 GameMode default.
        std::string controllerScript = "scripts/FirstPersonController.lua";
        float characterHeight = 1.7f;
        float eyeHeight = 1.7f; // From the feet; never above characterHeight.
        std::string playerPrefab; // Empty preserves the legacy character workflow.
    };

}
