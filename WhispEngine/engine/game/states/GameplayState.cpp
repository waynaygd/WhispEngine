#include "GameplayState.h"
#include "../../core/Logger.h"
#include "../../core/Application.h"
#include "../../platform/GlfwWindow.h"
#include <GLFW/glfw3.h>
#include "MenuState.h"

void GameplayState::OnEnter(Application& app)
{
    (void)app;
    m_PrevEsc = false;
    Logger::Get().Info("GameplayState: OnEnter (ESC -> Menu).");
}

void GameplayState::Update(Application& app, float dt)
{
    app.UpdateInputAndTransform(dt);

    auto* gw = static_cast<GlfwWindow*>(app.GetWindow());
    GLFWwindow* w = gw->GetGlfwHandle();

    const bool esc = glfwGetKey(w, GLFW_KEY_ESCAPE) == GLFW_PRESS;

    if (esc && !m_PrevEsc)
    {
        Logger::Get().Info("GameplayState: ESC detected -> switch to Menu");
        app.RequestStateChange(std::make_unique<MenuState>());
    }

    m_PrevEsc = esc;
}


void GameplayState::Render(Application& app, IRenderAdapter& renderer)
{
    (void)app;
    (void)renderer;
}
