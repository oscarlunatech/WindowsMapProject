#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace cartograph::jobs {

// A fixed-size pool of worker threads that run arbitrary callables submitted
// via submit(), returning a std::future for the result. Used both for
// one-off background work (e.g. loading a Dataset) and for "submit N tasks,
// wait for all N" fan-out (e.g. per-layer draw prep) - callers do the
// fan-out/collection themselves, there's no separate parallel-for API.
//
// A task must never call submit() and then block waiting on the result of
// another task on the *same* pool - with a fixed number of workers, that can
// deadlock (every worker blocked waiting on work that has no free worker
// left to run it). Fan-out/collection is only ever done from a thread that
// isn't itself a pool worker (e.g. the caller of submit()).
class ThreadPool {
public:
    // Parenthesized as (std::max) rather than std::max(...) so it isn't
    // mangled by windows.h's function-like max() macro in any translation
    // unit that includes both windows.h (without NOMINMAX) and this header.
    explicit ThreadPool(std::size_t threadCount = (std::max)(1u, std::thread::hardware_concurrency()));
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Worker thread count - callers use this to size chunked work (e.g.
    // "one task per worker" rather than "one task per item") so per-task
    // overhead doesn't dominate when there are many small items.
    std::size_t size() const { return workers_.size(); }

    template <class F>
    auto submit(F&& f) -> std::future<std::invoke_result_t<F>> {
        using Result = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(f));
        std::future<Result> future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.emplace([task] { (*task)(); });
        }
        condition_.notify_one();
        return future;
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> queue_;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool stop_ = false;
};

}  // namespace cartograph::jobs
