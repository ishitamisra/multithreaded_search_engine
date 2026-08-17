#include <atomic>
#include <numeric>
#include <vector>

#include "engine/threadpool.h"
#include "test_framework.h"

using namespace engine;

TEST_MAIN_BEGIN()
  ThreadPool pool(4);
  CHECK_EQ(pool.numThreads(), (size_t)4);

  std::atomic<int> counter{0};
  std::vector<std::future<void>> futures;
  for (int i = 0; i < 200; ++i) {
    futures.push_back(pool.submit([&counter]() { counter.fetch_add(1); }));
  }
  for (auto& f : futures) f.get();
  CHECK_EQ(counter.load(), 200);

  std::vector<std::future<int>> squares;
  for (int i = 0; i < 10; ++i) {
    squares.push_back(pool.submit([i]() { return i * i; }));
  }
  int sum = 0;
  for (auto& f : squares) sum += f.get();
  CHECK_EQ(sum, 285);  // sum of squares 0..9

  pool.waitIdle();
TEST_MAIN_END()
