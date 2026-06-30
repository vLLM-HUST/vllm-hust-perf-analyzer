#include "traceloom/runtime/thread_pool.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace traceloom {

ThreadPool::ThreadPool(std::size_t thread_count)
    : thread_count_(std::max<std::size_t>(1, thread_count)) {}

void ThreadPool::parallel_for(
    std::size_t task_count,
    const std::function<void(std::size_t)>& task) const {
  if (task_count == 0) {
    return;
  }
  if (thread_count_ == 1 || task_count == 1) {
    for (std::size_t index = 0; index < task_count; ++index) {
      task(index);
    }
    return;
  }

  const std::size_t worker_count = std::min(thread_count_, task_count);
  std::atomic<std::size_t> next_task{0};
  std::exception_ptr first_exception;
  std::mutex exception_mutex;
  std::vector<std::thread> workers;
  workers.reserve(worker_count);

  for (std::size_t worker = 0; worker < worker_count; ++worker) {
    workers.emplace_back([&]() {
      while (true) {
        const std::size_t index = next_task.fetch_add(1);
        if (index >= task_count) {
          break;
        }
        try {
          task(index);
        } catch (...) {
          std::lock_guard<std::mutex> lock(exception_mutex);
          if (!first_exception) {
            first_exception = std::current_exception();
          }
        }
      }
    });
  }

  for (std::thread& worker : workers) {
    worker.join();
  }
  if (first_exception) {
    std::rethrow_exception(first_exception);
  }
}

}  // namespace traceloom
