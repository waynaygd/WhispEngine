#pragma once

#include <cstdint>
#include <memory>
#include <functional>

namespace enki
{
    class TaskScheduler;
    class TaskSet;
}

class JobSystem
{
public:
    JobSystem();
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    bool Initialize();
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const;
    [[nodiscard]] std::uint32_t GetWorkerCount() const;

    enki::TaskScheduler& GetScheduler();

    using TaskHandle = std::shared_ptr<enki::TaskSet>;

    using RangeJob = std::function<void(
        std::uint32_t begin,
        std::uint32_t end,
        std::uint32_t threadIndex)>;

    TaskHandle Execute(std::function<void()> job);

    TaskHandle Dispatch(
        std::uint32_t itemCount,
        std::uint32_t minRange,
        RangeJob job);

    [[nodiscard]] bool IsComplete(const TaskHandle& task) const;

    void Wait(const TaskHandle& task);
    void WaitAll();

private:
    std::unique_ptr<enki::TaskScheduler> m_Scheduler;
    bool m_Initialized = false;
};