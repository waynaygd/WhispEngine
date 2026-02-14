#pragma once
#include "IWindow.h"
#include <string>

struct GLFWwindow;

class GlfwWindow final : public IWindow
{
public:
    ~GlfwWindow() override;

    bool Create(int width, int height, const std::string& title) override;
    void PollEvents() override;
    bool ShouldClose() const override;
    void* GetNativeHandle() const override;

    GLFWwindow* GetGlfwHandle() const { return m_Window; }

private:
    GLFWwindow* m_Window = nullptr;
};
