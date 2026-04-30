#include "Core/BackgroundJobQueue.h"

#include <algorithm>

namespace GeoFPS
{
BackgroundJobQueue::BackgroundJobQueue(size_t workerCount)
{
    if (workerCount == 0)
    {
        workerCount = std::max<size_t>(1, std::thread::hardware_concurrency() > 1 ?
                                              static_cast<size_t>(std::thread::hardware_concurrency()) - 1 :
                                              1);
    }

    m_Workers.reserve(workerCount);
    for (size_t index = 0; index < workerCount; ++index)
    {
        m_Workers.emplace_back([this]() { WorkerLoop(); });
    }
}

BackgroundJobQueue::~BackgroundJobQueue()
{
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Stopping = true;
    }
    m_Condition.notify_all();

    for (std::thread& worker : m_Workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}

size_t BackgroundJobQueue::WorkerCount() const
{
    return m_Workers.size();
}

void BackgroundJobQueue::WorkerLoop()
{
    while (true)
    {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Condition.wait(lock, [this]() { return m_Stopping || !m_Jobs.empty(); });
            if (m_Stopping && m_Jobs.empty())
            {
                return;
            }

            job = std::move(m_Jobs.front());
            m_Jobs.pop();
        }

        job();
    }
}
} // namespace GeoFPS
