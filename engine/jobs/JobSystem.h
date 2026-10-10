#pragma once

#include <cstdint>
#include <memory>
#include <functional>
#include <vector>

namespace enki
{
    class TaskScheduler;
    class TaskSet;
}

class JobSystem
{
public:
    // Submit, query, wait and shutdown from the initializing thread only.
    // Jobs operate on their assigned data; nested/external submission is not supported.
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
    // Scheduler APIs are used by the initializing thread. Retain submitted
    // tasks even when a caller drops its handle before completion.
    void Retain(const TaskHandle& task);
    std::vector<TaskHandle> m_InFlight;
    std::unique_ptr<enki::TaskScheduler> m_Scheduler;
    bool m_Initialized = false;
};
