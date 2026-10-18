#ifndef RELLU_FORMAT_PARALLEL_HPP
#define RELLU_FORMAT_PARALLEL_HPP
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace Rellu::Details {

  /**
   * Calls a function with every index in [0, count) from up to a number of
   * threads, then rethrows the first exception thrown by any call.
   * @param count The number of indices.
   * @param thread_count The largest number of threads, or 0 for one per
   *        processor.
   * @param f The function called with each index.
   */
  void parallel_for(std::size_t count, int thread_count, auto f) {
    auto concurrency = thread_count > 0 ? static_cast<std::size_t>(
      thread_count) : std::max<std::size_t>(
        std::thread::hardware_concurrency(), 1);
    concurrency = std::min(concurrency, count);
    if(concurrency <= 1) {
      for(auto i = std::size_t(0); i != count; ++i) {
        f(i);
      }
      return;
    }
    auto next = std::atomic<std::size_t>(0);
    auto is_failed = std::atomic<bool>(false);
    auto error = std::exception_ptr();
    auto mutex = std::mutex();
    auto work = [&] {
      while(!is_failed.load(std::memory_order_relaxed)) {
        auto i = next.fetch_add(1, std::memory_order_relaxed);
        if(i >= count) {
          return;
        }
        try {
          f(i);
        } catch(...) {
          auto lock = std::lock_guard(mutex);
          if(!error) {
            error = std::current_exception();
          }
          is_failed = true;
        }
      }
    };
    {
      auto threads = std::vector<std::jthread>();
      threads.reserve(concurrency - 1);
      for(auto i = std::size_t(1); i != concurrency; ++i) {
        threads.emplace_back(work);
      }
      work();
    }
    if(error) {
      std::rethrow_exception(error);
    }
  }
}

#endif
