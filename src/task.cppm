module;
#include <doctest_macros.hpp>
export module verilator_utils:task;
import :scheduler;

export namespace verilator_utils::detail
{
    /**
     * @brief 实现无挂起协程柄获取的可等待体
     *
     */
    template <typename promise_type>
    struct get_handle_awaiter : ::verilator_utils::detail::no_suspend_awaiter
    {
        using handle_t = ::std::coroutine_handle<promise_type>;
        handle_t handle{};

        void set_handle_impl(handle_t handle) noexcept { this->handle = handle; }

        [[nodiscard]] handle_t await_resume() const { return handle; }
    };

    /**
     * @brief 实现无挂起调度器引用获取的可等待体
     *
     */
    struct get_scheduler_awaiter : ::verilator_utils::detail::no_suspend_awaiter
    {
        ::verilator_utils::eval_scheduler* scheduler;

        template <::verilator_utils::is_coroutine_promise promise_type>
        void set_handle_impl(::std::coroutine_handle<promise_type> handle)
        { scheduler = handle.promise().check_scheduler(); }

        [[nodiscard]] ::verilator_utils::eval_scheduler& await_resume() const { return *scheduler; }
    };

    /**
     * @brief 实现结束仿真的可等待体
     *
     */
    struct eval_finish_awaiter : ::verilator_utils::detail::no_suspend_awaiter
    {
        template <::verilator_utils::is_coroutine_promise promise_type>
        static void set_handle_impl(::std::coroutine_handle<promise_type> handle)
        { handle.promise().check_scheduler()->finish(); }

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
    struct get_time_in_string_awaiter : ::verilator_utils::detail::no_suspend_awaiter
    {
        ::verilator_utils::eval_scheduler* scheduler;

        template <::verilator_utils::is_coroutine_promise promise_type>
        void set_handle_impl(::std::coroutine_handle<promise_type> handle)
        { scheduler = handle.promise().check_scheduler(); }

        [[nodiscard]] ::std::string await_resume() const { return scheduler->time_in_string(); }
    };

    /**
     * @brief 实现仿真时间获取的可等待体
     *
     */
    struct get_time_in_time_unit_awaiter
    {
        ::verilator_utils::eval_scheduler* scheduler;

        template <::verilator_utils::is_coroutine_promise promise_type>
        void set_handle_impl(::std::coroutine_handle<promise_type> handle)
        { scheduler = handle.promise().check_scheduler(); }

        [[nodiscard]] double await_resume() const { return scheduler->time_in_time_unit(); }
    };

    /**
     * @brief 实现仿真时间获取的可等待体
     *
     */
    struct get_time_in_time_precision_awaiter
    {
        ::verilator_utils::eval_scheduler* scheduler;

        template <::verilator_utils::is_coroutine_promise promise_type>
        void set_handle_impl(::std::coroutine_handle<promise_type> handle)
        { scheduler = handle.promise().check_scheduler(); }

        [[nodiscard]] ::std::uint64_t await_resume() const { return scheduler->time_in_time_precision(); }
    };

    /**
     * @brief 实现延迟功能的可等待体
     *
     */
    struct time_awaiter

    {
        /// 等待时间，单位为飞秒
        ::verilator_utils::femtosecond_t time_to_wait;
        /// 调度器指针，自动绑定
        ::verilator_utils::eval_scheduler* scheduler{};

        explicit time_awaiter(::verilator_utils::femtosecond_t time_to_wait) noexcept : time_to_wait{time_to_wait} {}

        /**
         * @brief 判断是否立即就绪
         *
         * @return false 不支持delta延迟，永远不会立即就绪
         */
        static bool await_ready() noexcept { return false; }

        /**
         * @brief 挂起等待，将当前任务加入等待队列
         *
         * @tparam promise_type 协程承诺类型
         * @param handle 当前协程的句柄
         */
        template <::verilator_utils::is_coroutine_promise promise_type>
        void await_suspend(::std::coroutine_handle<promise_type> handle)
        {
            scheduler = handle.promise().check_scheduler();
            scheduler->register_wait(time_to_wait, handle);
        }

        /**
         * @brief 恢复等待任务的执行
         *
         * @throws eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
         */
        void await_resume() const { scheduler->throw_if_finish(); }
    };

    /**
     * @brief 实现事件触发功能的可等待体
     *
     */
    struct event_awaiter
    {
        /// 事件回调，用于判断事件是否触发
        ::verilator_utils::default_event_callback event_callback;
        /// 调度器指针，自动绑定
        ::verilator_utils::eval_scheduler* scheduler{};

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
            scheduler = handle.promise().check_scheduler();
            scheduler->register_event(event_callback, handle);
        }

        /**
         * @brief 恢复等待任务的执行
         *
         * @throws eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
         */
        void await_resume() const
        {
            if(scheduler != nullptr) { scheduler->throw_if_finish(); }
        }
    };

    /**
     * @brief 实现评估阶段触发功能的可等待体
     *
     */
    struct eval_stage_awaiter : ::verilator_utils::detail::no_suspend_awaiter
    {
        using scheduler_t = ::verilator_utils::eval_scheduler;
        /// 目标评估阶段
        scheduler_t::eval_stage_enum eval_stage;
        /// 调度器指针，自动绑定
        scheduler_t* scheduler{};
        /// 事件回调，用于判断事件是否触发
        ::verilator_utils::default_event_callback event_callback{};

        /**
         * @brief 构造可等待体
         *
         * @note 目标评估阶段需要可等待，否则断言失败
         * @param eval_stage 目标评估阶段
         */
        explicit eval_stage_awaiter(scheduler_t::eval_stage_enum eval_stage) : eval_stage{eval_stage}
        {
            using namespace std::string_view_literals;
            REQUIRE_MESSAGE(eval_stage != scheduler_t::eval_stage_enum::eval_end, "该评估阶段不可等待"sv);
        }

