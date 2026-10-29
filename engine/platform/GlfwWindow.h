#pragma once
#include "IWindow.h"
#include <string>
#include <array>

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
    double ConsumeScrollDeltaY();
    void AddScrollDeltaY(double delta);
    void PointerMoved(double x,double y){m_Pointer={x,y};m_HasPointer=true;}
    void PointerLeft(){m_Pointer={-1,-1};m_HasPointer=true;}
    std::array<double,2> PointerPosition()const;

    void SetTitle(const char* title) override;

private:
    GLFWwindow* m_Window = nullptr;
    bool m_HoldsGlfw=false;
    double m_PendingScrollDeltaY = 0.0;
    std::array<double,2> m_Pointer{};bool m_HasPointer=false;
};
