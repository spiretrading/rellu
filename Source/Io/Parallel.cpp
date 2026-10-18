#include <algorithm>
#include "Parallel.hpp"

using namespace Rellu;

WorkerPool& WorkerPool::get() {
  static auto pool = WorkerPool();
  return pool;
}

WorkerPool::WorkerPool()
    : m_is_stopping(false) {
  auto count = std::max(1U, std::thread::hardware_concurrency()) - 1;
  m_threads.reserve(count);
  for(auto i = 0U; i != count; ++i) {
    m_threads.emplace_back([this] {
      serve();
    });
  }
}

WorkerPool::~WorkerPool() {
  {
    auto lock = std::lock_guard(m_mutex);
    m_is_stopping = true;
  }
  m_ready.notify_all();
  for(auto& thread : m_threads) {
    thread.join();
  }
}

std::size_t WorkerPool::get_size() const {
  return m_threads.size();
}

void WorkerPool::run(std::size_t helpers, void (*work)(void*), void* context) {
  auto count = std::min(helpers, m_threads.size());
  auto job = Job(work, context, count, 0);
  if(count != 0) {
    {
      auto lock = std::lock_guard(m_mutex);
      m_jobs.push_back(&job);
    }
    if(count == 1) {
      m_ready.notify_one();
    } else {
      m_ready.notify_all();
    }
  }
  work(context);
  auto lock = std::unique_lock(m_mutex);
  if(job.m_unclaimed != 0) {
    job.m_unclaimed = 0;
    m_jobs.erase(std::find(m_jobs.begin(), m_jobs.end(), &job));
  }
  job.m_done.wait(lock, [&] {
    return job.m_running == 0;
  });
}

void WorkerPool::serve() {
  auto lock = std::unique_lock(m_mutex);
  while(true) {
    m_ready.wait(lock, [&] {
      return m_is_stopping || !m_jobs.empty();
    });
    if(m_is_stopping) {
      return;
    }
    auto job = m_jobs.front();
    --job->m_unclaimed;
    ++job->m_running;
    if(job->m_unclaimed == 0) {
      m_jobs.pop_front();
    }
    lock.unlock();
    job->m_work(job->m_context);
    lock.lock();
    --job->m_running;
    if(job->m_running == 0) {
      job->m_done.notify_all();
    }
  }
}
