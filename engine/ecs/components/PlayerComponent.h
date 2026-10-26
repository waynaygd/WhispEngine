#pragma once

namespace ecs {

    struct PlayerComponent
    {
        // Local player identity, independent of input/camera permissions or body type.
        // eyeHeight remains the legacy controller's feet-relative eye setting.
        float eyeHeight = 1.7f;
    };

}
