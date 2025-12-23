#include "thread_pool.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

namespace {

int square_task(int value,
                std::atomic<int>* started,
                std::shared_future<void> gate) {
    started->fetch_add(1, std::memory_order_relaxed);
    gate.wait();
    return value * value;
}

void increment_task(std::atomic<int>* done) {
    done->fetch_add(1, std::memory_order_relaxed);
}

int throw_task() {
    throw std::runtime_error("boom");
}

void finish_task(std::atomic<int>* finished, int sleep_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    finished->fetch_add(1, std::memory_order_relaxed);
}

bool expect_true(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

bool wait_for_at_least(std::atomic<int>& value,
                       int expected,
                       std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (value.load(std::memory_order_relaxed) >= expected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return value.load(std::memory_order_relaxed) >= expected;
}

}  // namespace

int main() {
    bool ok = true;

    {
        ThreadPool pool(2);
        ok &= expect_true(pool.get_thread_count() == 2, "thread count mismatch");

        std::promise<void> gate;
        std::shared_future<void> gate_future = gate.get_future().share();
        std::atomic<int> started{0};

        std::vector<std::future<int>> futures;
        futures.reserve(6);
        for (int i = 0; i < 6; ++i) {
            futures.push_back(pool.dispatch_task(std::bind(square_task, i, &started, gate_future)));
        }

        ok &= expect_true(wait_for_at_least(started, 2, std::chrono::milliseconds(500)),
                          "workers did not start tasks in time");
        ok &= expect_true(pool.get_queue_size() == 4, "queue size does not match expected backlog");

        gate.set_value();

        int sum = 0;
        for (auto& future : futures) {
            sum += future.get();
        }
        ok &= expect_true(sum == 55, "unexpected sum of task results");
    }

    {
        ThreadPool pool(3);
        std::atomic<int> done{0};
        std::vector<std::future<void>> futures;
        futures.reserve(5);
        for (int i = 0; i < 5; ++i) {
            futures.push_back(pool.dispatch_task(std::bind(increment_task, &done)));
        }
        for (auto& future : futures) {
            future.get();
        }
        ok &= expect_true(done.load(std::memory_order_relaxed) == 5, "void tasks did not complete");

        auto exception_future = pool.dispatch_task(throw_task);
        try {
            (void)exception_future.get();
            ok &= expect_true(false, "exception not propagated from task");
        } catch (const std::runtime_error&) {
        } catch (...) {
            ok &= expect_true(false, "unexpected exception type");
        }
    }

    std::atomic<int> finished{0};
    {
        ThreadPool pool(2);
        for (int i = 0; i < 4; ++i) {
            pool.dispatch_task(std::bind(finish_task, &finished, 30));
        }
    }
    ok &= expect_true(finished.load(std::memory_order_relaxed) == 4,
                      "destructor did not wait for queued tasks");

    if (ok) {
        std::cout << "All ThreadPool tests passed.\n";
        return 0;
    }

    std::cerr << "ThreadPool tests failed.\n";
    return 1;
}
