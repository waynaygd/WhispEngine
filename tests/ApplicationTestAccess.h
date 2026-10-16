#pragma once
#include "core/Application.h"
#include <cmath>
#include <stdexcept>

// Inject elapsed time while exercising Application::Run, the real fixed-step
// scheduler, PhysicsSystem, EventBus and Lua delivery. Never sleep to earn a tick.
struct ApplicationTestAccess {
    static void FrameDelta(Application& app,float seconds){
        if(!std::isfinite(seconds)||seconds<=0||seconds>.1f)throw std::runtime_error("Invalid test frame delta");
        app.m_Time.m_TestFrameDelta=seconds;
    }
};
