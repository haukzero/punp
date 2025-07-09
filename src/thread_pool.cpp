#include "thread_pool.h"
#include <algorithm>

namespace punp {

    ThreadPool::ThreadPool(size_t num_threads) : _stop(false) {
        size_t n_thread = num_threads;
        if (n_thread == 0) {
            n_thread = opt_thread_cnt();
        }

        _workers.reserve(n_thread);
        for (size_t i = 0; i < n_thread; ++i) {
            _workers.emplace_back(&ThreadPool::worker_thread, this);
        }
    }

    ThreadPool::~ThreadPool() {
        shutdown();
    }

    void ThreadPool::scaling(size_t n_inc) {
        if (n_inc == 0) {
            return; // No scaling needed
        }

        size_t new_thread_count = _workers.size() + n_inc;
        _workers.reserve(new_thread_count);
        for (size_t i = _workers.size(); i < new_thread_count; ++i) {
            _workers.emplace_back(&ThreadPool::worker_thread, this);
        }
    }

    void ThreadPool::shutdown() {
        {
            std::lock_guard<std::mutex> lock(_queue_mtx);
            if (_stop) {
                return; // Already stopped
            }
            _stop = true;
        }

        _condition.notify_all();

        for (std::thread &worker : _workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        _workers.clear();
    }

    void ThreadPool::worker_thread() {
        while (true) {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(_queue_mtx);
                _condition.wait(lock, [this] { return _stop || !_tasks.empty(); });

                if (_stop && _tasks.empty()) {
                    break;
                }

                task = std::move(_tasks.front());
                _tasks.pop();
            }

            _active_threads.fetch_add(1);
            try {
                task();
            } catch (...) {
                // Swallow exceptions to prevent thread termination
            }
            _active_threads.fetch_sub(1);
        }
    }

    size_t ThreadPool::opt_thread_cnt(size_t n_task) {
        size_t hw_thread_cap = std::thread::hardware_concurrency();
        if (hw_thread_cap == 0) {
            hw_thread_cap = 1; // Fallback
        }

        if (n_task == 0) {
            return hw_thread_cap;
        }

        return std::min(n_task, hw_thread_cap);
    }

} // namespace punp
