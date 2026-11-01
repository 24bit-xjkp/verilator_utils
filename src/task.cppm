module;
#include <doctest_macros.hpp>
export module verilator_utils:task;
import :scheduler;
import :wrapper;

namespace
{
    using namespace ::std::string_view_literals;
}

namespace verilator_utils::detail
{
    /**
     * @brief 实现无挂起协程柄获取的可等待体
     *
     */
    template <typename promise_type>
    struct handle_awaiter : ::verilator_utils::detail::no_suspend_awaiter
    {
        using handle_t = ::std::coroutine_handle<promise_type>;
        handle_t handle{};

        template <::verilator_utils::is_coroutine_promise actual_promise_type>
            requires (::verilator_utils::same_as_any<promise_type, void, actual_promise_type>)
        void set_promise_impl(actual_promise_type& promise) noexcept
        { handle = ::std::coroutine_handle<actual_promise_type>::from_promise(promise); }

        [[nodiscard]] handle_t await_resume() const { return handle; }
    };

    /**
     * @brief 实现无挂起调度器引用获取的可等待体
     *
     */
    struct scheduler_awaiter : ::verilator_utils::detail::no_suspend_awaiter
    {
        ::verilator_utils::eval_scheduler* scheduler{};

        void set_promise_impl(::verilator_utils::detail::promise_base& promise) { scheduler = promise.check_scheduler(); }

        [[nodiscard]] ::verilator_utils::eval_scheduler& await_resume() const { return *scheduler; }
    };

    /**
     * @brief 实现结束仿真的可等待体
     *
     */
    struct eval_finish_awaiter : ::verilator_utils::detail::no_suspend_awaiter
    {
        static void set_promise_impl(::verilator_utils::detail::promise_base& promise) { promise.check_scheduler()->finish(); }

        /**
         * @brief 恢复当前任务执行
         *
         * @throws eval_finish_exception 仿真结束，抛出异常终止当前任务的执行
         */
        static void await_resume() { throw ::verilator_utils::eval_finish_exception{}; }
    };

    /**
     * @brief 实现仿真时间获取的可等待体
     *
     */
    struct time_in_string_awaiter : ::verilator_utils::detail::no_suspend_awaiter
    {
        ::verilator_utils::eval_scheduler* scheduler{};

        void set_promise_impl(::verilator_utils::detail::promise_base& promise) { scheduler = promise.check_scheduler(); }

        [[nodiscard]] ::std::string await_resume() const { return scheduler->time_in_string(); }
    };

    /**
     * @brief 实现仿真时间获取的可等待体
     *
     */
    struct time_in_time_unit_awaiter : ::verilator_utils::detail::no_suspend_awaiter
    {
        ::verilator_utils::eval_scheduler* scheduler{};

        void set_promise_impl(::verilator_utils::detail::promise_base& promise) { scheduler = promise.check_scheduler(); }

        [[nodiscard]] double await_resume() const { return scheduler->time_in_time_unit(); }
    };

    /**
     * @brief 实现仿真时间获取的可等待体
     *
     */
    struct time_in_time_precision_awaiter : ::verilator_utils::detail::no_suspend_awaiter
    {
        ::verilator_utils::eval_scheduler* scheduler{};

        void set_promise_impl(::verilator_utils::detail::promise_base& promise) { scheduler = promise.check_scheduler(); }

        [[nodiscard]] ::std::uint64_t await_resume() const { return scheduler->time_in_time_precision(); }
    };

    /**
     * @brief 实现延迟功能的可等待体
     *
     */
    struct time_awaiter : ::std::suspend_always
    {
        /// 等待时间，单位为飞秒
        ::verilator_utils::femtosecond_t time_to_wait;

        explicit time_awaiter(::verilator_utils::femtosecond_t time_to_wait) noexcept :
            ::std::suspend_always{}, time_to_wait{time_to_wait}
        {
        }

        /**
         * @brief 挂起等待，将当前任务加入等待队列
         *
         * @tparam promise_type 协程承诺类型
         * @param handle 当前协程的句柄
         */
        template <::verilator_utils::is_coroutine_promise promise_type>
        void await_suspend(::std::coroutine_handle<promise_type> handle)
        { handle.promise().check_scheduler()->register_wait(time_to_wait, handle); }
    };

    /**
     * @brief 实现事件触发功能的可等待体
     *
     */
    struct event_awaiter
    {
        /// 事件回调，用于判断事件是否触发
        ::verilator_utils::default_event_callback event_callback;

