module;
#include <doctest.h>
#include <verilated.h>
export module verilator_utils:task;
import :scheduler;

export namespace verilator_utils
{
    /**
     * @brief 生成时钟信号，占空比为50%
     *
     * @param scheduler 调度器引用
     * @param clk 时钟信号切片
     * @param period 时钟周期
     * @return 生成时钟信号的任务
     */
    [[nodiscard]] inline ::verilator_utils::task generate_clock(::verilator_utils::eval_scheduler& scheduler,
                                                                ::verilator_utils::bit_slice<::CData> clk,
                                                                ::verilator_utils::femtosecond_t period)
    {
        auto half_period{period / 2zu};
        clk = 0;
        while(true)
        {
            co_await scheduler.wait_time(half_period);
            clk = !clk;
        }
    }

    /**
     * @brief 生成复位信号，持续cycle个下降沿
     *
     * @param scheduler 调度器引用
     * @param reset 复位信号引用
     * @param clk 时钟信号引用
     * @param cycle 复位信号持续的下降沿数
     * @param active_high 复位信号的极性，true表示高电平有效，false表示低电平有效
     * @return 生成复位信号的任务
     */
    [[nodiscard]] inline ::verilator_utils::task generate_reset(::verilator_utils::eval_scheduler& scheduler,
                                                                ::verilator_utils::bit_slice<::CData> reset,
                                                                ::verilator_utils::bit_slice<::CData> clk,
                                                                ::size_t cycle = 3,
                                                                bool active_high = true)
    {
        reset = active_high;
        co_await scheduler.wait_negedge(clk, cycle);
        reset = !active_high;
    }

    /**
     * @brief 异步任务类型
     *
     */
    struct async_task
    {
        /// 协程柄类型
        using handle_t = ::std::coroutine_handle<::verilator_utils::task::promise_type>;

        /**
         * @brief 将同步任务转化为异步任务，并将任务添加到调度器就绪队列
         *
         * @note 任务必须处于initial_suspend状态，且不能有父任务
         * @param scheduler 调度器引用
         * @param task 同步任务对象
         */
        inline async_task(::verilator_utils::eval_scheduler& scheduler,
                          ::verilator_utils::detail::is_task_reference auto&& task) :
            callback{[subhandle = task.get_handle()] { return !subhandle || subhandle.done(); }}, scheduler{scheduler},
            subhandle{task.get_handle()}
        {
            using namespace ::std::string_view_literals;
            auto&& promise{task.get_promise()};
            REQUIRE_MESSAGE(promise.parent == nullptr, "该任务已经绑定到父任务，不能转化为异步任务"sv);
            REQUIRE_MESSAGE(promise.status == ::verilator_utils::task::status_enum::initial_suspend,
                            "该任务已开始执行，不能转化为异步任务"sv);
            promise.is_async = true;
            scheduler.add_task(task);
        }

        /**
         * @brief 析构异步任务对象并销毁绑定的子协程柄
         *
         * @note 若协程未完成，则将所有权交给调度器
         */
        inline ~async_task() { detach(); }

        inline async_task(const async_task&) noexcept = delete;
        inline async_task& operator= (const async_task&) noexcept = delete;
        inline async_task& operator= (async_task&& other) = delete;

        inline async_task(async_task&& other) noexcept :
            scheduler{other.scheduler}, subhandle{::std::exchange(other.subhandle, nullptr)}
        {
        }

        /**
         * @brief 销毁绑定的协程柄
         *
         */
        inline void destroy()
        {
            if(subhandle) { ::std::exchange(subhandle, nullptr).destroy(); }
        }

        /**
         * @brief 获取任务的协程句柄
         *
         * @return 任务的协程句柄
         */
        inline handle_t get_handle() const { return subhandle; }

        /**
         * @brief 获取任务的promise引用
         *
         * @return 任务的promise引用
         */
        inline ::verilator_utils::task::promise_type& get_promise() const noexcept { return subhandle.promise(); }

        /**
         * @brief 获取绑定的调度器引用
         *
         * @return eval_scheduler& 调度器引用
         */
        inline ::verilator_utils::eval_scheduler& get_scheduler() { return scheduler; }

        /**
         * @brief 判断子任务是否执行完
         *
         * @note 未绑定到子任务视为执行完
         * @return 子任务是否执行完
         */
        inline bool done() const noexcept { return !subhandle || subhandle.done(); }

