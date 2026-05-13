#include "GameplayState.h"
#include "../../core/Logger.h"
#include "../../core/Application.h"
#include <string>
#include "MenuState.h"

void GameplayState::OnEnter(Application& app)
{
    app.EnterGameplayScene();
    m_PrevEsc = false;
    m_PrevSpace = false;
    m_PrevBackspace = false;
    m_PrevF = false;
    m_PrevF3 = false;
    Logger::Get().Info(
        "GameplayState: OnEnter (ESC -> Menu, SPACE -> spawn ECS entity, F -> fire projectile, BACKSPACE -> destroy ECS entity).");
}

void GameplayState::OnExit(Application& app)
{
    app.ExitGameplayScene();
    Logger::Get().Info("GameplayState: OnExit");
}

void GameplayState::Update(Application& app, float dt)
{
    (void)dt;

    const bool esc = app.IsInputActionActive("PauseToMenu");
    const bool rawSpace = app.IsInputActionActive("SpawnEntity");
    const bool backspace = app.IsInputActionActive("DestroyEntity");
    const bool fire = app.IsInputActionActive("FireProjectile");
    const bool debugF3 = app.IsInputActionActive("ToggleDebugColliders");
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

    if (fire && !m_PrevF)
        app.SpawnPhysicsProjectile();
    if (debugF3 && !m_PrevF3)
        app.ToggleDebugColliders();

    m_PrevEsc = esc;
    m_PrevSpace = rawSpace;
    m_PrevBackspace = backspace;
    m_PrevF = fire;
    m_PrevF3 = debugF3;
}


void GameplayState::Render(Application& app, IRenderAdapter& renderer)
{
    (void)app;
    (void)renderer;
}
