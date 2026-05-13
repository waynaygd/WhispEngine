#pragma once
#include "../Entity.h"
#include <functional>
#include <vector>
namespace ecs { struct CollisionEvent { Entity a{}; Entity b{}; }; class EventBus { public: using CollisionListener = std::function<void(const CollisionEvent&)>; void SubscribeCollision(CollisionListener listener){ m_CollisionListeners.push_back(std::move(listener)); } void PublishCollision(const CollisionEvent& event) const { for (const auto& listener : m_CollisionListeners) listener(event);} private: std::vector<CollisionListener> m_CollisionListeners; }; }