        /**
         * @brief 初始化可等待体
         *
         * @tparam callback_t 事件回调类型
         * @param callback 事件回调函数
         */
        template <::verilator_utils::is_event_callback callback_t>
        explicit event_awaiter(callback_t&& callback) : event_callback{::std::forward<callback_t>(callback)}
        {
        }

        /**
         * @brief 判断是否立即就绪
         *
         * @return 是否立即就绪
         */
        [[nodiscard]] bool await_ready() const { return event_callback(); }

        /**
         * @brief 挂起等待，将当前任务加入事件队列
         *
         * @tparam promise_type 协程承诺类型
         * @param handle 当前协程的句柄
         */
        template <::verilator_utils::is_coroutine_promise promise_type>
        void await_suspend(::std::coroutine_handle<promise_type> handle)
        {
            auto scheduler{handle.promise().check_scheduler()};
            scheduler->register_event(event_callback, handle);
        }

        /**
         * @brief 恢复等待任务的执行
         *
         */
        static void await_resume() noexcept {}
    };

    /**
     * @brief 实现评估阶段触发功能的可等待体
     *
     */
    struct eval_stage_awaiter : ::std::suspend_always
    {
        using scheduler_t = ::verilator_utils::eval_scheduler;
        /// 调度器引用
        scheduler_t& scheduler;
        /// 目标评估阶段
        scheduler_t::eval_stage_enum eval_stage;
        /// 事件回调，用于判断事件是否触发
        ::verilator_utils::default_event_callback event_callback{};

        /**
         * @brief 构造可等待体
         *
         * @note 目标评估阶段需要可等待，否则断言失败
         * @param scheduler 调度器引用
         * @param eval_stage 目标评估阶段
         */
        explicit eval_stage_awaiter(scheduler_t& scheduler, scheduler_t::eval_stage_enum eval_stage) :
            scheduler{scheduler}, eval_stage{eval_stage}
        { ::verilator_utils::check{}(eval_stage != scheduler_t::eval_stage_enum::eval_end, "该评估阶段不可等待"sv); }

        /**
         * @brief 判断是否立即就绪
         *
         * @return 是否立即就绪
         */
        [[nodiscard]] bool await_ready() const noexcept { return scheduler.eval_stage() >= eval_stage; }

        /**
         * @brief 若未到目标阶段则挂起等待，将当前任务加入事件队列
         *
         * @tparam promise_type 协程承诺类型
         * @param handle 当前协程的句柄
         */
        template <::verilator_utils::is_coroutine_promise promise_type>
        void await_suspend(::std::coroutine_handle<promise_type> handle)
        {
            set_event_callback();
            scheduler.register_event(event_callback, handle);
        }

        // 绕过lld开启lto时崩溃的问题
        void set_event_callback()
        {
            event_callback = [this] noexcept { return await_ready(); };
        }
    };

    /**
     * @brief 实现协程栈回溯的可等待体
     *
     */
    struct stacktrace_awaiter : ::verilator_utils::detail::no_suspend_awaiter
    {
        ::std::source_location location{};
        ::verilator_utils::detail::coroutine_pair pair{};

        template <::verilator_utils::is_coroutine_promise promise_type>
        void set_promise_impl(promise_type& promise) noexcept
        { pair = promise; }

        [[nodiscard]] ::verilator_utils::coroutine_stacktrace await_resume() const
        {
            ::verilator_utils::coroutine_stacktrace stacktrace{pair};
            // 由于未被挂起，因此当前协程的location是空的，向其中注册当前的location
            if(!stacktrace.frames.empty()) { stacktrace.frames.front().location = location; }
            return stacktrace;
        }
    };
}  // namespace verilator_utils::detail

export namespace verilator_utils
{
    /**
     * @brief 在任务中获取协程柄
     *
     * @tparam promise_type 协程的承诺体类型，为void表示获取类型擦除的协程柄
     * @warning 谨慎操作promise中的字段
     * @return 可等待体
     * @code {.cpp}
     * task<void> foo()
     * {
     *     auto handle{co_await handle<typename task<void>::promise_type>()};
     * }
     * @endcode
     */
    template <typename promise_type = void>
        requires (::std::is_void_v<promise_type> || ::verilator_utils::is_coroutine_promise<promise_type>)
    [[nodiscard]] ::verilator_utils::detail::handle_awaiter<promise_type> handle() noexcept
    { return ::verilator_utils::detail::handle_awaiter<promise_type>{}; }

