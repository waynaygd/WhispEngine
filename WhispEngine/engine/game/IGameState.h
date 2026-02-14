#pragma once

class IRenderAdapter;

class IGameState
{
public:
    virtual ~IGameState() = default;

    virtual void OnEnter() {}
    virtual void OnExit() {}

    virtual void Update(float dt) = 0;
    virtual void Render(IRenderAdapter& renderer) = 0;
};
