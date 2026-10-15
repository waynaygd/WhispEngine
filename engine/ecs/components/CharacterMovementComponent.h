#pragma once

namespace ecs {

    struct CharacterMovementComponent
    {
        // Настройки
        float moveSpeed = 4.0f;
        float jumpSpeed = 4.5f;

        // Состояние во время игры
        bool grounded = false;
        bool jumpWasDown = false;
    };

}