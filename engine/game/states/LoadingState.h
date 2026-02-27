#pragma once
#include "../IGameState.h"

class LoadingState final : public IGameState
{
public:
    const char* Name() const override { return "Loading"; }

    void OnEnter(Application& app) override;
    void Update(Application& app, float dt) override;
    void Render(Application& app, IRenderAdapter& renderer) override;

private:
    float m_Time = 0.0f;
};
