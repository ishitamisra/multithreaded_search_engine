#pragma once
// A small, general-purpose thread pool used to parallelize crawling,
// indexing and query fan-out. Tasks are submitted as std::function<void()>
// and executed on a fixed number of worker threads pulled from a shared
// queue; submit() returns a std::future so callers can wait on results.

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace engine {

class ThreadPool {
 public:
  explicit ThreadPool(size_t numThreads);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  template <typename F, typename... Args>
  auto submit(F&& f, Args&&... args)
      -> std::future<std::invoke_result_t<F, Args...>> {
    using Ret = std::invoke_result_t<F, Args...>;
    auto task = std::make_shared<std::packaged_task<Ret()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    std::future<Ret> result = task->get_future();
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (stopping_) {
        throw std::runtime_error("submit() called on a stopped ThreadPool");
      }
      tasks_.emplace([task]() { (*task)(); });
    }
    cv_.notify_one();
    return result;
  }

  // Blocks until the task queue is empty and every worker is idle.
  void waitIdle();

  size_t numThreads() const { return workers_.size(); }

 private:
  void workerLoop();

  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::condition_variable idleCv_;
  bool stopping_ = false;
  std::atomic<size_t> activeTasks_{0};
};

}  // namespace engine
