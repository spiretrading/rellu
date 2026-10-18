#ifndef RELLU_IO_PARALLEL_HPP
#define RELLU_IO_PARALLEL_HPP
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace Rellu {

  /**
   * Calls a function with every index in [0, count) on all processors,
   * rethrowing the first exception thrown once every call has finished.
   * @param count The number of indices.
   * @param f The function to call with each index.
   */
  void parallel_for(std::size_t count, auto f) {
    auto next = std::atomic<std::size_t>(0);
    auto error = std::exception_ptr();
    auto mutex = std::mutex();
    auto work = [&] {
      while(true) {
        auto index = next.fetch_add(1);
        if(index >= count) {
          return;
        }
        try {
          f(index);
        } catch(...) {
          auto lock = std::lock_guard(mutex);
          if(!error) {
            error = std::current_exception();
          }
        }
      }
    };
    auto thread_count = std::min<std::size_t>(
      count, std::max(1U, std::thread::hardware_concurrency()));
    auto threads = std::vector<std::jthread>();
    if(thread_count > 1) {
      threads.reserve(thread_count - 1);
      for(auto i = std::size_t(1); i < thread_count; ++i) {
        threads.emplace_back(work);
      }
    }
    work();
    for(auto& thread : threads) {
      thread.join();
    }
    if(error) {
      std::rethrow_exception(error);
    }
  }
}

#endif
