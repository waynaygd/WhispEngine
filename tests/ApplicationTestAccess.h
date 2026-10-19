#pragma once
#include "core/Application.h"
#include <cmath>
#include <stdexcept>

// Inject elapsed time while exercising Application::Run, the real fixed-step
// scheduler, PhysicsSystem, EventBus and Lua delivery. Never sleep to earn a tick.
struct ApplicationTestAccess {
    // Keep the real camera/binding services while an integration fixture feeds
    // named input actions independently of the foreground desktop pointer.
    static scripting::GameplayServices GameplayHost(Application& app){return app.GameplayCapabilities();}
    // The headless desktop can reject native cursor warps. Seed the existing
    // capture state for UI regressions; release still takes the production path.
    static bool GameplayCapture(Application& app,bool capture){
        if(!capture)return app.CaptureGameplayCursor(false);
        app.m_PlayerMouseCaptured=true;app.m_InputManager.ResetMouseDelta();return true;
    }
    static void FrameDelta(Application& app,float seconds){
        if(!std::isfinite(seconds)||seconds<=0||seconds>.1f)throw std::runtime_error("Invalid test frame delta");
        app.m_Time.m_TestFrameDelta=seconds;
    }
};