        /**
         * @brief 分离异步任务的协程柄，此后异步任务不再持有该协程柄
         *
         * @note 若任务已完成则销毁协程柄，若任务未完成则托管给调度器
         * @return 异步任务的协程柄
         */
        inline void detach() noexcept
        {
            if(subhandle)
            {
                if(subhandle.done()) { subhandle.destroy(); }
                else
                {
                    // 将孤儿协程托管给调度器
                    subhandle.promise().is_async = false;
                }
                subhandle = nullptr;
            }
        }

        /**
         * @brief 检查任务对象是否绑定了协程柄
         *
         * @return 是否绑定了协程柄
         */
        inline explicit operator bool() const noexcept { return static_cast<bool>(subhandle); }

        /**
         * @brief 检查任务对象是否可等待
         *
         * @return 是否可等待
         */
        inline bool joinable() const noexcept { return static_cast<bool>(subhandle); }

        /**
         * @brief 判断是否立即完成
         *
         * @return 子任务已执行完则立即完成
         */
        inline bool await_ready() const
        {
            REQUIRE(joinable());
            return done();
        }

        /**
         * @brief 向调度器事件队列中注册等待事件，然后挂起协程
         *
         * @param handle 当前任务的协程柄
         */
        inline void await_suspend(handle_t handle) { scheduler.register_event(callback, handle); }

        /**
         * @brief 恢复等待任务的执行
         *
         * @note 异步任务下，父子任务同时存在于调度队列中，不能在子任务完成前恢复父任务
         * @throws eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
         * @throws 若子任务抛出异常则重新抛出异常
         */
        inline void await_resume()
        {
            // 协作式取消的优先级更高
            scheduler.throw_if_finish();
            subhandle.promise().rethrow_exception();
        }

    private:
        /// 事件回调函数，判断子任务是否完成
        ::verilator_utils::default_event_callback callback;
        /// 调度器引用
        ::verilator_utils::eval_scheduler& scheduler;
        /// 子任务的协程柄
        handle_t subhandle;
    };

    /**
     * @brief 等待所有异步任务完成用的可等待体
     *
     */
    struct task_join_awaiter
    {
        inline task_join_awaiter(::std::span<::verilator_utils::async_task> tasks, ::verilator_utils::eval_scheduler& scheduler) :
            callback{[tasks] { return all_tasks_done(tasks); }}, tasks{tasks}, scheduler{scheduler}
        {
        }

        /**
         * @brief 判断是否立即完成
         *
         * @return 所有子任务都已执行完则立即完成
         */
        inline bool await_ready() { return callback(); }

        /**
         * @brief 向调度器事件队列中注册等待事件，然后挂起协程
         *
         * @param handle 当前任务的协程柄
         */
        inline void await_suspend(::verilator_utils::async_task::handle_t handle) { scheduler.register_event(callback, handle); }

        /// 储存子任务中未处理的异常
        using unhandled_exception_pair = ::std::pair<::verilator_utils::async_task&, ::std::exception_ptr>;
        /// 若子任务存在未处理的异常则抛出该向量
        using unhandled_exception_vector = ::std::vector<unhandled_exception_pair>;

        /**
         * @brief 恢复等待任务的执行
         *
         * @throws eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
         * @throws unhandled_exception_vector 子任务中未处理的异常
         */
        inline void await_resume()
        {
            scheduler.throw_if_finish();
            auto vec{::std::views::filter(tasks,
                                          [](::verilator_utils::async_task& task) static noexcept
                                          { return task.get_promise().with_unhandled_exception(); }) |
                     ::std::views::transform([](::verilator_utils::async_task& task) static noexcept
                                             { return unhandled_exception_pair{task, task.get_promise().exception}; }) |
                     ::std::ranges::to<unhandled_exception_vector>()};

            if(!vec.empty())
            {
                throw vec;  // NOLINT(misc-throw-by-value-catch-by-reference)
            }
        }

    private:
        /**
         * @brief 判断是否所有子任务都执行完毕
         *
         * @return 是否所有子任务都执行完毕
         */
        inline static bool all_tasks_done(::std::span<::verilator_utils::async_task> tasks)
        {
            return ::std::ranges::all_of(tasks, [](::verilator_utils::async_task& task) { return task.done(); });
        }

