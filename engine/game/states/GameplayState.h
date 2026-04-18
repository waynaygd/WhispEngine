#pragma once
#include "../IGameState.h"

class GameplayState final : public IGameState
{
public:
    const char* Name() const override { return "Gameplay"; }

    void OnEnter(Application& app) override;
    void OnExit(Application& app) override;
    void Update(Application& app, float dt) override;
    void Render(Application& app, IRenderAdapter& renderer) override;

private:
    bool m_PrevEsc = false;
    bool m_PrevSpace = false;
    bool m_PrevBackspace = false;
};
