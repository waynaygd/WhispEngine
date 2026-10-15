#pragma once
#include <imgui.h>
// Native renderer regressions queue synthetic ImGui events. GLFW's NewFrame can
// otherwise append the desktop cursor after those events, making picking depend
// on an unrelated foreground app/mouse. Only the test's queued input is accepted
// while this scope is active; the production backend remains unchanged.
struct SyntheticEditorInput {
    ImGuiContext* context=ImGui::GetCurrentContext();
    bool accepting=ImGui::GetIO().AppAcceptingEvents;
    SyntheticEditorInput(){ImGui::GetIO().SetAppAcceptingEvents(false);}
    ~SyntheticEditorInput(){if(context&&ImGui::GetCurrentContext()==context)ImGui::GetIO().SetAppAcceptingEvents(accepting);}
    template<class F>static void Queue(F event){auto& io=ImGui::GetIO();const bool previous=io.AppAcceptingEvents;io.SetAppAcceptingEvents(true);event(io);io.SetAppAcceptingEvents(previous);}
    static void Position(float x,float y){Queue([&](ImGuiIO& io){io.AddMousePosEvent(x,y);});}
    static void Button(int button,bool down){Queue([&](ImGuiIO& io){io.AddMouseButtonEvent(button,down);});}
    static void Key(ImGuiKey key,bool down){Queue([&](ImGuiIO& io){io.AddKeyEvent(key,down);});}
};
