#include "stippling/engine/worker_pool.hpp"

namespace stippling {

WorkerPool::WorkerPool(unsigned thread_count) {
  for (unsigned index = 1; index < thread_count; ++index) {
    workers_.emplace_back([this]() { worker_loop(); });
  }
}

WorkerPool::~WorkerPool() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_all();
  for (auto& worker : workers_) {
    worker.join();
  }
}

unsigned WorkerPool::thread_count() const noexcept {
  return static_cast<unsigned>(workers_.size()) + 1u;
}

void WorkerPool::run(std::size_t count, const std::function<void(std::size_t)>& task) {
  if (workers_.empty() || count <= 1) {
    for (std::size_t index = 0; index < count; ++index) {
      task(index);
    }
    return;
  }

  {
    std::lock_guard lock(mutex_);
    task_ = &task;
    count_ = count;
    next_index_ = 0;
    busy_workers_ = workers_.size();
    error_ = nullptr;
    ++job_;
  }
  wake_.notify_all();
  claim_and_run();

  std::unique_lock lock(mutex_);
  done_.wait(lock, [this]() { return busy_workers_ == 0; });
  task_ = nullptr;
  if (error_) {
    std::rethrow_exception(error_);
  }
}

void WorkerPool::worker_loop() {
  std::uint64_t seen_job = 0;
  while (true) {
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [&]() { return stopping_ || job_ != seen_job; });
      if (stopping_) {
        return;
      }
      seen_job = job_;
    }

    claim_and_run();

    std::lock_guard lock(mutex_);
    if (--busy_workers_ == 0) {
      done_.notify_one();
    }
  }
}

void WorkerPool::claim_and_run() {
  for (auto index = next_index_.fetch_add(1); index < count_;
       index = next_index_.fetch_add(1)) {
    try {
      (*task_)(index);
    } catch (...) {
      std::lock_guard lock(mutex_);
      if (!error_) {
        error_ = std::current_exception();
      }
    }
  }
}

}  // namespace stippling