    /**
     * @brief 在任务中获取调度器引用
     *
     * @return 可等待体
     * @code {.cpp}
     * task foo()
     * {
     *     auto&& scheduler{co_await scheduler()};
     * }
     * @endcode
     */
    [[nodiscard]] ::verilator_utils::detail::scheduler_awaiter scheduler() noexcept
    { return ::verilator_utils::detail::scheduler_awaiter{}; }

    /**
     * @brief 在任务中结束仿真
     *
     * @note 会抛出eval_finish_exception异常来终止任务
     * @return 可等待体
     * @code {.cpp}
     * task foo()
     * {
     *     co_await eval_finish();
     * }
     * @endcode
     */
    [[nodiscard]] ::verilator_utils::detail::eval_finish_awaiter eval_finish() noexcept
    { return ::verilator_utils::detail::eval_finish_awaiter{}; }

    /**
     * @brief 在任务中获取仿真时间字符串
     *
     * @return 可等待体
     */
    [[nodiscard]] ::verilator_utils::detail::time_in_string_awaiter time_in_string() noexcept
    { return ::verilator_utils::detail::time_in_string_awaiter{}; }

    /**
     * @brief 在任务中获取仿真时间，单位为dut时间单位
     *
     * @return 可等待体
     */
    [[nodiscard]] ::verilator_utils::detail::time_in_time_unit_awaiter time_in_time_unit() noexcept
    { return ::verilator_utils::detail::time_in_time_unit_awaiter{}; }

    /**
     * @brief 在任务中获取仿真时间，单位为dut时间精度
     *
     * @return 可等待体
     */
    [[nodiscard]] ::verilator_utils::detail::time_in_time_precision_awaiter time_in_time_precision() noexcept
    { return ::verilator_utils::detail::time_in_time_precision_awaiter{}; }

    /**
     * @brief 在任务中进行协程栈回溯
     *
     * @return 可等待体
     */
    [[nodiscard]] ::verilator_utils::detail::stacktrace_awaiter
        stacktrace(::std::source_location location = ::std::source_location::current()) noexcept
    { return ::verilator_utils::detail::stacktrace_awaiter{.location = location}; }

    /**
     * @brief 等待指定时间
     *
     * @note 不支持delta延迟，等待时间不能为0
     * @param time_to_wait 等待时间，单位为时间精度，不能为0
     * @return 可等待体
     */
    [[nodiscard]] ::verilator_utils::detail::time_awaiter wait_time(::verilator_utils::femtosecond_t time_to_wait)
    { return ::verilator_utils::detail::time_awaiter{time_to_wait}; }

    /**
     * @brief 等待事件触发
     *
     * @tparam callback_t 事件回调类型
     * @param callback 事件回调函数
     * @return 可等待体
     */
    template <::verilator_utils::is_event_callback callback_t>
    [[nodiscard]] ::verilator_utils::detail::event_awaiter wait_event(callback_t&& callback)
    { return ::verilator_utils::detail::event_awaiter{::std::forward<callback_t>(callback)}; }

    /**
     * @brief 边沿类型
     *
     */
    enum class edge_enum : ::std::uint8_t
    {
        /// 上升沿
        rising = 1,
        /// 下降沿
        falling = 2,
        /// 双边沿
        both = rising | falling,
    };

    /**
     * @brief 边沿检测器
     *
     */
    struct edge_detector
    {
        using enum ::verilator_utils::edge_enum;

        /**
         * @brief 构造边沿检测器对象
         *
         * @param bit 要检测的信号
         * @param edge_to_detect 要检测的边沿
         */
        edge_detector(const ::verilator_utils::is_bit_slice auto& bit, ::verilator_utils::edge_enum edge_to_detect) :
            callback{[bit] { return static_cast<bool>(bit); }}, previous_value{static_cast<bool>(bit)},
            edge_to_detect_{edge_to_detect}
        {
        }

        /**
         * @brief 获取边沿检测结果
         *
         * @return 是否出现要检测的边沿
         */
        bool operator() ()
        {
            const bool current_value{callback()};
            const bool previous_value{::std::exchange(this->previous_value, current_value)};
            switch(edge_to_detect_)
            {
                case rising: return !previous_value && current_value;
                case falling: return previous_value && !current_value;
                case both: return previous_value != current_value;
                default: ::std::unreachable();  // edge_to_detect_越界属于未定义行为
            }
        }

