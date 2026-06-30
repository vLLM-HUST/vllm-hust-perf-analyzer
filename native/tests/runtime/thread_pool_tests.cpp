#include "traceloom/runtime/thread_pool.h"
#include "traceloom/testing/test_util.h"

#include <atomic>
#include <stdexcept>
#include <vector>

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  ThreadPool single_thread(1);
  require(single_thread.thread_count() == 1);

  std::atomic<int> sum{0};
  ThreadPool pool(4);
  require(pool.thread_count() == 4);
  pool.parallel_for(100, [&](std::size_t index) {
    sum.fetch_add(static_cast<int>(index), std::memory_order_relaxed);
  });
  require(sum.load() == 4950);

  std::vector<int> values(10, 0);
  pool.parallel_for(values.size(), [&](std::size_t index) {
    values[index] = static_cast<int>(index * index);
  });
  for (std::size_t index = 0; index < values.size(); ++index) {
    require(values[index] == static_cast<int>(index * index));
  }

  bool caught_exception = false;
  try {
    pool.parallel_for(10, [](std::size_t index) {
      if (index == 3) {
        throw std::runtime_error("planned failure");
      }
    });
  } catch (const std::runtime_error&) {
    caught_exception = true;
  }
  require(caught_exception);

  pool.parallel_for(0, [](std::size_t) { require(false); });

  return 0;
}
