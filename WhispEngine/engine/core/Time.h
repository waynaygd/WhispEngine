#pragma once
#include <chrono>

class Time
{
public:
    void Initialize();
    float Tick();
    float GetDeltaTime() const { return m_DeltaTime; }

private:
    using Clock = std::chrono::high_resolution_clock;

    Clock::time_point m_LastTime;
    float m_DeltaTime = 0.0f;
};
