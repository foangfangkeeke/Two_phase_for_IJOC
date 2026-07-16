#pragma once

#include "solver_config.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace ParallelConfig {

inline int MasterThreads(const SolverConfig& config)
{
    return std::max(1, config.threads);
}

inline int SubProblemThreads()
{
    return 1;
}

inline size_t MaxSubProblemConcurrency(const SolverConfig& config)
{
    return static_cast<size_t>(std::max(1, config.threads));
}

} // namespace ParallelConfig

template <typename Fn>
auto RunBoundedIndexedTasks(size_t taskCount, size_t maxConcurrent, Fn&& fn)
    -> std::vector<std::invoke_result_t<Fn, size_t>>
{
    using Result = std::invoke_result_t<Fn, size_t>;

    std::vector<Result> results(taskCount);
    if (taskCount == 0) {
        return results;
    }

    const size_t workerCount = std::max<size_t>(1, std::min(maxConcurrent, taskCount));
    std::atomic<size_t> nextTask{0};
    std::atomic<bool> stop{false};
    std::exception_ptr firstException = nullptr;
    std::mutex exceptionMutex;
    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    for (size_t workerIdx = 0; workerIdx < workerCount; ++workerIdx) {
        workers.emplace_back([&]() {
            while (true) {
                if (stop.load()) {
                    break;
                }
                const size_t taskIdx = nextTask.fetch_add(1);
                if (taskIdx >= taskCount) {
                    break;
                }
                try {
                    results[taskIdx] = fn(taskIdx);
                } catch (...) {
                    std::lock_guard<std::mutex> lock(exceptionMutex);
                    if (!firstException) {
                        firstException = std::current_exception();
                    }
                    stop.store(true);
                }
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    if (firstException) {
        std::rethrow_exception(firstException);
    }
    return results;
}
