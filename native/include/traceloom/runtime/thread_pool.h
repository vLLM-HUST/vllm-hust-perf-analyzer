#pragma once

#include <cstddef>
#include <functional>

namespace traceloom {

class ThreadPool {
 public:
  explicit ThreadPool(std::size_t thread_count);

  std::size_t thread_count() const noexcept { return thread_count_; }
  void parallel_for(std::size_t task_count,
                    const std::function<void(std::size_t)>& task) const;

 private:
  std::size_t thread_count_ = 1;
};

}  // namespace traceloom
