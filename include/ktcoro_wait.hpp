#ifndef KTCORO_WAIT_HPP_
#define KTCORO_WAIT_HPP_

#include <coroutine>
#include <list>
#include <chrono>

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

class ktcoro_wait {
public:
    static ktcoro_wait& Instance() {
        static ktcoro_wait inst{};
        return inst;
    }

    void process() {
        for (auto& task : tasks) {
            if (std::chrono::steady_clock::now() > task.wake_time) {
                task.handle();
                task.completed = true;
            }
        }
        tasks.remove_if([](auto& task) { return task.completed; });
    }
    std::list<detail::ctx>& get_task_list() {
        return tasks;
    }

private:
    std::list<detail::ctx> tasks;
};

struct ktwait {
    class promise_type {
    public:
        void return_void() const {}

        auto initial_suspend() const {
            return std::suspend_never{};
        }

        auto final_suspend() const noexcept {
            return std::suspend_never{};
        }

        void unhandled_exception() const {}

        auto get_return_object() {
            return ktwait{};
        }

        template <class Clock, class Duration>
        auto yield_value(std::list<detail::ctx>& wq, const std::chrono::time_point<Clock, Duration>& time) const {
            struct schedule_for_execution {
                std::list<detail::ctx>& wq;
                std::chrono::time_point<std::chrono::steady_clock,
                    std::chrono::milliseconds> t;

                constexpr bool await_ready() const noexcept { return false; }
                void await_suspend(std::coroutine_handle<> this_coro) const {
                    wq.emplace_back(this_coro, t);
                }
                constexpr void await_resume() const noexcept {}
            };
            return schedule_for_execution{ wq, std::chrono::time_point_cast<std::chrono::milliseconds>(time) };
        }

        auto yield_value() const {
            return yield_value(ktcoro_wait::Instance().get_task_list(), std::chrono::steady_clock::now());
        }

        template <class Rep, class Period>
        auto await_transform(const std::chrono::duration<Rep, Period>& time) {
            return yield_value(ktcoro_wait::Instance().get_task_list(), std::chrono::steady_clock::now() + time);
        }

        auto await_transform(unsigned long msecs) {
            return yield_value(ktcoro_wait::Instance().get_task_list(), std::chrono::steady_clock::now() + std::chrono::milliseconds(msecs));
        }

        auto await_transform(ktwait handle) {
            return std::suspend_never{};
        }
    };
};
#endif // KTCORO_WAIT_HPP_