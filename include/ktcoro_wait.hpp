#ifndef KTCORO_WAIT_HPP_
#define KTCORO_WAIT_HPP_

#include <coroutine>
#include <list>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <map>

namespace detail {
    struct ctx {
        std::coroutine_handle<> handle;
        std::chrono::time_point<std::chrono::steady_clock,
            std::chrono::milliseconds> wake_time;
        bool completed{ false };

        ctx(std::coroutine_handle<> h, std::chrono::time_point<std::chrono::steady_clock,
            std::chrono::milliseconds> t) : handle(h), wake_time(t) {

        }
    };
}

struct ktwait;

class ktcoro_wait {
public:
    static ktcoro_wait& Instance() {
        static ktcoro_wait inst{};
        return inst;
    }

    void process() {
        std::lock_guard lock(tasklists_mut);
        auto& local_tasklist = tasklists[std::this_thread::get_id()];
        for (auto& task : local_tasklist) {
            if (std::chrono::steady_clock::now() > task.wake_time && task.handle) {
                task.handle();
                task.completed = true;
            }
        }
        local_tasklist.remove_if([](auto& task) { return task.completed; });
    }

    void process_all_tasks() {
        std::lock_guard lock(tasklists_mut);
        for (auto& tasklist : tasklists) {
            for (auto& task : tasklist.second) {
                if (std::chrono::steady_clock::now() > task.wake_time && task.handle) {
                    task.handle();
                    task.completed = true;
                }
            }
            tasklist.second.remove_if([](auto& task) { return task.completed; });
        }
    }

    void add_task(std::coroutine_handle<>& h, const std::chrono::time_point<std::chrono::steady_clock, std::chrono::milliseconds>& time) {
        tasklists[std::this_thread::get_id()].emplace_back(h, time);
    }

    void remove_task_for_current_thread(std::coroutine_handle<> coro_handle) {
        remove_task_for_thread(std::this_thread::get_id(), coro_handle);
    }

    void remove_task_for_thread(std::thread::id thread_id, std::coroutine_handle<> coro_handle) {
        std::lock_guard lock(tasklists_mut);
        auto& local_tasklist = tasklists[thread_id];
        for (auto task = local_tasklist.begin(); task != local_tasklist.end(); ++task) {
            if (task->handle == coro_handle) {
                task->handle.destroy();
                local_tasklist.erase(task);
                break;
            }
        }
    }

    void remove_task(std::coroutine_handle<> coro_handle) {
        std::lock_guard lock(tasklists_mut);
        for (auto& local_tasklist : tasklists) {
            for (auto task = local_tasklist.second.begin(); task != local_tasklist.second.end(); ++task) {
                if (task->handle == coro_handle) {
                    task->handle.destroy();
                    local_tasklist.second.erase(task);
                    break;
                }
            }
        }
    }

    const std::list<detail::ctx>& get_thread_tasklist() {
        return tasklists[std::this_thread::get_id()];
    }

    const std::map<std::thread::id, std::list<detail::ctx>>& get_tasklists() {
        return tasklists;
    }

private:
    std::map<std::thread::id, std::list<detail::ctx>> tasklists;
    std::recursive_mutex tasklists_mut;
};

struct ktwait {
    class promise_type {
    public:
        void return_void() const {}

        auto initial_suspend() const {
            return std::suspend_never{};
        }

        auto final_suspend() noexcept {
            struct execute_on_suspend {
                promise_type* me;

                constexpr bool await_ready() const noexcept { return true; }
                constexpr void await_suspend(std::coroutine_handle<promise_type> h) const noexcept {}
                void await_resume() const noexcept {
                    if (me->waiter)
                        me->waiter();
                }
            };
            return execute_on_suspend{ this };
        }

        void unhandled_exception() const {}

        auto get_return_object() {
            return ktwait{ std::coroutine_handle<promise_type>::from_promise(*this) };
        }

        template <class Clock, class Duration>
        auto yield_value(ktcoro_wait& wq, const std::chrono::time_point<Clock, Duration>& time) const {
            struct schedule_for_execution {
                ktcoro_wait& wq;
                std::chrono::time_point<std::chrono::steady_clock,
                    std::chrono::milliseconds> t;

                constexpr bool await_ready() const noexcept { return false; }
                void await_suspend(std::coroutine_handle<> this_coro) const {
                    wq.add_task(this_coro, t);
                }
                constexpr void await_resume() const noexcept {}
            };
            return schedule_for_execution{ wq, std::chrono::time_point_cast<std::chrono::milliseconds>(time) };
        }

        auto yield_value() const {
            return yield_value(ktcoro_wait::Instance(), std::chrono::steady_clock::now());
        }

        template <class Rep, class Period>
        auto await_transform(const std::chrono::duration<Rep, Period>& time) {
            return yield_value(ktcoro_wait::Instance(), std::chrono::steady_clock::now() + time);
        }

        auto await_transform(unsigned long msecs) {
            return yield_value(ktcoro_wait::Instance(), std::chrono::steady_clock::now() + std::chrono::milliseconds(msecs));
        }

        auto await_transform(ktwait waitobj) {
            struct execute_on_await {
                ktwait waitobj;

                constexpr bool await_ready() const noexcept {
                    return false;
                }
                void await_suspend(std::coroutine_handle<promise_type> h) const {
                    waitobj.coro.promise().waiter = h;
                }
                void await_resume() const noexcept { }
            };
            return execute_on_await{ waitobj };
        }

        std::coroutine_handle<promise_type> waiter;
    };

    ktwait(std::coroutine_handle<promise_type> h) : coro(h) {}

    std::coroutine_handle<promise_type> coro;
    std::coroutine_handle<promise_type> suspended_by;
};
#endif // KTCORO_WAIT_HPP_