        /**
         * @brief 获取要检测的边沿类型
         *
         * @return 要检测的边沿类型
         */
        [[nodiscard]] ::verilator_utils::edge_enum edge_to_detect() const noexcept { return edge_to_detect_; }

        /**
         * @brief 设置要检测的边沿类型
         *
         * @param edge_to_detect 要检测的边沿类型
         * @return 先前设置的边沿类型
         */
        void set_edge_to_detect(::verilator_utils::edge_enum edge_to_detect) noexcept
        {
            // 不校验枚举取值，传入edge_enum范围之外的值属于未定义行为
            edge_to_detect_ = edge_to_detect;
        }

    private:
        ::verilator_utils::default_event_callback callback;
        bool previous_value;
        ::verilator_utils::edge_enum edge_to_detect_;
    };

    /**
     * @brief 等待上升沿
     *
     * @param bit 时钟信号
     * @param edge_to_wait 要等待到边沿数量
     * @return event_awaiter 可等待体
     * @note 可用于编写参考模型，在测试激励中通常应该使用wait_verify
     */
    [[nodiscard]] ::verilator_utils::detail::event_awaiter wait_posedge(const ::verilator_utils::is_bit_slice auto& bit,
                                                                        ::std::size_t edge_to_wait = 1)
    {
        ::verilator_utils::check{}(edge_to_wait != 0, "要等待的边沿数量不能为0，实际为{}"sv, edge_to_wait);
        using edge_detector_t = ::verilator_utils::edge_detector;
        return ::verilator_utils::wait_event(
            [edge_detector = edge_detector_t{bit, edge_detector_t::rising}, edge_to_wait] mutable {
                edge_to_wait -= edge_detector();
                return edge_to_wait == 0;
            });
    }

    /**
     * @brief 等待下降沿
     *
     * @param bit 时钟信号
     * @param edge_to_wait 要等待到边沿数量
     * @return event_awaiter 可等待体
     * @note 可用于编写参考模型，在测试激励中通常应该使用wait_stimulate
     */
    [[nodiscard]] ::verilator_utils::detail::event_awaiter wait_negedge(const ::verilator_utils::is_bit_slice auto& bit,
                                                                        ::std::size_t edge_to_wait = 1)
    {
        ::verilator_utils::check{}(edge_to_wait != 0, "要等待的边沿数量不能为0，实际为{}"sv, edge_to_wait);
        using edge_detector_t = ::verilator_utils::edge_detector;
        return ::verilator_utils::wait_event(
            [edge_detector = edge_detector_t{bit, edge_detector_t::falling}, edge_to_wait] mutable {
                edge_to_wait -= edge_detector();
                return edge_to_wait == 0;
            });
    }

    /**
     * @brief 等待双边沿
     *
     * @param bit 时钟信号
     * @param edge_to_wait 要等待到边沿数量
     * @return event_awaiter 可等待体
     * @note 在测试激励中很少使用
     */
    [[nodiscard]] ::verilator_utils::detail::event_awaiter wait_alledge(const ::verilator_utils::is_bit_slice auto& bit,
                                                                        ::std::size_t edge_to_wait = 1)
    {
        ::verilator_utils::check{}(edge_to_wait != 0, "要等待的边沿数量不能为0，实际为{}"sv, edge_to_wait);
        using edge_detector_t = ::verilator_utils::edge_detector;
        return ::verilator_utils::wait_event([edge_detector = edge_detector_t{bit, edge_detector_t::both}, edge_to_wait] mutable {
            edge_to_wait -= edge_detector();
            return edge_to_wait == 0;
        });
    }

    /**
     * @brief 等待到指定评估阶段
     *
     * @param scheduler 调度器引用
     * @param eval_stage 评估阶段
     * @return 可等待体
     * @note 目标评估阶段需要可等待，否则断言失败
     * @note 若目标评估阶段可调度，则在目标阶段恢复执行，否则在目标阶段后最近的可调度阶段恢复执行
     * @code {.cpp}
     * task foo(eval_scheduler& scheduler)
     * {
     *     // 等待到电路评估完成，此时加入的激励在下一周期生效
     *     co_await wait_eval_stage(scheduler, eval_scheduler::eval_stage_enum::after_dut_eval);
     * }
     * @endcode
     */
    [[nodiscard]] ::verilator_utils::detail::eval_stage_awaiter
        wait_eval_stage(::verilator_utils::eval_scheduler& scheduler,
                        ::verilator_utils::eval_scheduler::eval_stage_enum eval_stage)
    { return ::verilator_utils::detail::eval_stage_awaiter{scheduler, eval_stage}; }
}  // namespace verilator_utils

