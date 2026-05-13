#include "InputManager.h"

bool InputManager::IsActionActive(const std::string& action) const
{
    const auto it = m_ActionMap.find(action);
    return it != m_ActionMap.end() && IsKeyPressed(it->second);
}

bool InputManager::IsKeyPressed(int glfwKey) const
{
    return m_Window != nullptr && glfwGetKey(m_Window, glfwKey) == GLFW_PRESS;
}

ecs::Vec3 InputManager::GetMovementAxis() const
{
    ecs::Vec3 move{};
    if (IsActionActive("MoveForward")) move.z += 1.0f;
    if (IsActionActive("MoveBackward")) move.z -= 1.0f;
    if (IsActionActive("MoveRight")) move.x += 1.0f;
    if (IsActionActive("MoveLeft")) move.x -= 1.0f;
    if (IsActionActive("MoveUp")) move.y += 1.0f;
    if (IsActionActive("MoveDown")) move.y -= 1.0f;
    return move;
}
