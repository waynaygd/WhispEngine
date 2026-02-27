#include "LoadingState.h"
#include "../../core/Logger.h"
#include "../../core/Application.h"
#include "../StateMachine.h"
#include "MenuState.h"

void LoadingState::OnEnter(Application& app)
{
    (void)app;
    m_Time = 0.0f;
    Logger::Get().Info("LoadingState: OnEnter");
}

void LoadingState::Update(Application& app, float dt)
{
    (void)app;
    m_Time += dt;

    if (m_Time >= 1.0f)
    {
        app.RequestStateChange(std::make_unique<MenuState>());
    }
}

void LoadingState::Render(Application& app, IRenderAdapter& renderer)
{
    (void)app;
    (void)renderer;
}