        /**
         * @brief 根据协程柄初始化字段
         *
         * @param handle 协程柄
         */
        void set_handle_impl(auto handle)
        {
            scheduler = handle.promise().check_scheduler();
            event_callback = [this] { return scheduler->get_eval_stage() >= eval_stage; };
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
        { scheduler->register_event(event_callback, handle); }

        /**
         * @brief 恢复等待任务的执行
         *
         * @throws eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
         */
        void await_resume() const { scheduler->throw_if_finish(); }
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
     *     auto handle{co_await get_handle<typename task<void>::promise_type>()};
     * }
     * @endcode
     */
    template <typename promise_type = void>
        requires (::std::is_void_v<promise_type> || ::verilator_utils::is_coroutine_promise<promise_type>)
    [[nodiscard]] ::verilator_utils::detail::get_handle_awaiter<promise_type> get_handle() noexcept
    { return ::verilator_utils::detail::get_handle_awaiter<promise_type>{}; }

    /**
     * @brief 在任务中获取调度器引用
     *
     * @return 可等待体
     * @code {.cpp}
     * task foo()
     * {
     *     auto&& scheduler{co_await get_scheduler()};
     * }
     * @endcode
     */
    [[nodiscard]] ::verilator_utils::detail::get_scheduler_awaiter get_scheduler() noexcept
    { return ::verilator_utils::detail::get_scheduler_awaiter{}; }

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
    [[nodiscard]] ::verilator_utils::detail::get_time_in_string_awaiter get_time_in_string() noexcept
    { return ::verilator_utils::detail::get_time_in_string_awaiter{}; }

    /**
     * @brief 在任务中获取仿真时间，单位为dut时间单位
     *
     * @return 可等待体
     */
    [[nodiscard]] ::verilator_utils::detail::get_time_in_time_unit_awaiter get_time_in_time_unit() noexcept
    { return ::verilator_utils::detail::get_time_in_time_unit_awaiter{}; }

