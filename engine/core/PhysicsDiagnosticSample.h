#pragma once
#include "../ecs/systems/PhysicsSystem.h"
#include "PhysicsFrameStatistics.h"
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

// Opt-in diagnostics only. Samples are buffered and written after simulation so
// file IO cannot become a measured frame spike. No physical settings are changed.
struct PhysicsDiagnosticSample {
    std::string phase;
    bool parallel = false;
    unsigned frame = 0;
    double frameMs = 0;
    ecs::PhysicsSystem::Statistics physics;
    PhysicsFrameStatistics fixed;
    PhysicsDiagnosticSample(std::string label, bool mode, unsigned index, double milliseconds,
        const ecs::PhysicsSystem::Statistics& stats)
        : phase(std::move(label)), parallel(mode), frame(index), frameMs(milliseconds), physics(stats) {
        fixed.AddTick(stats); // Headless samples measure exactly one Update.
    }
    PhysicsDiagnosticSample(std::string label, bool mode, unsigned index, double milliseconds,
        const ecs::PhysicsSystem::Statistics& stats, const PhysicsFrameStatistics& frameStats)
        : phase(std::move(label)), parallel(mode), frame(index), frameMs(milliseconds), physics(stats), fixed(frameStats) {}
};
inline bool SavePhysicsDiagnostics(const std::string& path, const std::vector<PhysicsDiagnosticSample>& samples)
{
    std::ofstream file(path);
    if (!file) return false;
    file << "phase,mode,frame,frameMs,physicsMs,bodies,sleeping,substeps,pairs,contacts,points,integrateMs,broadphaseMs,narrowphaseMs,solverMs,poseMs,projectionMs,dispatchMs,waitMs,solverIterations,physicsTicks,lastTickMs,internalSubsteps,accumulatorSeconds,droppedSeconds,totalDroppedSeconds,simulationSeconds,broadphaseRebuilds,broadphaseReuses,broadphaseBufferGrowths,aabbComputations,activeBodies\n";
    file << std::setprecision(9);
    for (const auto& row : samples) {
        const auto& s = row.physics;
        file << row.phase << ',' << (row.parallel ? "Parallel" : "Serial") << ',' << row.frame << ','
             << row.frameMs << ',' << s.totalMs << ',' << s.bodies << ',' << s.sleeping << ',' << s.substeps << ','
             << s.pairs << ',' << s.contacts << ',' << s.contactPoints << ',' << s.integrateMs << ',' << s.broadphaseMs << ','
             << s.narrowphaseMs << ',' << s.solverMs << ',' << s.poseMs << ',' << s.projectionMs << ',' << s.dispatchMs << ',' << s.waitMs << ',' << s.solverIterations << ','
             << row.fixed.ticks << ',' << row.fixed.lastTickMs << ',' << row.fixed.internalSubsteps << ',' << row.fixed.accumulatorSeconds << ','
             << row.fixed.droppedSeconds << ',' << row.fixed.totalDroppedSeconds << ',' << row.fixed.simulationSeconds << ','
             << s.broadphaseRebuilds << ',' << s.broadphaseReuses << ',' << s.broadphaseBufferGrowths << ',' << s.aabbComputations << ',' << s.activeBodies << '\n';
    }
    return bool(file);
}
