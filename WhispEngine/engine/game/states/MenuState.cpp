#include "MenuState.h"
#include "../../core/Logger.h"
#include "../../core/Application.h"
#include "../../platform/GlfwWindow.h"
#include <GLFW/glfw3.h>
#include "GameplayState.h"

void MenuState::OnEnter(Application& app)
{
    (void)app;
    m_PrevEnter = false; 
    Logger::Get().Info("MenuState: OnEnter (ENTER -> Gameplay)");
}

void MenuState::Update(Application& app, float)
{
    auto* gw = static_cast<GlfwWindow*>(app.GetWindow());
    GLFWwindow* w = gw->GetGlfwHandle();

    const bool enterMain = glfwGetKey(w, GLFW_KEY_ENTER) == GLFW_PRESS;
    const bool enterKP = glfwGetKey(w, GLFW_KEY_KP_ENTER) == GLFW_PRESS;
    const bool enter = enterMain || enterKP;

    if (enter && !m_PrevEnter)
    {
        Logger::Get().Info("MenuState: ENTER detected -> switch to Gameplay");
        app.RequestStateChange(std::make_unique<GameplayState>());
    }

    m_PrevEnter = enter;
}


void MenuState::Render(Application& app, IRenderAdapter& renderer)
{
    (void)app;
    (void)renderer;
}