namespace verilator_utils::detail
{
    /**
     * @brief 等待时钟边沿和评估阶段
     *
     * @param clk 时钟信号切片
     * @param edge_to_wait 要等待到边沿个数
     * @param edge 时钟沿极性
     * @param eval_stage 目标评估阶段
     * @return 同步任务
     * @note 等待到n个给定时钟边沿后的给定评估阶段
     */
    [[nodiscard]] ::verilator_utils::task<void>
        wait_edge_and_eval_stage(const ::verilator_utils::bit_slice<::CData>& clk,
                                 ::std::size_t edge_to_wait,
                                 ::verilator_utils::edge_enum edge,
                                 ::verilator_utils::eval_scheduler::eval_stage_enum eval_stage)
    {
        if(edge == ::verilator_utils::edge_enum::rising) { co_await ::verilator_utils::wait_posedge(clk, edge_to_wait); }
        else
        {
            co_await ::verilator_utils::wait_negedge(clk, edge_to_wait);
        }
        co_await ::verilator_utils::wait_eval_stage(co_await scheduler(), eval_stage);
    }
}  // namespace verilator_utils::detail

export namespace verilator_utils
{

    /**
     * @brief 等待到验证时机
     *
     * @param clk 时钟信号切片
     * @param edge_to_wait 要等待到边沿个数
     * @param edge 时钟沿极性
     * @param eval_stage 目标评估阶段
     * @return 同步任务
     * @note 等待到edge_to_wait个给定时钟边沿后的给定评估阶段
     * @note 默认为时钟上升沿且电路 **评估完成后 ** 进行验证
     */
    [[nodiscard]] ::verilator_utils::task<void>
        wait_verify(const ::verilator_utils::bit_slice<::CData>& clk,
                    ::std::size_t edge_to_wait = 1,
                    ::verilator_utils::edge_enum edge = ::verilator_utils::edge_enum::rising,
                    ::verilator_utils::eval_scheduler::eval_stage_enum eval_stage =
                        ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval)
    {
        ::verilator_utils::check{}(edge != ::verilator_utils::edge_enum::both, "不支持等待双边沿"sv);
        return ::verilator_utils::detail::wait_edge_and_eval_stage(clk, edge_to_wait, edge, eval_stage);
    }

    /**
     * @brief 等待到激励时机
     *
     * @param clk 时钟信号切片
     * @param edge_to_wait 要等待到边沿个数
     * @param edge 时钟沿极性
     * @param eval_stage 目标评估阶段，默认根据时钟沿极性选择目标阶段
     * @return 同步任务
     * @note 等待到edge_to_wait个给定时钟边沿前的给定评估阶段
     * @note 默认为时钟下降沿触发时在电路 ** 评估完成前 ** 进行激励，时钟上升沿触发时在电路 ** 评估完成后 ** 进行激励
     */
    [[nodiscard]] ::verilator_utils::task<void>
        wait_stimulate(const ::verilator_utils::bit_slice<::CData>& clk,
                       ::std::size_t edge_to_wait = 1,
                       ::verilator_utils::edge_enum edge = ::verilator_utils::edge_enum::falling,
                       ::std::optional<::verilator_utils::eval_scheduler::eval_stage_enum> eval_stage = ::std::nullopt)
    {
        ::verilator_utils::check{}(edge != ::verilator_utils::edge_enum::both, "不支持等待双边沿"sv);
        const auto default_stage{edge == ::verilator_utils::edge_enum::rising
                                     ? ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval
                                     : ::verilator_utils::eval_scheduler::eval_stage_enum::before_dut_eval};
        return ::verilator_utils::detail::wait_edge_and_eval_stage(clk, edge_to_wait, edge, eval_stage.value_or(default_stage));
    }

