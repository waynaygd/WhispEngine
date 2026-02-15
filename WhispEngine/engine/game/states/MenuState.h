#pragma once
#include "../IGameState.h"

class MenuState final : public IGameState
{
public:
    const char* Name() const override { return "Menu"; }

    void OnEnter(Application& app) override;
    void Update(Application& app, float dt) override;
    void Render(Application& app, IRenderAdapter& renderer) override;

private:
    bool m_PrevEnter = false;
};
