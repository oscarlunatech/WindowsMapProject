#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "cartograph/jobs/thread_pool.h"

using cartograph::jobs::ThreadPool;

TEST_CASE("submit runs a task and returns its result via the future", "[jobs]") {
    ThreadPool pool(2);
    std::future<int> future = pool.submit([] { return 40 + 2; });
    REQUIRE(future.get() == 42);
}

TEST_CASE("many submitted tasks all run and their results are all collectible", "[jobs]") {
    ThreadPool pool(4);
    constexpr int kTaskCount = 200;

    std::vector<std::future<int>> futures;
    futures.reserve(kTaskCount);
    for (int i = 0; i < kTaskCount; ++i) {
        futures.push_back(pool.submit([i] { return i * i; }));
    }

    for (int i = 0; i < kTaskCount; ++i) {
        REQUIRE(futures[static_cast<std::size_t>(i)].get() == i * i);
    }
}

TEST_CASE("an exception thrown in a task propagates through future::get", "[jobs]") {
    ThreadPool pool(2);
    std::future<int> future = pool.submit([]() -> int { throw std::runtime_error("boom"); });
    REQUIRE_THROWS_AS(future.get(), std::runtime_error);
}

TEST_CASE("tasks actually run concurrently across worker threads", "[jobs]") {
    ThreadPool pool(4);
    std::atomic<int> concurrent{0};
    std::atomic<int> maxConcurrent{0};

    auto task = [&] {
        const int now = ++concurrent;
        int prevMax = maxConcurrent.load();
        while (now > prevMax && !maxConcurrent.compare_exchange_weak(prevMax, now)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        --concurrent;
        return now;
    };

    std::vector<std::future<int>> futures;
    for (int i = 0; i < 4; ++i) {
        futures.push_back(pool.submit(task));
    }
    for (auto& f : futures) {
        f.get();
    }

    REQUIRE(maxConcurrent.load() > 1);
}
