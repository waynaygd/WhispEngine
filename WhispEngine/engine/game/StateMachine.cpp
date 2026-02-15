#include "StateMachine.h"

void StateMachine::ChangeState(std::unique_ptr<IGameState> newState)
{
    m_PendingState = std::move(newState);
    if (m_PendingState)
        m_PendingState->SetStateMachine(this);
}

void StateMachine::ApplyPending(Application& app)
{
    if (!m_PendingState) return;

    if (m_CurrentState)
        m_CurrentState->OnExit(app);

    m_CurrentState = std::move(m_PendingState);

    if (m_CurrentState)
        m_CurrentState->OnEnter(app);
}

void StateMachine::Update(Application& app, float dt)
{
    if (m_CurrentState)
        m_CurrentState->Update(app, dt);
}

void StateMachine::Render(Application& app, IRenderAdapter& renderer)
{
    if (m_CurrentState)
        m_CurrentState->Render(app, renderer);
}
