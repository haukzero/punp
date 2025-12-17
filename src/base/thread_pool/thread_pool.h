#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace punp {

    class ThreadPool {
    private:
        std::vector<std::thread> _workers;
        std::queue<std::function<void()>> _tasks;
        std::mutex _queue_mtx;
        std::condition_variable _condition;
        std::atomic<bool> _stop;
        std::atomic<size_t> _active_threads{0};

        void worker_thread();

        static size_t opt_thread_cnt(size_t n_task = 0);

    public:
        explicit ThreadPool(size_t num_threads = 0);
        ~ThreadPool();

        void scaling(size_t new_size);

        template <typename F, typename... Args>
        auto submit(F &&f, Args &&...args) -> std::future<std::invoke_result_t<F, Args...>>;

        template <typename F, typename Callback, typename... Args>
        void submit_with_callback(F &&f, Callback &&cb, Args &&...args);

        size_t thread_cnt() const noexcept { return _workers.size(); }

        size_t idle_threads() const noexcept { return _workers.size() - _active_threads.load(); }
        bool has_idle_threads() const noexcept { return idle_threads() > 0 && !_stop.load(); }

        void shutdown();
    };

    template <typename F, typename... Args>
    auto ThreadPool::submit(F &&f, Args &&...args) -> std::future<std::invoke_result_t<F, Args...>> {
        using return_type = std::invoke_result_t<F, Args...>;
        using ArgsTuple = std::tuple<std::decay_t<Args>...>;

        auto args_tuple = ArgsTuple(std::forward<Args>(args)...);
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            [f = std::forward<F>(f), args = std::move(args_tuple)]() mutable -> return_type {
                return std::apply(std::forward<F>(f), std::move(args));
            });

        std::future<return_type> result = task->get_future();

        {
            std::lock_guard<std::mutex> lock(_queue_mtx);
            if (_stop) {
                throw std::runtime_error("Cannot submit task to stopped thread pool");
            }
            _tasks.emplace([task]() { (*task)(); });
        }

        _condition.notify_one();
        return result;
    }

    template <typename F, typename Callback, typename... Args>
    void ThreadPool::submit_with_callback(F &&f, Callback &&cb, Args &&...args) {
        using return_type = std::invoke_result_t<F, Args...>;
        using ArgsTuple = std::tuple<std::decay_t<Args>...>;

        auto args_tuple = ArgsTuple(std::forward<Args>(args)...);
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            [f = std::forward<F>(f), args = std::move(args_tuple)]() mutable -> return_type {
                return std::apply(std::forward<F>(f), std::move(args));
            });

        {
            std::lock_guard<std::mutex> lock(_queue_mtx);
            if (_stop) {
                throw std::runtime_error("Cannot submit task to stopped thread pool");
            }
            _tasks.emplace([task, cb = std::forward<Callback>(cb)]() {
                try {
                    (*task)();
                    if constexpr (std::is_void_v<return_type>) {
                        cb();
                    } else {
                        cb(task->get_future().get());
                    }
                } catch (...) {
                }
            });
        }

        _condition.notify_one();
    }

} // namespace punp
