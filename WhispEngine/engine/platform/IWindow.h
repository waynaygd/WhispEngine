#pragma once
#include <string>

class IWindow
{
public:
    virtual ~IWindow() = default;

    virtual bool Create(int width, int height, const std::string& title) = 0;
    virtual void PollEvents() = 0;
    virtual bool ShouldClose() const = 0;
    virtual void* GetNativeHandle() const = 0;
};
