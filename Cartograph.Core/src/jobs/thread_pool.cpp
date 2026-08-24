#include "cartograph/jobs/thread_pool.h"

namespace cartograph::jobs {

ThreadPool::ThreadPool(std::size_t threadCount) {
    workers_.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i) {
        workers_.emplace_back([this] {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    condition_.wait(lock, [this] { return stop_ || !queue_.empty(); });
                    if (stop_ && queue_.empty()) {
                        return;
                    }
                    task = std::move(queue_.front());
                    queue_.pop();
                }
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    condition_.notify_all();
    for (std::thread& worker : workers_) {
        worker.join();
    }
}

}  // namespace cartograph::jobs
