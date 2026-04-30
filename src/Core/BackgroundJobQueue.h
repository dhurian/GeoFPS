#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace GeoFPS
{
class BackgroundJobQueue
{
  public:
    explicit BackgroundJobQueue(size_t workerCount = 0);
    ~BackgroundJobQueue();

    BackgroundJobQueue(const BackgroundJobQueue&) = delete;
    BackgroundJobQueue& operator=(const BackgroundJobQueue&) = delete;

    template <typename Task>
    auto Enqueue(Task&& task) -> std::future<std::invoke_result_t<Task>>
    {
        using Result = std::invoke_result_t<Task>;
        auto packagedTask = std::make_shared<std::packaged_task<Result()>>(std::forward<Task>(task));
        std::future<Result> future = packagedTask->get_future();
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Jobs.emplace([packagedTask]() { (*packagedTask)(); });
        }
        m_Condition.notify_one();
        return future;
    }

    [[nodiscard]] size_t WorkerCount() const;

  private:
    void WorkerLoop();

    mutable std::mutex m_Mutex;
    std::condition_variable m_Condition;
    std::queue<std::function<void()>> m_Jobs;
    std::vector<std::thread> m_Workers;
    bool m_Stopping {false};
};
} // namespace GeoFPS