        /// 事件回调函数，判断子任务是否都完成
        ::verilator_utils::default_event_callback callback;
        /// 子任务视图
        ::std::span<::verilator_utils::async_task> tasks;
        /// 调度器引用
        ::verilator_utils::eval_scheduler& scheduler;
    };

    /**
     * @brief 等待所有异步任务完成
     *
     * @param scheduler 调度器引用
     * @param tasks 异步任务视图
     * @return task_join_awaiter 可等待体
     */
    [[nodiscard]] inline ::verilator_utils::task_join_awaiter task_join(::verilator_utils::eval_scheduler& scheduler,
                                                                        ::std::span<::verilator_utils::async_task> tasks)
    { return ::verilator_utils::task_join_awaiter{tasks, scheduler}; }

    /**
     * @brief 等待到验证时机
     *
     * @param scheduler 调度器引用
     * @param clk 时钟信号切片
     * @param edge_to_wait 要等待到边沿个数
     * @return 可等待体
     * @note 等待到n个时钟上升沿后的after_dut_eval阶段
     */
    [[nodiscard]] inline ::verilator_utils::eval_scheduler::event_awaiter
        wait_for_verify(::verilator_utils::eval_scheduler& scheduler,
                        ::verilator_utils::bit_slice<::CData> clk,
                        ::std::size_t edge_to_wait = 1)
    {
        REQUIRE_NE(edge_to_wait, 0);
        using edge_detector_t = ::verilator_utils::edge_detector;
        return scheduler.wait_event(
            [edge_detector = edge_detector_t{[clk] { return static_cast<bool>(clk); }, edge_detector_t::rising},
             edge_to_wait,
             &scheduler] mutable
            {
                if(edge_detector()) { --edge_to_wait; }
                return edge_to_wait == 0 &&
                       scheduler.get_eval_stage() == ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval;
            });
    }

    /**
     * @brief 等待到激励时机
     *
     * @param scheduler 调度器引用
     * @param clk 时钟信号切片
     * @param edge_to_wait 要等待到边沿个数
     * @return 可等待体
     * @note 等待到n个时钟下降沿后的before_dut_eval阶段
     */
    [[nodiscard]] inline ::verilator_utils::eval_scheduler::event_awaiter
        wait_for_stimulus(::verilator_utils::eval_scheduler& scheduler,
                          ::verilator_utils::bit_slice<::CData> clk,
                          ::std::size_t edge_to_wait = 1)
    { return scheduler.wait_negedge(clk, edge_to_wait); }

    /**
     * @brief 等待直到复位完成
     *
     * @note 这会等待直到复位信号无效且初始评估完成
     * @param scheduler 调度器引用
     * @param rst 复位信号切片
     * @param active_high 复位信号的极性，true表示高电平有效，false表示低电平有效
     * @return 可等待体
     */
    [[nodiscard]] inline ::verilator_utils::eval_scheduler::event_awaiter
        wait_until_reset_finish(::verilator_utils::eval_scheduler& scheduler,
                                ::verilator_utils::bit_slice<::CData> rst,
                                bool active_high = true)
    {
        return scheduler.wait_event(
            [&scheduler, rst, active_high]
            {
                return scheduler.get_eval_stage() >= ::verilator_utils::eval_scheduler::eval_stage_enum::after_initial_eval &&
                       rst == !active_high;
            });
    }

    /**
     * @brief 等待到指定评估阶段
     *
     * @param scheduler 调度器引用
     * @param eval_stage 评估阶段
     * @return 可等待体
     * @code {.cpp}
     * task foo(eval_scheduler& scheduler)
     * {
     *     // 等待到电路评估完成，此时加入的激励在下一周期生效
     *     co_await wait_for_eval_stage(scheduler, eval_scheduler::eval_stage_enum::after_dut_eval);
     * }
     * @endcode
     */
    [[nodiscard]] inline ::verilator_utils::eval_scheduler::event_awaiter
        wait_for_eval_stage(::verilator_utils::eval_scheduler& scheduler,
                            ::verilator_utils::eval_scheduler::eval_stage_enum eval_stage)
    {
        return scheduler.wait_event([&scheduler, eval_stage] { return scheduler.get_eval_stage() == eval_stage; });
    }
}  // namespace verilator_utils
