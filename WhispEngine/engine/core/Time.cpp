#include "Time.h"

void Time::Initialize()
{
    m_LastTime = Clock::now();
}

float Time::Tick()
{
    auto now = Clock::now();
    std::chrono::duration<float> delta = now - m_LastTime;

    m_DeltaTime = delta.count();

    if (m_DeltaTime > 0.1f)
        m_DeltaTime = 0.1f;

    m_LastTime = now;
    return m_DeltaTime;
}
