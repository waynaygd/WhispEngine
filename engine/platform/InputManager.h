#pragma once
#include "../ecs/MathTypes.h"
#include <GLFW/glfw3.h>
#include <string>
#include <unordered_map>

class InputManager
{
public:
    void SetWindow(GLFWwindow* window) { m_Window = window; }
    void BindAction(const std::string& action, int glfwKey) { m_ActionMap[action] = glfwKey; }
    bool IsActionActive(const std::string& action) const;
    bool IsKeyPressed(int glfwKey) const;
    ecs::Vec3 GetMovementAxis() const;
private:
    GLFWwindow* m_Window = nullptr;
    std::unordered_map<std::string, int> m_ActionMap;
};