    /**
     * @brief 在任务中获取仿真时间，单位为dut时间精度
     *
     * @return 可等待体
     */
    [[nodiscard]] ::verilator_utils::detail::get_time_in_time_precision_awaiter get_time_in_time_precision() noexcept
    { return ::verilator_utils::detail::get_time_in_time_precision_awaiter{}; }

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
        REQUIRE_NE(edge_to_wait, 0);
        using edge_detector_t = ::verilator_utils::edge_detector;
        return ::verilator_utils::wait_event(
            [edge_detector = edge_detector_t{bit, edge_detector_t::rising}, edge_to_wait] mutable
            {
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
        REQUIRE_NE(edge_to_wait, 0);
        using edge_detector_t = ::verilator_utils::edge_detector;
        return ::verilator_utils::wait_event(
            [edge_detector = edge_detector_t{bit, edge_detector_t::falling}, edge_to_wait] mutable
            {
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
        REQUIRE_NE(edge_to_wait, 0);
        using edge_detector_t = ::verilator_utils::edge_detector;
        return ::verilator_utils::wait_event(
            [edge_detector = edge_detector_t{bit, edge_detector_t::both}, edge_to_wait] mutable
            {
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
     *     co_await wait_eval_stage(eval_scheduler::eval_stage_enum::after_dut_eval);
     * }
     * @endcode
     */
    [[nodiscard]] ::verilator_utils::detail::eval_stage_awaiter
        wait_eval_stage(::verilator_utils::eval_scheduler::eval_stage_enum eval_stage)
    { return ::verilator_utils::detail::eval_stage_awaiter{eval_stage}; }
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
        wait_edge_and_eval_stage(::verilator_utils::bit_slice<::CData> clk,
                                 ::std::size_t edge_to_wait,
                                 ::verilator_utils::edge_enum edge,
                                 ::verilator_utils::eval_scheduler::eval_stage_enum eval_stage)
    {
        if(edge == ::verilator_utils::edge_enum::rising) { co_await ::verilator_utils::wait_posedge(clk, edge_to_wait); }
        else
        {
            co_await ::verilator_utils::wait_negedge(clk, edge_to_wait);
        }
        co_await ::verilator_utils::wait_eval_stage(eval_stage);
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
        REQUIRE_NE(edge, ::verilator_utils::edge_enum::both);
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
                       ::verilator_utils::eval_scheduler::eval_stage_enum eval_stage =
                           ::verilator_utils::eval_scheduler::eval_stage_enum::invalid)
    {
        REQUIRE_NE(edge, ::verilator_utils::edge_enum::both);

        if(eval_stage == ::verilator_utils::eval_scheduler::eval_stage_enum::invalid)
        {
            eval_stage = edge == ::verilator_utils::edge_enum::rising
                             ? ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval
                             : ::verilator_utils::eval_scheduler::eval_stage_enum::before_dut_eval;
        }
        return ::verilator_utils::detail::wait_edge_and_eval_stage(clk, edge_to_wait, edge, eval_stage);
    }

    /**
     * @brief 生成时钟信号
     *
     * @param scheduler 调度器引用
     * @param clk 时钟信号切片
     * @param period 时钟周期
     * @param delay 时钟发生延迟
     * @param duty_ratio 占空比
     * @return 生成时钟信号的任务
     */
    [[nodiscard]] ::verilator_utils::task<void> generate_clock(::verilator_utils::bit_slice<::CData>& clk,
                                                               ::verilator_utils::femtosecond_t period,
                                                               ::verilator_utils::femtosecond_t delay = 0_fs,
                                                               double duty_ratio = 0.5)
    {
        clk = 0;
        if(delay != 0_fs) { co_await ::verilator_utils::wait_time(delay); }
        auto positive_duration{period * duty_ratio};
        auto negative_duration{period - positive_duration};
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
                                                               ::verilator_utils::bit_slice<::CData>& clk,
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
        auto&& scheduler{co_await get_scheduler()};
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
    [[nodiscard]] ::verilator_utils::task<void> wait_reset_finish(::verilator_utils::bit_slice<::CData> rst,
                                                                  bool active_high = true)
    {
        co_await ::verilator_utils::wait_event([rst, active_high] { return rst != active_high; });
    }

    namespace detail
    {
        /// 3~64位LFSR m序列反馈系数表
        constexpr ::std::array lfsr_feedback_mask_table{::std::to_array({
            //  3:  x^3 + x^2 + 1
            (1zu << 2zu) | (1zu << 0zu),
            //  4:  x^4 + x^3 + 1
            (1zu << 3zu) | (1zu << 0zu),
            //  5:  x^5 + x^3 + 1
            (1zu << 3zu) | (1zu << 0zu),
            //  6:  x^6 + x^5 + 1
            (1zu << 5zu) | (1zu << 0zu),
            //  7:  x^7 + x^6 + 1
            (1zu << 6zu) | (1zu << 0zu),
            //  8:  x^8 + x^6 + x^5 + x^4 + 1
            (1zu << 6zu) | (1zu << 5zu) | (1zu << 4zu) | (1zu << 0zu),
            //  9:  x^9 + x^5 + 1
            (1zu << 5zu) | (1zu << 0zu),
            // 10:  x^10 + x^7 + 1
            (1zu << 7zu) | (1zu << 0zu),
            // 11:  x^11 + x^9 + 1
            (1zu << 9zu) | (1zu << 0zu),
            // 12:  x^12 + x^11 + x^10 + x^4 + 1
            (1zu << 11zu) | (1zu << 10zu) | (1zu << 4zu) | (1zu << 0zu),
            // 13:  x^13 + x^12 + x^11 + x^8 + 1
            (1zu << 12zu) | (1zu << 11zu) | (1zu << 8zu) | (1zu << 0zu),
            // 14:  x^14 + x^13 + x^12 + x^2 + 1
            (1zu << 13zu) | (1zu << 12zu) | (1zu << 2zu) | (1zu << 0zu),
            // 15:  x^15 + x^14 + 1
            (1zu << 14zu) | (1zu << 0zu),
            // 16:  x^16 + x^14 + x^13 + x^11 + 1
            (1zu << 14zu) | (1zu << 13zu) | (1zu << 11zu) | (1zu << 0zu),
            // 17:  x^17 + x^14 + 1
            (1zu << 14zu) | (1zu << 0zu),
            // 18:  x^18 + x^11 + 1
            (1zu << 11zu) | (1zu << 0zu),
            // 19:  x^19 + x^18 + x^17 + x^14 + 1
            (1zu << 18zu) | (1zu << 17zu) | (1zu << 14zu) | (1zu << 0zu),
            // 20:  x^20 + x^17 + 1
            (1zu << 17zu) | (1zu << 0zu),
            // 21:  x^21 + x^19 + 1
            (1zu << 19zu) | (1zu << 0zu),
            // 22:  x^22 + x^21 + 1
            (1zu << 21zu) | (1zu << 0zu),
            // 23:  x^23 + x^18 + 1
            (1zu << 18zu) | (1zu << 0zu),
            // 24:  x^24 + x^23 + x^22 + x^17 + 1
            (1zu << 23zu) | (1zu << 22zu) | (1zu << 17zu) | (1zu << 0zu),
            // 25:  x^25 + x^22 + 1
            (1zu << 22zu) | (1zu << 0zu),
            // 26:  x^26 + x^6 + x^2 + x + 1
            (1zu << 6zu) | (1zu << 2zu) | (1zu << 1zu) | (1zu << 0zu),
            // 27:  x^27 + x^5 + x^2 + x + 1
            (1zu << 5zu) | (1zu << 2zu) | (1zu << 1zu) | (1zu << 0zu),
            // 28:  x^28 + x^25 + 1
            (1zu << 25zu) | (1zu << 0zu),
            // 29:  x^29 + x^27 + 1
            (1zu << 27zu) | (1zu << 0zu),
            // 30:  x^30 + x^6 + x^4 + x + 1
            (1zu << 6zu) | (1zu << 4zu) | (1zu << 1zu) | (1zu << 0zu),
            // 31:  x^31 + x^28 + 1
            (1zu << 28zu) | (1zu << 0zu),
            // 32:  x^32 + x^22 + x^2 + x + 1
            (1zu << 22zu) | (1zu << 2zu) | (1zu << 1zu) | (1zu << 0zu),
            // 33:  x^33 + x^20 + 1
            (1zu << 20zu) | (1zu << 0zu),
            // 34:  x^34 + x^27 + x^2 + x + 1
            (1zu << 27zu) | (1zu << 2zu) | (1zu << 1zu) | (1zu << 0zu),
            // 35:  x^35 + x^33 + 1
            (1zu << 33zu) | (1zu << 0zu),
            // 36:  x^36 + x^25 + 1
            (1zu << 25zu) | (1zu << 0zu),
            // 37:  x^37 + x^5 + x^4 + x^3 + x^2 + x + 1
            (1zu << 5zu) | (1zu << 4zu) | (1zu << 3zu) | (1zu << 2zu) | (1zu << 1zu) | (1zu << 0zu),
            // 38:  x^38 + x^6 + x^5 + x + 1
            (1zu << 6zu) | (1zu << 5zu) | (1zu << 1zu) | (1zu << 0zu),
            // 39:  x^39 + x^35 + 1
            (1zu << 35zu) | (1zu << 0zu),
            // 40:  x^40 + x^38 + x^21 + x^19 + 1
            (1zu << 38zu) | (1zu << 21zu) | (1zu << 19zu) | (1zu << 0zu),
            // 41:  x^41 + x^38 + 1
            (1zu << 38zu) | (1zu << 0zu),
            // 42:  x^42 + x^41 + x^20 + x^19 + 1
            (1zu << 41zu) | (1zu << 20zu) | (1zu << 19zu) | (1zu << 0zu),
            // 43:  x^43 + x^42 + x^38 + x^37 + 1
            (1zu << 42zu) | (1zu << 38zu) | (1zu << 37zu) | (1zu << 0zu),
            // 44:  x^44 + x^43 + x^18 + x^17 + 1
            (1zu << 43zu) | (1zu << 18zu) | (1zu << 17zu) | (1zu << 0zu),
            // 45:  x^45 + x^44 + x^42 + x^41 + 1
            (1zu << 44zu) | (1zu << 42zu) | (1zu << 41zu) | (1zu << 0zu),
            // 46:  x^46 + x^45 + x^26 + x^25 + 1
            (1zu << 45zu) | (1zu << 26zu) | (1zu << 25zu) | (1zu << 0zu),
            // 47:  x^47 + x^42 + 1
            (1zu << 42zu) | (1zu << 0zu),
            // 48:  x^48 + x^47 + x^21 + x^20 + 1
            (1zu << 47zu) | (1zu << 21zu) | (1zu << 20zu) | (1zu << 0zu),
            // 49:  x^49 + x^40 + 1
            (1zu << 40zu) | (1zu << 0zu),
            // 50:  x^50 + x^49 + x^24 + x^23 + 1
            (1zu << 49zu) | (1zu << 24zu) | (1zu << 23zu) | (1zu << 0zu),
            // 51:  x^51 + x^50 + x^36 + x^35 + 1
            (1zu << 50zu) | (1zu << 36zu) | (1zu << 35zu) | (1zu << 0zu),
            // 52:  x^52 + x^49 + 1
            (1zu << 49zu) | (1zu << 0zu),
            // 53:  x^53 + x^52 + x^38 + x^37 + 1
            (1zu << 52zu) | (1zu << 38zu) | (1zu << 37zu) | (1zu << 0zu),
            // 54:  x^54 + x^53 + x^18 + x^17 + 1
            (1zu << 53zu) | (1zu << 18zu) | (1zu << 17zu) | (1zu << 0zu),
            // 55:  x^55 + x^31 + 1
            (1zu << 31zu) | (1zu << 0zu),
            // 56:  x^56 + x^55 + x^35 + x^34 + 1
            (1zu << 55zu) | (1zu << 35zu) | (1zu << 34zu) | (1zu << 0zu),
            // 57:  x^57 + x^50 + 1
            (1zu << 50zu) | (1zu << 0zu),
            // 58:  x^58 + x^39 + 1
            (1zu << 39zu) | (1zu << 0zu),
            // 59:  x^59 + x^58 + x^38 + x^37 + 1
            (1zu << 58zu) | (1zu << 38zu) | (1zu << 37zu) | (1zu << 0zu),
            // 60:  x^60 + x^59 + 1
            (1zu << 59zu) | (1zu << 0zu),
            // 61:  x^61 + x^60 + x^46 + x^45 + 1
            (1zu << 60zu) | (1zu << 46zu) | (1zu << 45zu) | (1zu << 0zu),
            // 62:  x^62 + x^61 + x^6 + x^5 + 1
            (1zu << 61zu) | (1zu << 6zu) | (1zu << 5zu) | (1zu << 0zu),
            // 63:  x^63 + x^62 + 1
            (1zu << 62zu) | (1zu << 0zu),
            // 64:  x^64 + x^63 + x^61 + x^60 + 1
            (1zu << 63zu) | (1zu << 61zu) | (1zu << 60zu) | (1zu << 0zu),
        })};

        /**
         * @brief 检查LFSR生成器参数是否合法
         *
         * @param width LFSR宽度
         * @param feedback_mask 反馈表达式
         * @param initial_value LFSR初始值
         */
        void check_lfsr_generator_args(::std::size_t width, ::std::uint64_t& feedback_mask, ::std::uint64_t initial_value)
        {
            using namespace ::std::string_view_literals;
            REQUIRE_GE(width, 3);
            REQUIRE_LE(width, 64);
            REQUIRE_MESSAGE(initial_value != 0, "初始值为0时LFSR输出恒为0"sv);
            if(width != 64)
            {
                REQUIRE_MESSAGE((feedback_mask >> width) == 0, "反馈表达式宽度不应超过LFSR宽度"sv);
                REQUIRE_MESSAGE((initial_value >> width) == 0, "初始值宽度不应超过LFSR宽度"sv);
            }

            if(feedback_mask == 0) { feedback_mask = ::verilator_utils::detail::lfsr_feedback_mask_table[width - 3]; }
            REQUIRE_MESSAGE((feedback_mask & 1zu) != 0, "反馈表达式必须包含常数项"sv);
        }
    }  // namespace detail

    /**
     * @brief 斐波那契型LFSR生成器，输出序列长度无限
     *
     * @param width LFSR宽度，取值范围为[3, 64]
     * @param feedback_mask 反馈表达式，省略最高次项但包含常数项，为0表示使用m序列对应的反馈表达式
     * @param initial_value LFSR初始值
     * @return 生成器
     */
    [[nodiscard]] ::verilator_utils::generator<bool>
        fibonacci_lfsr_generator(::std::size_t width, ::std::uint64_t feedback_mask = 0, ::std::uint64_t initial_value = 1)
    {
        ::verilator_utils::detail::check_lfsr_generator_args(width, feedback_mask, initial_value);
        ::std::uint64_t value{initial_value};
        while(true)
        {
            co_yield static_cast<bool>(value & 1zu);
            auto feedback_value{static_cast<::std::uint64_t>(::std::popcount(value & feedback_mask)) % 2};
            value = value >> 1zu | feedback_value << (width - 1);
        }
    }

    /**
     * @brief 伽罗瓦型LFSR生成器，输出序列长度无限
     *
     * @param width LFSR宽度，取值范围为[3, 64]
     * @param feedback_mask 反馈表达式，省略最高次项但包含常数项，为0表示使用m序列对应的反馈表达式
     * @param initial_value LFSR初始值
     * @return 生成器
     */
    [[nodiscard]] ::verilator_utils::generator<bool>
        galois_lfsr_generator(::std::size_t width, ::std::uint64_t feedback_mask = 0, ::std::uint64_t initial_value = 1)
    {
        ::verilator_utils::detail::check_lfsr_generator_args(width, feedback_mask, initial_value);
        ::std::uint64_t value{initial_value};
        while(true)
        {
            auto out{value & 1zu};
            co_yield static_cast<bool>(out);
            auto masked_broadcast_out{(0zu - out) & feedback_mask};
            value = (value ^ masked_broadcast_out) >> 1zu | out << (width - 1);
        }
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
            ::verilator_utils::eval_scheduler* scheduler{};
            /// 事件回调，用于轮询检测时钟边沿
            ::verilator_utils::default_event_callback event_callback{[this] { return await_ready(); }};

            bool await_ready()
            {
                bool triggered{};
                ::std::ranges::for_each(clk_list, [&triggered](clock_trigger& clk) { triggered |= clk.detect(); });
                return triggered;
            }

            template <::verilator_utils::is_coroutine_promise promise_type>
            void await_suspend(::std::coroutine_handle<promise_type> handle)
            {
                scheduler = handle.promise().check_scheduler();
                scheduler->register_event(event_callback, handle);
            }

            [[nodiscard]] auto await_resume() const
            {
                if(scheduler != nullptr) { scheduler->throw_if_finish(); }
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
        void add_clock(const ::verilator_utils::bit_slice<::CData>& clk, ::verilator_utils::edge_enum edge_to_detect)
        { clk_list.emplace_back(::verilator_utils::edge_detector{clk, edge_to_detect}); }

        /**
         * @brief 等待某个边沿触发
         *
         * @return 可等待体
         */
        [[nodiscard]] select_clock_awaiter operator co_await() { return select_clock_awaiter{clk_list}; }
    };
}  // namespace verilator_utils

export namespace verilator_utils
{
    /**
     * @brief 异步任务类型
     *
     * @note co_await后会释放协程帧以实现积极的内存回收
     */
    struct async_task
    {
        /// 协程柄类型
        using handle_t = ::verilator_utils::task<void>::handle_t;

        /**
         * @brief 将同步任务转化为异步任务，并将任务添加到调度器就绪队列
         *
         * @note 任务必须处于initial_suspend状态，且不能有父任务
         * @param scheduler 调度器引用
         * @param task 同步任务对象
         */
        async_task(::verilator_utils::eval_scheduler& scheduler, ::verilator_utils::task<void> task) :
            subhandle{task.get_handle()}
        {
            using namespace ::std::string_view_literals;
            REQUIRE_MESSAGE(static_cast<bool>(task), "该任务对象未绑定协程"sv);
            auto&& promise{task.get_promise()};
            REQUIRE_MESSAGE(promise.parent == nullptr, "该任务已经绑定到父任务，不能转化为异步任务"sv);
            REQUIRE_MESSAGE(promise.status == ::verilator_utils::task<void>::status_enum::initial_suspend,
                            "该任务已开始执行，不能转化为异步任务"sv);
            promise.is_async = true;
            promise.scheduler = &scheduler;
            scheduler.add_task(::std::move(task));
        }

        /**
         * @brief 析构异步任务对象并销毁绑定的子协程柄
         *
         * @note 若协程未完成，则将所有权交给调度器
         */
        ~async_task() noexcept { detach(); }

        async_task(const async_task&) noexcept = delete;
        async_task& operator= (const async_task&) noexcept = delete;
        async_task& operator= (async_task&& other) = delete;

        async_task(async_task&& other) noexcept : subhandle{::std::exchange(other.subhandle, nullptr)} {}

        /**
         * @brief 销毁绑定的协程柄
         *
         */
        void destroy()
        {
            if(subhandle) { ::std::exchange(subhandle, nullptr).destroy(); }
        }

        /**
         * @brief 获取任务的协程句柄
         *
         * @return 任务的协程句柄
         */
        [[nodiscard]] handle_t get_handle() const { return subhandle; }

        /**
         * @brief 获取任务的promise引用
         *
         * @return 任务的promise引用
         */
        [[nodiscard]] ::verilator_utils::task<void>::promise_type& get_promise() const noexcept { return subhandle.promise(); }

        /**
         * @brief 判断子任务是否执行完
         *
         * @return 子任务是否执行完
         */
        [[nodiscard]] bool done() const noexcept { return subhandle.done(); }

        /**
         * @brief 分离异步任务的协程柄，此后异步任务不再持有该协程柄
         *
         * @note 若任务已完成则销毁协程柄，若任务未完成则托管给调度器
         * @return 异步任务的协程柄
         */
        void detach() noexcept
        {
            if(subhandle)
            {
                if(subhandle.done()) { subhandle.destroy(); }
                else
                {
                    // 将孤儿协程托管给调度器
                    subhandle.promise().is_async = false;
                    subhandle.promise().parent = nullptr;
                    subhandle.promise().parent_promise = nullptr;
                }
                subhandle = nullptr;
            }
        }

        /**
         * @brief 检查任务对象是否绑定了协程柄
         *
         * @return 是否绑定了协程柄
         */
        explicit operator bool() const noexcept { return static_cast<bool>(subhandle); }

        /**
         * @brief 检查任务对象是否可等待
         *
         * @return 是否可等待
         */
        [[nodiscard]] bool joinable() const noexcept { return static_cast<bool>(subhandle); }

        /**
         * @brief 实现异步子任务的可等待体
         *
         */
        struct async_task_awaiter
        {
            /// 子任务的协程柄
            handle_t subhandle;

            /**
             * @brief 判断是否立即完成
             *
             * @return 子任务已执行完则立即完成
             */
            [[nodiscard]] bool await_ready() const { return subhandle.done(); }

            /**
             * @brief 向调度器事件队列中注册等待事件，然后挂起协程
             *
             * @param handle 当前任务的协程柄
             */
            void await_suspend(handle_t handle) const
            {
                subhandle.promise().is_async = false;
                subhandle.promise().parent = handle;
                subhandle.promise().parent_promise = ::std::addressof(handle.promise());
            }

            /**
             * @brief 恢复等待任务的执行
             *
             * @note 异步任务下，父子任务同时存在于调度队列中，不能在子任务完成前恢复父任务
             * @throws eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
             * @throws 若子任务抛出异常则重新抛出异常
             */
            void await_resume() const
            {
                // 协作式取消的优先级更高
                subhandle.promise().scheduler->throw_if_finish();
                subhandle.promise().rethrow_exception();
            }

            async_task_awaiter(const async_task_awaiter&) = delete;
            async_task_awaiter& operator= (const async_task_awaiter&) = delete;
            async_task_awaiter& operator= (async_task_awaiter&&) = delete;

            explicit async_task_awaiter(handle_t subhandle) noexcept : subhandle{subhandle} {}

            async_task_awaiter(async_task_awaiter&& other) noexcept : subhandle{::std::exchange(other.subhandle, nullptr)} {}

            ~async_task_awaiter() noexcept { subhandle.destroy(); }
        };

        async_task_awaiter operator co_await()
        {
            REQUIRE(joinable());
            return async_task_awaiter{::std::exchange(subhandle, nullptr)};
        }

    private:
        /// 子任务的协程柄
        handle_t subhandle;
    };

    /**
     * @brief 异步任务池
     *
     */
    struct spawn_pool
    {
    private:
        using pool_t = ::std::vector<::verilator_utils::async_task>;
        /// 任务池
        pool_t pool;
        /// 调度器引用
        ::verilator_utils::eval_scheduler& scheduler;

        /**
         * @brief 执行join_all操作
         *
         * @return 子协程
         */
        [[nodiscard]] ::verilator_utils::task<void> do_join_all()
        {
            ::std::vector<::std::exception_ptr> exceptions{};
            for(auto&& subtask: pool)
            {
                try
                {
                    co_await subtask;
                }
                catch(...)
                {
                    exceptions.emplace_back(::std::current_exception());
                }
            }
            pool.clear();
            if(!exceptions.empty())
            {
                throw exceptions;  // NOLINT(misc-throw-by-value-catch-by-reference,cert-err09-cpp,cert-err61-cpp)
            }
        }

    public:
        /**
         * @brief 构造异步任务池
         *
         * @param scheduler 调度器引用
         */
        explicit spawn_pool(::verilator_utils::eval_scheduler& scheduler) noexcept : pool{}, scheduler{scheduler} {}

        spawn_pool(const spawn_pool&) = delete;
        spawn_pool& operator= (const spawn_pool&) = delete;
        spawn_pool& operator= (spawn_pool&&) = delete;
        spawn_pool(spawn_pool&& other) noexcept = default;
        ~spawn_pool() noexcept = default;

        /**
         * @brief 将同步任务转化为异步任务并添加到任务池中
         *
         * @param task 同步任务
         */
        void add_task(::verilator_utils::task<void> task) { pool.emplace_back(scheduler, ::std::move(task)); }

        /**
         * @brief 实现等待任务池中任意任务完成使用的可等待体
         *
         */
        struct join_any_awaiter
        {
            explicit join_any_awaiter(pool_t& pool) noexcept : pool{pool} {}

            /**
             * @brief 判断是否立即完成
             *
             * @return 所有子任务都已执行完则立即完成
             */
            bool await_ready() { return any_tasks_done(); }

            /**
             * @brief 向子任务注册父协程信息，以便由子协程唤醒父协程
             *
             * @param handle 当前任务的协程柄
             */
            void await_suspend(::verilator_utils::async_task::handle_t handle)
            {
                for(auto&& subtask: pool)
                {
                    subtask.get_promise().parent = handle;
                    subtask.get_promise().parent_promise = ::std::addressof(handle.promise());
                }
            }

            /**
             * @brief 恢复等待任务的执行
             *
             * @throws eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
             * @throws 子任务中未处理的异常
             */
            void await_resume()
            {
                // 没有子任务立即就绪，需要遍历任务池查找就绪任务
                if(ptr == nullptr)
                {
                    for(auto&& subtask: pool)
                    {
                        subtask.get_promise().parent = nullptr;
                        subtask.get_promise().parent_promise = nullptr;
                        if(subtask.done()) { ptr = ::std::addressof(subtask); }
                    }

                    // 标记ptr为空的情况不可达以消除静态分析警告
                    if(ptr == nullptr) { ::std::unreachable(); }
                }
                constexpr static auto deleter{
                    [](join_any_awaiter* self)
                    {
                        // 若待析构的元素不为最后一个元素，则将最后的元素移动到当前位置，然后析构最后的空元素
                        // 否则直接析构元素
                        if(self->ptr != ::std::addressof(self->pool.back()))
                        {
                            ::std::destroy_at(self->ptr);
                            ::std::construct_at(self->ptr, ::std::move(self->pool.back()));
                        }
                        self->pool.pop_back();
                    },
                };
                ::std::unique_ptr<join_any_awaiter, decltype(deleter)> _{this, deleter};
                auto&& promise{ptr->get_promise()};
                promise.scheduler->throw_if_finish();
                promise.rethrow_exception();
            }

        private:
            /**
             * @brief 判断是否所有子任务都执行完毕
             *
             * @return 是否所有子任务都执行完毕
             */
            bool any_tasks_done()
            {
                auto iter{::std::ranges::find(pool, true, [](::verilator_utils::async_task& task) { return task.done(); })};
                if(iter != pool.end()) { ptr = ::std::to_address(iter); }
                return ptr != nullptr;
            }

            /// 子任务视图
            pool_t& pool;
            /// 首个完成任务的指针
            ::verilator_utils::async_task* ptr{};
        };

        /**
         * @brief 判断任务池是否为空
         *
         * @return 任务池是否为空
         */
        [[nodiscard]] bool empty() const { return pool.empty(); };

        /**
         * @brief 判断任务池是否可等待
         *
         * @return 任务池是否可等待
         */
        [[nodiscard]] bool joinable() const { return !pool.empty(); };

        /**
         * @brief 等待任务池中所有任务完成
         *
         * @return 子协程，在其上执行co_await以获取结果
         */
        [[nodiscard]] ::verilator_utils::task<void> join_all()
        {
            using namespace ::std::string_view_literals;
            REQUIRE_MESSAGE(joinable(), "任务集合不能为空"sv);
            return do_join_all();
        }

        /**
         * @brief 等待任务池中任意任务完成
         *
         * @return 可等待体
         */
        [[nodiscard]] join_any_awaiter join_any()
        {
            using namespace ::std::string_view_literals;
            REQUIRE_MESSAGE(joinable(), "任务集合不能为空"sv);
            return join_any_awaiter{pool};
        }

        /**
         * @brief 将池中所有任务托管给调度器
         *
         * @return 可等待体
         */
        [[nodiscard]] ::std::suspend_never join_none()
        {
            using namespace ::std::string_view_literals;
            REQUIRE_MESSAGE(joinable(), "任务集合不能为空"sv);
            pool.clear();
            return ::std::suspend_never{};
        }
    };

    namespace detail
    {
        /**
         * @brief 实现无挂起异步任务池获取的可等待体
         *
         */
        struct get_spawn_pool_awaiter : ::verilator_utils::detail::no_suspend_awaiter
        {
            /// 调度器指针
            ::verilator_utils::eval_scheduler* scheduler;

            template <::verilator_utils::is_coroutine_promise promise_type>
            void set_handle_impl(::std::coroutine_handle<promise_type> handle)
            { scheduler = handle.promise().check_scheduler(); }

            [[nodiscard]] ::verilator_utils::spawn_pool await_resume() const { return ::verilator_utils::spawn_pool{*scheduler}; }
        };

        /**
         * @brief 实现同步任务转异步任务的可等待体
         *
         */
        struct to_async_awaiter : ::verilator_utils::detail::no_suspend_awaiter
        {
            /// 同步任务
            ::verilator_utils::task<void> task;
            /// 调度器指针
            ::verilator_utils::eval_scheduler* scheduler{};

            template <::verilator_utils::is_coroutine_promise promise_type>
            void set_handle_impl(::std::coroutine_handle<promise_type> handle)
            { scheduler = handle.promise().check_scheduler(); }

            [[nodiscard]] ::verilator_utils::async_task await_resume()
            { return ::verilator_utils::async_task{*scheduler, ::std::move(task)}; }
        };

        struct add_task_awaiter : ::verilator_utils::detail::no_suspend_awaiter
        {
            /// 同步任务
            ::verilator_utils::task<void> task;
            /// 调度器指针
            ::verilator_utils::eval_scheduler* scheduler{};

            template <::verilator_utils::is_coroutine_promise promise_type>
            void set_handle_impl(::std::coroutine_handle<promise_type> handle)
            { scheduler = handle.promise().check_scheduler(); }

            void await_resume() noexcept { scheduler->add_task(::std::move(task)); }
        };
    }  // namespace detail

    /**
     * @brief 获取异步任务池
     *
     * @return 可等待体
     * @code {.cpp}
     * task foo()
     * {
     *     auto pool{co_await get_spawn_pool()};
     * }
     * @endcode
     */
    [[nodiscard]] ::verilator_utils::detail::get_spawn_pool_awaiter get_spawn_pool() noexcept
    { return ::verilator_utils::detail::get_spawn_pool_awaiter{}; }

    /**
     * @brief 将同步任务转为异步任务
     *
     * @param task 同步任务
     * @return 可等待体
     */
    [[nodiscard]] ::verilator_utils::detail::to_async_awaiter to_async(::verilator_utils::task<void> task)
    { return ::verilator_utils::detail::to_async_awaiter{.task = ::std::move(task)}; }

    /**
     * @brief 向调度器中添加一个任务
     *
     * @param task 同步任务
     * @return 可等待体
     */
    [[nodiscard]] ::verilator_utils::detail::add_task_awaiter add_task(::verilator_utils::task<void> task)
    { return ::verilator_utils::detail::add_task_awaiter{.task = ::std::move(task)}; }
}  // namespace verilator_utils

export namespace verilator_utils
{
    /**
     * @brief 邮箱，类似SystemVerilog mailbox
     *
     * @tparam type 元素类型
     */
    template <::std::move_constructible type>
    struct mailbox
    {
        using value_type = type;
        using reference = value_type&;
        using const_reference = const value_type&;

    private:
        constexpr static auto do_pop{[](::std::vector<type>* queue) static noexcept { queue->erase(queue->begin()); }};
        using do_pop_t = ::std::unique_ptr<::std::vector<type>, decltype(do_pop)>;
        friend struct ::std::formatter<mailbox>;

    public:
        /**
         * @brief 创建邮箱对象
         *
         * @param max_count 邮箱最大容量，为0表示无限容量
         */
        explicit mailbox(::std::size_t max_count = 0) : max_count{max_count}
        {
            if(max_count != 0) { queue.reserve(max_count); }
        }

        /**
         * @brief 获取邮箱内元素数量
         *
         * @return 元素数量
         */
        [[nodiscard]] ::std::size_t num() const noexcept { return queue.size(); }

        /**
         * @brief 向邮箱末尾放入元素，容量不足时会阻塞
         *
         * @tparam args_t 参数类型列表
         * @param args 参数列表
         * @return 子任务，配合co_await使用
         */
        template <typename... args_t>
        [[nodiscard]] ::verilator_utils::task<void> put(args_t&&... args)
            requires (::std::constructible_from<value_type, args_t...>)
        {
            if(max_count != 0 && queue.size() == max_count)
            {
                co_await ::verilator_utils::wait_event([this] { return queue.size() < max_count; });
            }
            queue.emplace_back(::std::forward<args_t>(args)...);
        }

        /**
         * @brief 尝试向邮箱末尾放入元素，不会阻塞
         *
         * @tparam args_t 参数类型列表
         * @param args 参数列表
         * @return 是否成功放入元素
         */
        template <typename... args_t>
        bool try_put(args_t&&... args) noexcept
            requires (::std::constructible_from<value_type, args_t...>)
        {
            if(max_count == 0 || queue.size() < max_count)
            {
                queue.emplace_back(::std::forward<args_t>(args)...);
                return true;
            }
            else
            {
                return false;
            }
        }

        /**
         * @brief 从邮箱获取首个元素，然后删除该元素，邮箱为空时会阻塞
         *
         * @return 子任务，配合co_await使用
         */
        [[nodiscard]] ::verilator_utils::task<value_type> get()
        {
            if(queue.empty())
            {
                co_await ::verilator_utils::wait_event([this] { return !queue.empty(); });
            }
            do_pop_t _{::std::addressof(queue)};
            co_return ::std::move(queue.front());
        }

        /**
         * @brief 尝试从邮箱获取首个元素，然后删除该元素，不会阻塞
         *
         * @return std::optional 成功获取时包含元素，否则为空
         */
        ::std::optional<value_type> try_get() noexcept(::std::is_nothrow_move_constructible_v<value_type>)
        {
            if(!queue.empty())
            {
                do_pop_t _{::std::addressof(queue)};
                return ::std::optional<value_type>{::std::move(queue.front())};
            }
            else
            {
                return ::std::nullopt;
            }
        }

        /**
         * @brief 从邮箱获取首个元素，不会删除元素，邮箱为空时会阻塞
         *
         * @return 子任务，配合co_await使用
         */
        auto peek(this auto&& self) noexcept -> ::verilator_utils::task<decltype(self.queue.front())>
        {
            if(self.queue.empty())
            {
                co_await ::verilator_utils::wait_event([&self] { return !self.queue.empty(); });
            }
            co_return self.queue.front();
        }

        /**
         * @brief 从邮箱获取首个元素，不会删除元素和阻塞
         *
         * @return std::optional 成功获取时包含元素引用，否则为空
         */
        auto try_peek(this auto&& self) noexcept
        {
            using optional_t = ::std::optional<decltype(self.queue.front())>;
            return self.queue.empty() ? ::std::nullopt : optional_t{self.queue.front()};
        }

    private:
        ::std::size_t max_count{};
        ::std::vector<type> queue{};
    };

    /**
     * @brief 信号量，类似SystemVerilog semaphore
     *
     */
    struct semaphore
    {
        /**
         * @brief 创建具有指定计数器初值的信号量
         *
         * @param initial_count 计数器初值
         */
        explicit semaphore(::std::size_t initial_count = 0) noexcept : count{initial_count} {}

        /**
         * @brief 增加内部计数器
         *
         * @param update 要增加的量
         */
        void put(::std::size_t update = 1) noexcept { count += update; }

        /**
         * @brief 减少内部计数器，阻塞直到能如此
         *
         * @param update 要减小的量
         * @return 子任务，配合co_await使用
         */
        ::verilator_utils::task<void> get(::std::size_t update = 1)
        {
            if(count >= update)
            {
                count -= update;
                co_return;
            }

            auto my_ticket{next_ticket++};
            co_await ::verilator_utils::wait_event([this, my_ticket, update] { return my_ticket == ticket && count >= update; });
            count -= update;
            ++ticket;
        }

        /**
         * @brief 尝试减少内部计数器，不会阻塞
         *
         * @param update 要减小的量
         * @return 是否成功减小计数器
         */
        bool try_get(::std::size_t update = 1) noexcept
        {
            if(count >= update)
            {
                count -= update;
                return true;
            }
            else
            {
                return false;
            }
        }

    private:
        ::std::size_t count{};
        ::std::size_t ticket{};
        ::std::size_t next_ticket{};
    };

    /**
     * @brief 移位寄存器，在参考模型中对信号进行延迟
     *
     * @tparam type 数据类型
     */
    template <::verilator_utils::is_format_wrapper_data_type type>
    struct shift_register
    {

        /**
         * @brief 初始化移位寄存器
         *
         * @param depth 寄存器链深度
         * @param packed_format 打包的数据格式
         */
        explicit shift_register(::std::size_t depth, ::verilator_utils::packed_format packed_format) :
            depth{depth}, format{packed_format}
        { reg.reserve(depth); }

        /**
         * @brief 初始化移位寄存器
         *
         * @param depth 寄存器链深度
         * @param width 数据宽度
         * @param format 数据格式
         */
        explicit shift_register(::std::size_t depth, ::size_t width, ::verilator_utils::data_format::format format) :
            shift_register{
                depth,
                {width, format}
        }
        {
        }

    private:
        ::std::size_t depth;
        ::std::vector<type> reg;
        ::verilator_utils::packed_format format;
        using arg_t = ::std::conditional_t<(sizeof(type) > sizeof(::std::size_t)), const type&, type>;
        friend struct ::std::formatter<shift_register>;

    public:
        /**
         * @brief 更新移位寄存器中的值
         *
         * @param value 要更新的值
         * @return 最早进入寄存器链的值，若寄存器链未填满则为空
         */
        ::std::optional<::verilator_utils::format_wrapper<type>> update(arg_t value)
        {
            ::std::optional<::verilator_utils::format_wrapper<type>> result{};
            if(reg.size() == depth)
            {
                result = ::verilator_utils::format_wrapper<type>{reg.front(), format};
                reg.erase(reg.begin());
            }
            reg.emplace_back(value);
            return result;
        }

        /**
         * @brief 清空寄存器链
         *
         */
        void reset() noexcept { reg.clear(); }
    };
}  // namespace verilator_utils

export namespace std
{
    /**
     * @brief 邮箱格式化支持
     *
     * 支持的格式符：
     * - #: 输出详细信息
     * @tparam type 元素类型
     */
    template <::std::move_constructible type>
    struct formatter<::verilator_utils::mailbox<type>>
    {
        bool with_detail{};

        constexpr ::std::format_parse_context::iterator parse(::std::format_parse_context& ctx)
        {
            return ::verilator_utils::detail::parse_format_string_with_detail_flag(ctx,
                                                                                   "无效的verilator_utils::mailbox格式符",
                                                                                   with_detail);
        }

        template <typename iter_t, typename char_t>
        auto format(const ::verilator_utils::mailbox<type>& value, ::std::basic_format_context<iter_t, char_t>& ctx) const
        {
            if(with_detail) { return ::std::format_to(ctx.out(), "{{max_count: {}, value: {}}}", value.max_count, value.queue); }
            else
            {
                return ::std::format_to(ctx.out(), "{}", value.queue);
            }
        }
    };

    /**
     * @brief 邮箱格式化支持
     *
     * 支持的格式符：
     * - #: 输出详细信息
     * @tparam type 元素类型
     */
    template <::verilator_utils::is_format_wrapper_data_type type>
    struct formatter<::verilator_utils::shift_register<type>>
    {
        bool with_detail{};

        constexpr ::std::format_parse_context::iterator parse(::std::format_parse_context& ctx)
        {
            return ::verilator_utils::detail::parse_format_string_with_detail_flag(ctx,
                                                                                   "无效的verilator_utils::shift_register格式符",
                                                                                   with_detail);
        }

        template <typename iter_t, typename char_t>
        auto format(const ::verilator_utils::shift_register<type>& value, ::std::basic_format_context<iter_t, char_t>& ctx) const
        {
            if(with_detail) { return ::std::format_to(ctx.out(), "{{depth: {}, reg: {}}}", value.depth, value.reg); }
            else
            {
                return ::std::format_to(ctx.out(), "{}", value.reg);
            }
        }
    };
}  // namespace std

export namespace doctest
{
    template <::std::move_constructible type>
    struct StringMaker<::verilator_utils::mailbox<type>>
    {
        static ::doctest::String convert(const ::verilator_utils::mailbox<type>& value) { return ::std::format("{:#}", value); }
    };

    template <::verilator_utils::is_format_wrapper_data_type type>
    struct StringMaker<::verilator_utils::shift_register<type>>
    {
        static ::doctest::String convert(const ::verilator_utils::shift_register<type>& value)
        { return ::std::format("{:#}", value); }
    };
}  // namespace doctest
