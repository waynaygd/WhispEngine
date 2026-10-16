#pragma once
#include <chrono>
#include <optional>

class Time
{
public:
    void Initialize();
    float Tick();
    float GetDeltaTime() const { return m_DeltaTime; }
    float GetFrameDeltaTime() const { return m_FrameDeltaTime; }

private:
    friend struct ApplicationTestAccess;
    // Only the test adapter can supply a deterministic delta. Normal runs use steady_clock.
    std::optional<float> m_TestFrameDelta;
    using Clock = std::chrono::steady_clock;

    Clock::time_point m_LastTime;
    float m_DeltaTime = 0.0f;
    float m_FrameDeltaTime = 0.0f;
};
