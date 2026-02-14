#pragma once
#include <memory>
#include "IGameState.h"

class IGameState;
class IRenderAdapter;

class StateMachine
{
public:
    void ChangeState(std::unique_ptr<IGameState> newState);

    void Update(float dt);
    void Render(IRenderAdapter& renderer);

private:
    std::unique_ptr<IGameState> m_CurrentState;
};
