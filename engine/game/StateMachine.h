#pragma once
#include <memory>
#include "IGameState.h"

class Application;
class IRenderAdapter;

class StateMachine
{
public:
    void ChangeState(std::unique_ptr<IGameState> newState); 
    void ApplyPending(Application& app);                    

    void Update(Application& app, float dt);
    void Render(Application& app, IRenderAdapter& renderer);

private:
    std::unique_ptr<IGameState> m_CurrentState;
    std::unique_ptr<IGameState> m_PendingState;
};
