#include "StateMachine.h"
#include "IGameState.h"

void StateMachine::ChangeState(std::unique_ptr<IGameState> newState)
{
    if (m_CurrentState)
        m_CurrentState->OnExit();

    m_CurrentState = std::move(newState);

    if (m_CurrentState)
        m_CurrentState->OnEnter();
}

void StateMachine::Update(float dt)
{
    if (m_CurrentState)
        m_CurrentState->Update(dt);
}

void StateMachine::Render(IRenderAdapter& renderer)
{
    if (m_CurrentState)
        m_CurrentState->Render(renderer);
}