    /**
     * @brief 等待到验证时机并执行验证回调
     *
     * @param env 事件信号切片，应当为脉冲信号
     * @param verify_callback 执行验证的回调函数
     * @param edge_to_wait 要等待到边沿个数
     * @param edge 边沿极性
     * @param eval_stage 目标评估阶段
     * @return 同步任务
     * @note 等待到edge_to_wait个给定边沿后的给定评估阶段
     * @note 默认为上升沿且电路 **评估完成后 ** 进行验证
     */
    [[nodiscard]] ::verilator_utils::task<void>
        verify_at(const ::verilator_utils::bit_slice<::CData>& env,
                  ::std::function<void()> verify_callback,
                  ::std::size_t edge_to_wait = 1,
                  ::verilator_utils::edge_enum edge = ::verilator_utils::edge_enum::rising,
                  ::verilator_utils::eval_scheduler::eval_stage_enum eval_stage =
                      ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval)
    {
        co_await ::verilator_utils::wait_verify(env, edge_to_wait, edge, eval_stage);
        auto eval_time{co_await ::verilator_utils::time_in_string()};
        CAPTURE(eval_time);
        verify_callback();
    }

    /**
     * @brief 等待到验证时机并执行验证回调
     *
     * @param clk 时钟信号切片
     * @param event_callback 事件回调函数
     * @param verify_callback 执行验证的回调函数
     * @param edge 边沿极性
     * @param eval_stage 目标评估阶段
     * @return 同步任务
     * @note 等待直到event_callback为true，至少等待一个时钟沿
     * @note 默认为上升沿且电路 **评估完成后 ** 进行验证
     */
    [[nodiscard]] ::verilator_utils::task<void>
        verify_at(const ::verilator_utils::bit_slice<::CData>& clk,
                  ::verilator_utils::default_event_callback event_callback,
                  ::std::function<void()> verify_callback,
                  ::verilator_utils::edge_enum edge = ::verilator_utils::edge_enum::rising,
                  ::verilator_utils::eval_scheduler::eval_stage_enum eval_stage =
                      ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval)
    {
        // 总是等待1周期，避免event为电平信号时，多个verify_at调用在同一时钟周期触发
        co_await ::verilator_utils::wait_verify(clk, 1, edge, eval_stage);
        while(!event_callback()) { co_await ::verilator_utils::wait_verify(clk, 1, edge, eval_stage); }
        auto eval_time{co_await ::verilator_utils::time_in_string()};
        CAPTURE(eval_time);
        verify_callback();
    }

    /**
     * @brief 生成时钟信号
     *
     * @param scheduler 调度器引用
     * @param clk 时钟信号切片
     * @param period 时钟周期
     * @param delay 时钟发生延迟
     * @param duty_ratio 占空比，取值范围为(0, 1)
     * @return 生成时钟信号的任务
     */
    [[nodiscard]] ::verilator_utils::task<void> generate_clock(::verilator_utils::bit_slice<::CData>& clk,
                                                               ::verilator_utils::femtosecond_t period,
                                                               ::verilator_utils::femtosecond_t delay = 0_fs,
                                                               double duty_ratio = 0.5)
    {
        ::verilator_utils::check{}(duty_ratio > 0. && duty_ratio < 1., "时钟占空比{}超出取值范围(0, 1)"sv, duty_ratio);
        clk = 0;
        if(delay != 0_fs) { co_await ::verilator_utils::wait_time(delay); }
        const auto positive_duration{period * duty_ratio};
        const auto negative_duration{period - positive_duration};
        while(true)
        {
            clk = 0;
            co_await ::verilator_utils::wait_time(negative_duration);
            clk = 1;
            co_await ::verilator_utils::wait_time(positive_duration);
        }
    }

    /**
     * @brief 生成复位信号，持续cycle个下降沿
     *
     * @param reset 复位信号引用
     * @param clk 时钟信号引用
     * @param cycle 复位信号持续的下降沿数
     * @param active_high 复位信号的极性，true表示高电平有效，false表示低电平有效
     * @return 生成复位信号的任务
     */
    [[nodiscard]] ::verilator_utils::task<void> generate_reset(::verilator_utils::bit_slice<::CData>& reset,
                                                               const ::verilator_utils::bit_slice<::CData>& clk,
                                                               ::size_t cycle = 3,
                                                               bool active_high = true)
    {
        reset = static_cast<::std::uint64_t>(active_high);
        co_await ::verilator_utils::wait_stimulate(clk, cycle);
        reset = static_cast<::std::uint64_t>(!active_high);
    }

