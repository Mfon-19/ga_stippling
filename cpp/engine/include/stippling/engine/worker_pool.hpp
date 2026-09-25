#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace stippling {

/**
 * Fixed set of threads that run `task(i)` for every index of a job. The
 * calling thread takes part too, so a pool of N threads spawns N - 1 workers.
 */
class WorkerPool {
 public:
  explicit WorkerPool(unsigned thread_count);
  ~WorkerPool();

  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;

  [[nodiscard]] unsigned thread_count() const noexcept;

  /** Blocks until every index has run; rethrows the first task exception. */
  void run(std::size_t count, const std::function<void(std::size_t)>& task);

 private:
  std::vector<std::thread> workers_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable done_;
  const std::function<void(std::size_t)>* task_{nullptr};
  std::size_t count_{0};
  std::atomic<std::size_t> next_index_{0};
  std::size_t busy_workers_{0};
  std::uint64_t job_{0};
  bool stopping_{false};
  std::exception_ptr error_{};

  void worker_loop();
  void claim_and_run();
};

}  // namespace stippling
