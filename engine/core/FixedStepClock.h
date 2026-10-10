#pragma once
#include <algorithm>
#include <cmath>

// One bounded schedule for physics and optional fixed StateMachine updates.
// Overload policy: execute at most 4 ticks, drop whole excess ticks, keep the
// fractional remainder. Dropped time is explicit; simulation slows under load.
class FixedStepClock {
public:
    static constexpr double TickSeconds = 1.0 / 60.0;
    static constexpr int MaxTicks = 4;
    struct Plan { int ticks = 0; double droppedSeconds = 0; };
    Plan Advance(double realSeconds, bool active = true) {
        if (!active) { m_Accumulator = 0; return {}; }
        if (!std::isfinite(realSeconds) || realSeconds < 0) return {};
        m_Accumulator += realSeconds;
        const double whole = std::floor((m_Accumulator + 1e-12) / TickSeconds);
        Plan plan;
        plan.ticks = static_cast<int>(std::min(whole, double(MaxTicks)));
        plan.droppedSeconds = std::max(0.0, whole - plan.ticks) * TickSeconds;
        m_Accumulator = std::max(0.0, m_Accumulator - whole * TickSeconds);
        m_Dropped += plan.droppedSeconds;
        return plan;
    }
    void Reset() { m_Accumulator = 0; m_Dropped = 0; }
    double Accumulator() const { return m_Accumulator; }
    double DroppedSeconds() const { return m_Dropped; }
private:
    double m_Accumulator = 0, m_Dropped = 0;
};
