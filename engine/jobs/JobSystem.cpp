#include "JobSystem.h"

#include "../core/Logger.h"

#include <TaskScheduler.h>
#include <algorithm>

JobSystem::JobSystem() = default;

JobSystem::~JobSystem()
{
    Shutdown();
}

bool JobSystem::Initialize()
{
    if (m_Initialized)
        return true;

    m_Scheduler = std::make_unique<enki::TaskScheduler>();
    m_Scheduler->Initialize();

    m_Initialized = true;

    Logger::Get().Info(
        "JobSystem: initialized with " +
        std::to_string(m_Scheduler->GetNumTaskThreads()) +
        " task threads");

    return true;
}

void JobSystem::Shutdown()
{
    if (!m_Initialized)
        return;

    if (m_Scheduler != nullptr)
    {
        m_Scheduler->WaitforAllAndShutdown();
        m_Scheduler.reset();
    }

    m_Initialized = false;

    Logger::Get().Info("JobSystem: shutdown");
}

bool JobSystem::IsInitialized() const
{
    return m_Initialized;
}

std::uint32_t JobSystem::GetWorkerCount() const
{
    if (m_Scheduler == nullptr)
        return 0;

    return m_Scheduler->GetNumTaskThreads();
}

enki::TaskScheduler& JobSystem::GetScheduler()
{
    return *m_Scheduler;
}

JobSystem::TaskHandle JobSystem::Execute(std::function<void()> job)
{
    if (!m_Initialized || m_Scheduler == nullptr || !job)
        return nullptr;

    auto task = std::make_shared<enki::TaskSet>(
        1,
        [job = std::move(job)](
            enki::TaskSetPartition,
            std::uint32_t)
        {
            job();
        });

    m_Scheduler->AddTaskSetToPipe(task.get());

    return task;
}

JobSystem::TaskHandle JobSystem::Dispatch(
    std::uint32_t itemCount,
    std::uint32_t minRange,
    RangeJob job)
{
    if (!m_Initialized ||
        m_Scheduler == nullptr ||
        !job ||
        itemCount == 0)
    {
        return nullptr;
    }

    minRange =
        std::max(
            1u,
            std::min(
                minRange,
                itemCount));

    auto task =
        std::make_shared<enki::TaskSet>(
            itemCount,
            [job = std::move(job)](
                enki::TaskSetPartition range,
                std::uint32_t threadIndex)
            {
                job(
                    range.start,
                    range.end,
                    threadIndex);
            });

    task->m_MinRange = minRange;

    m_Scheduler->AddTaskSetToPipe(
        task.get());

    return task;
}

bool JobSystem::IsComplete(const TaskHandle& task) const
{
    if (task == nullptr)
        return true;

    return task->GetIsComplete();
}

void JobSystem::Wait(const TaskHandle& task)
{
    if (m_Scheduler == nullptr || task == nullptr)
        return;

    m_Scheduler->WaitforTask(task.get());
}

void JobSystem::WaitAll()
{
    if (m_Scheduler == nullptr)
        return;

    m_Scheduler->WaitforAll();
}