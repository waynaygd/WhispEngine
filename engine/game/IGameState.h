#pragma once

class Application;
class IRenderAdapter;
class StateMachine;

class IGameState
{
public:
    virtual ~IGameState() = default;

    void SetStateMachine(StateMachine* sm) { m_SM = sm; }

    virtual const char* Name() const = 0;

    virtual void OnEnter(Application& app) { (void)app; }
    virtual void OnExit(Application& app) { (void)app; }

    virtual void Update(Application& app, float dt) = 0;
    virtual void Render(Application& app, IRenderAdapter& renderer) = 0;

protected:
    StateMachine* m_SM = nullptr;
};
