#ifndef RELLU_IO_PARALLEL_HPP
#define RELLU_IO_PARALLEL_HPP
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace Rellu {

  /**
   * Threads kept for running work in parallel, sleeping while there is
   * none.
   */
  class WorkerPool {
    public:

      /** Returns the pool shared by every parallel operation. */
      static WorkerPool& get();

      /** Constructs a WorkerPool with a thread per additional processor. */
      WorkerPool();

      /** Stops and joins the threads. */
      ~WorkerPool();

      /** Returns the number of threads. */
      std::size_t get_size() const;

      /**
       * Runs a function on the calling thread and on up to a number of the
       * pool's threads at once, returning once every call has returned.
       * Calls not yet started when the calling thread's call returns are
       * not made.
       * @param helpers The largest number of the pool's threads to use.
       * @param work The function to call.
       * @param context The argument passed to the <i>work</i>.
       */
      void run(std::size_t helpers, void (*work)(void*), void* context);

    private:
      struct Job {
        void (*m_work)(void*);
        void* m_context;
        std::size_t m_unclaimed;
        std::size_t m_running;
        std::condition_variable m_done;
      };
      std::mutex m_mutex;
      std::condition_variable m_ready;
      std::deque<Job*> m_jobs;
      bool m_is_stopping;
      std::vector<std::thread> m_threads;

      WorkerPool(const WorkerPool&) = delete;
      WorkerPool& operator =(const WorkerPool&) = delete;
      void serve();
  };

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
    auto& pool = WorkerPool::get();
    auto helpers =
      std::min(count, pool.get_size() + 1) - std::min<std::size_t>(count, 1);
    if(helpers == 0) {
      work();
    } else {
      pool.run(helpers, [] (void* context) {
        (*static_cast<decltype(work)*>(context))();
      }, &work);
    }
    if(error) {
      std::rethrow_exception(error);
    }
  }
}

#endif
