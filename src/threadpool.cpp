#include "engine/threadpool.h"

namespace engine {

ThreadPool::ThreadPool(size_t numThreads) {
  if (numThreads == 0) numThreads = 1;
  workers_.reserve(numThreads);
  for (size_t i = 0; i < numThreads; ++i) {
    workers_.emplace_back([this] { workerLoop(); });
  }
}

ThreadPool::~ThreadPool() {
  {
    std::unique_lock<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  cv_.notify_all();
  for (auto& t : workers_) {
    if (t.joinable()) t.join();
  }
}

void ThreadPool::workerLoop() {
  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
      if (tasks_.empty()) {
        if (stopping_) return;
        continue;
      }
      task = std::move(tasks_.front());
      tasks_.pop();
      ++activeTasks_;
    }
    task();
    {
      std::unique_lock<std::mutex> lock(mutex_);
      --activeTasks_;
      if (tasks_.empty() && activeTasks_ == 0) idleCv_.notify_all();
    }
  }
}

void ThreadPool::waitIdle() {
  std::unique_lock<std::mutex> lock(mutex_);
  idleCv_.wait(lock, [this] { return tasks_.empty() && activeTasks_ == 0; });
}

}  // namespace engine
