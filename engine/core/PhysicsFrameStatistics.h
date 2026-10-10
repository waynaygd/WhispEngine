#pragma once
#include "../ecs/systems/PhysicsSystem.h"
#include <algorithm>
struct PhysicsFrameStatistics {
    ecs::PhysicsSystem::Statistics physics;
    int ticks = 0, internalSubsteps = 0;
    double lastTickMs = 0, accumulatorSeconds = 0;
    double droppedSeconds = 0, totalDroppedSeconds = 0, simulationSeconds = 0;
    void AddTick(const ecs::PhysicsSystem::Statistics& s) {
        ++ticks; internalSubsteps += s.substeps; lastTickMs = s.totalMs;
        physics.bodies = s.bodies; physics.sleeping = s.sleeping;
        physics.activeBodies = s.activeBodies;
        physics.pairs = std::max(physics.pairs, s.pairs);
        physics.contacts = std::max(physics.contacts, s.contacts);
        physics.contactPoints = std::max(physics.contactPoints, s.contactPoints);
        physics.substeps = std::max(physics.substeps, s.substeps);
        physics.solverIterations = s.solverIterations;
        physics.totalMs += s.totalMs; physics.integrateMs += s.integrateMs;
        physics.broadphaseMs += s.broadphaseMs; physics.narrowphaseMs += s.narrowphaseMs;
        physics.solverMs += s.solverMs; physics.poseMs += s.poseMs;
        physics.projectionMs += s.projectionMs; physics.dispatchMs += s.dispatchMs;
        physics.waitMs += s.waitMs;
        physics.broadphaseRebuilds += s.broadphaseRebuilds;
        physics.broadphaseReuses += s.broadphaseReuses;
        physics.broadphaseBufferGrowths += s.broadphaseBufferGrowths;
        physics.aabbComputations += s.aabbComputations;
    }
};