    /**
     * @brief 生成异步复位信号，持续duration时间
     *
     * @param reset 复位信号引用
     * @param duration 持续时间
     * @param active_high 复位信号的极性，true表示高电平有效，false表示低电平有效
     * @return 生成复位信号的任务
     */
    [[nodiscard]] ::verilator_utils::task<void> generate_async_reset(::verilator_utils::bit_slice<::CData>& reset,
                                                                     ::verilator_utils::femtosecond_t duration,
                                                                     bool active_high = true)
    {
        reset = static_cast<::std::uint64_t>(active_high);
        co_await ::verilator_utils::wait_time(duration);
        reset = static_cast<::std::uint64_t>(!active_high);
    }

    /**
     * @brief 设置最大仿真时间
     * 由于generate_clock会不断生成时钟激励，因此需要手动通过eval_finish结束仿真，否则仿真会无限执行。
     * 该函数会在到达最大仿真时间时自动通过eval_finish结束仿真。
     * @param duration 最大仿真时间
     * @return 子任务，通常应通过add_task放入调度器中
     * @code {.cpp}
     * dut_context<dut_t, void> ctx;
     * // 添加时钟激励
     * ctx.add_task(generate_clock(2_ns));
     * // 设置最大仿真时间为100ns，避免因时钟激励导致仿真无限执行
     * ctx.add_task(max_eval_time(100_ns));
     * @endcode
     */
    [[nodiscard]] ::verilator_utils::task<void> max_eval_time(::verilator_utils::femtosecond_t duration)
    {
        co_await ::verilator_utils::wait_time(duration);
        auto&& scheduler{co_await ::verilator_utils::scheduler()};
        scheduler.error();
        scheduler.finish();
        throw ::verilator_utils::eval_timeout_exception{};
    }

    /**
     * @brief 等待直到复位完成
     *
     * @param scheduler 调度器引用
     * @param rst 复位信号切片
     * @param active_high 复位信号的极性，true表示高电平有效，false表示低电平有效
     * @return 同步任务
     * @note 等待到复位信号无效且初始评估完成
     */
    [[nodiscard]] ::verilator_utils::task<void> wait_reset_finish(const ::verilator_utils::bit_slice<::CData>& rst,
                                                                  bool active_high = true)
    {
        co_await ::verilator_utils::wait_event([rst, active_high] { return rst != active_high; });
    }

    /**
     * @brief 检测一组时钟是否触发，用于参考模型中
     *
     */
    struct select_clock
    {

    private:
        struct clock_trigger
        {
            ::verilator_utils::edge_detector edge_detector;
            bool triggered{};

            /**
             * @brief 进行边沿检测并缓存结果
             *
             * @return 是否检测到指定边沿
             */
            bool detect()
            {
                triggered = edge_detector();
                return triggered;
            }
        };

        ::std::vector<clock_trigger> clk_list{};

        struct select_clock_awaiter
        {
            ::std::vector<clock_trigger>& clk_list;
            /// 事件回调，用于轮询检测时钟边沿
            ::verilator_utils::default_event_callback event_callback{};

            bool await_ready()
            {
                bool triggered{};
                ::std::ranges::for_each(clk_list, [&triggered](clock_trigger& clk) { triggered |= clk.detect(); });
                return triggered;
            }

            template <::verilator_utils::is_coroutine_promise promise_type>
            void await_suspend(::std::coroutine_handle<promise_type> handle)
            {
                event_callback = [this] { return await_ready(); };
                auto scheduler{handle.promise().check_scheduler()};
                scheduler->register_event(event_callback, handle);
            }

            [[nodiscard]] auto await_resume() const
            {
                return clk_list | ::std::views::transform([](const clock_trigger& clk) { return clk.triggered; });
            }
        };

    public:
        /**
         * @brief 添加要检测的时钟
         *
         * @param clk 时钟信号
         * @param edge_to_detect 要检测的边沿
         */
        void add_clock(const ::verilator_utils::bit_slice<::CData>& clk,
                       ::verilator_utils::edge_enum edge_to_detect = ::verilator_utils::edge_enum::rising)
        { clk_list.emplace_back(::verilator_utils::edge_detector{clk, edge_to_detect}); }

        /**
         * @brief 等待某个边沿触发
         *
         * @return 可等待体
         */
        [[nodiscard]] friend select_clock_awaiter operator co_await(select_clock& self)
        { return select_clock_awaiter{self.clk_list}; }
    };
}  // namespace verilator_utils
