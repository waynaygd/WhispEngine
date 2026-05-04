#include "GameplayState.h"
#include "../../core/Logger.h"
#include "../../core/Application.h"
#include "../../platform/GlfwWindow.h"
#include <GLFW/glfw3.h>
#include <string>
#include "MenuState.h"

void GameplayState::OnEnter(Application& app)
{
    app.EnterGameplayScene();
    m_PrevEsc = false;
    m_PrevSpace = false;
    m_PrevBackspace = false;
    Logger::Get().Info(
        "GameplayState: OnEnter (ESC -> Menu, SPACE -> spawn ECS entity when camera look is inactive, BACKSPACE -> destroy ECS entity).");
}

void GameplayState::OnExit(Application& app)
{
    app.ExitGameplayScene();
    Logger::Get().Info("GameplayState: OnExit");
}

void GameplayState::Update(Application& app, float dt)
{
    (void)dt;

    auto* gw = static_cast<GlfwWindow*>(app.GetWindow());
    GLFWwindow* w = gw->GetGlfwHandle();

    const bool esc = glfwGetKey(w, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    const bool rawSpace = glfwGetKey(w, GLFW_KEY_SPACE) == GLFW_PRESS;
    const bool backspace = glfwGetKey(w, GLFW_KEY_BACKSPACE) == GLFW_PRESS;
    const bool space = !app.IsCameraControlActive() && rawSpace;

    if (esc && !m_PrevEsc)
    {
        Logger::Get().Info("GameplayState: ESC detected -> switch to Menu");
        app.RequestStateChange(std::make_unique<MenuState>());
    }

    if (space && !m_PrevSpace)
    {
        app.SpawnGameplayEntity();
        Logger::Get().Info(
            "GameplayState: SPACE detected -> spawned ECS entity, total=" +
            std::to_string(app.GetGameplayEntityCount()));
    }

    if (backspace && !m_PrevBackspace)
    {
        app.DestroyLastGameplayEntity();
        Logger::Get().Info(
            "GameplayState: BACKSPACE detected -> destroy request processed, total=" +
            std::to_string(app.GetGameplayEntityCount()));
    }

    m_PrevEsc = esc;
    m_PrevSpace = rawSpace;
    m_PrevBackspace = backspace;
}


void GameplayState::Render(Application& app, IRenderAdapter& renderer)
{
    (void)app;
    (void)renderer;
}
