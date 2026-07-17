module;
#include <doctest_fwd.hpp>
export module verilator_utils:task;
import :scheduler;

extern "C++"
{
#include <doctest.h>
}

export namespace verilator_utils::detail
{
    /**
     * @brief 实现无挂起协程柄获取的可等待体
     *
     */
    struct get_handle_awaiter : ::verilator_utils::task::no_suspend_awaiter
    {
        using handle_t = ::verilator_utils::task::handle_t;
        handle_t handle{};

        inline void set_handle_impl(handle_t handle) noexcept { this->handle = handle; }

        [[nodiscard]] inline handle_t await_resume() const { return handle; }
    };

    /**
     * @brief 实现无挂起调度器引用获取的可等待体
     *
     */
    struct get_scheduler_awaiter : ::verilator_utils::task::no_suspend_awaiter
    {
        using handle_t = ::verilator_utils::task::handle_t;
        ::verilator_utils::eval_scheduler* scheduler;

        inline void set_handle_impl(handle_t handle) { scheduler = handle.promise().check_scheduler(); }

        [[nodiscard]] inline ::verilator_utils::eval_scheduler& await_resume() const { return *scheduler; }
    };

    /**
     * @brief 实现结束仿真的可等待体
     *
     */
    struct eval_finish_awaiter : ::verilator_utils::task::no_suspend_awaiter
    {
        using handle_t = ::verilator_utils::task::handle_t;

        inline static void set_handle_impl(handle_t handle) { handle.promise().check_scheduler()->finish(); }

        /**
         * @brief 恢复当前任务执行
         *
         * @throws eval_finish_exception 仿真结束，抛出异常终止当前任务的执行
         */
        inline static void await_resume() { throw ::verilator_utils::eval_finish_exception{}; }
    };

    struct get_time_in_string_awaiter : ::verilator_utils::task::no_suspend_awaiter
    {
        using handle_t = ::verilator_utils::task::handle_t;
        ::verilator_utils::eval_scheduler* scheduler;

        inline void set_handle_impl(handle_t handle) { scheduler = handle.promise().check_scheduler(); }

        [[nodiscard]] inline ::std::string await_resume() const { return scheduler->time_in_string(); }
    };

    struct get_time_in_time_unit_awaiter : ::verilator_utils::task::no_suspend_awaiter
    {
        using handle_t = ::verilator_utils::task::handle_t;
        ::verilator_utils::eval_scheduler* scheduler;

        inline void set_handle_impl(handle_t handle) { scheduler = handle.promise().check_scheduler(); }

        [[nodiscard]] inline double await_resume() const { return scheduler->time_in_time_unit(); }
    };

    struct get_time_in_time_precision_awaiter : ::verilator_utils::task::no_suspend_awaiter
    {
        using handle_t = ::verilator_utils::task::handle_t;
        ::verilator_utils::eval_scheduler* scheduler;

        inline void set_handle_impl(handle_t handle) { scheduler = handle.promise().check_scheduler(); }

        [[nodiscard]] inline ::std::uint64_t await_resume() const { return scheduler->time_in_time_precision(); }
    };

    /**
     * @brief 实现延迟功能的可等待体
     *
     */
    struct time_awaiter

    {
        using handle_t = ::verilator_utils::task::handle_t;
        /// 等待时间，单位为飞秒
        ::verilator_utils::femtosecond_t time_to_wait;
        /// 调度器指针，自动绑定
        ::verilator_utils::eval_scheduler* scheduler{};

        inline time_awaiter(::verilator_utils::femtosecond_t time_to_wait) noexcept : time_to_wait{time_to_wait} {}

        /**
         * @brief 判断是否立即就绪
         *
         * @return false 不支持delta延迟，永远不会立即就绪
         */
        inline static bool await_ready() noexcept { return false; }

        /**
         * @brief 挂起等待，将当前任务加入等待队列
         *
         * @param handle 当前协程的句柄
         */
        inline void await_suspend(handle_t handle)
        {
            scheduler = handle.promise().check_scheduler();
            handle.promise().scheduler->register_wait(time_to_wait, handle);
        }

        /**
         * @brief 恢复等待任务的执行
         *
         * @throws eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
         */
        inline void await_resume() const
        {
            // if(scheduler) { scheduler->throw_if_finish(); }
            scheduler->throw_if_finish();
        }
    };

    /**
     * @brief 实现事件触发功能的可等待体
     *
     */
    struct event_awaiter
    {
        using handle_t = ::verilator_utils::task::handle_t;
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
        inline event_awaiter(callback_t&& callback) : event_callback{::std::forward<callback_t>(callback)}
        {
        }

        /**
         * @brief 判断是否立即就绪
         *
         * @return 是否立即就绪
         */
        [[nodiscard]] inline bool await_ready() const { return event_callback(); }

        /**
         * @brief 挂起等待，将当前任务加入事件队列
         *
         * @param handle 当前协程的句柄
         */
        inline void await_suspend(handle_t handle)
        {
            scheduler = handle.promise().check_scheduler();
            scheduler->register_event(event_callback, handle);
        }

        /**
         * @brief 恢复等待任务的执行
         *
         * @throws eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
         */
        inline void await_resume() const
        {
            if(scheduler != nullptr) { scheduler->throw_if_finish(); }
        }
    };

    /**
     * @brief 实现评估阶段触发功能的可等待体
     *
     */
    struct eval_stage_awaiter : ::verilator_utils::task::no_suspend_awaiter
    {
        using scheduler_t = ::verilator_utils::eval_scheduler;
        using handle_t = ::verilator_utils::task::handle_t;
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
        inline eval_stage_awaiter(scheduler_t::eval_stage_enum eval_stage) : eval_stage{eval_stage}
        {
            using namespace std::string_view_literals;
            REQUIRE_MESSAGE(eval_stage != scheduler_t::eval_stage_enum::eval_end, "该评估阶段不可等待"sv);
        }

        /**
         * @brief 根据协程柄初始化字段
         *
         * @param handle 协程柄
         */
        inline void set_handle_impl(handle_t handle)
        {
            scheduler = handle.promise().check_scheduler();
            event_callback = [this] { return scheduler->get_eval_stage() >= eval_stage; };
        }

        /**
         * @brief 判断是否立即就绪
         *
         * @return 是否立即就绪
         */
        [[nodiscard]] inline bool await_ready() const { return event_callback(); }

        /**
         * @brief 挂起等待，将当前任务加入事件队列
         *
         * @param handle 当前协程的句柄
         */
        inline void await_suspend(handle_t handle) { scheduler->register_event(event_callback, handle); }

        /**
         * @brief 恢复等待任务的执行
         *
         * @throws eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
         */
        inline void await_resume() const { scheduler->throw_if_finish(); }
    };
}  // namespace verilator_utils::detail

export namespace verilator_utils
{
    /**
     * @brief 在任务中获取协程柄
     *
     * @warning 谨慎操作promise中的字段
     * @return 可等待体
     * @code {.cpp}
     * task foo()
     * {
     *     auto handle{co_await get_handle()};
     * }
     * @endcode
     */
    [[nodiscard]] inline ::verilator_utils::detail::get_handle_awaiter get_handle() noexcept
    { return ::verilator_utils::detail::get_handle_awaiter{}; }

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
    [[nodiscard]] inline ::verilator_utils::detail::get_scheduler_awaiter get_scheduler() noexcept
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
    [[nodiscard]] inline ::verilator_utils::detail::eval_finish_awaiter eval_finish() noexcept
    { return ::verilator_utils::detail::eval_finish_awaiter{}; }

    /**
     * @brief 在任务中获取仿真时间字符串
     *
     * @return 可等待体
     */
    [[nodiscard]] inline ::verilator_utils::detail::get_time_in_string_awaiter get_time_in_string() noexcept
    { return ::verilator_utils::detail::get_time_in_string_awaiter{}; }

    /**
     * @brief 在任务中获取仿真时间，单位为dut时间单位
     *
     * @return 可等待体
     */
    [[nodiscard]] inline ::verilator_utils::detail::get_time_in_time_unit_awaiter get_time_in_time_unit() noexcept
    { return ::verilator_utils::detail::get_time_in_time_unit_awaiter{}; }

    /**
     * @brief 在任务中获取仿真时间，单位为dut时间精度
     *
     * @return 可等待体
     */
    [[nodiscard]] inline ::verilator_utils::detail::get_time_in_time_precision_awaiter get_time_in_time_precision() noexcept
    { return ::verilator_utils::detail::get_time_in_time_precision_awaiter{}; }

    /**
     * @brief 等待指定时间
     *
     * @note 不支持delta延迟，等待时间不能为0
     * @param time_to_wait 等待时间，单位为时间精度，不能为0
     * @return 可等待体
     */
    [[nodiscard]] inline ::verilator_utils::detail::time_awaiter wait_time(::verilator_utils::femtosecond_t time_to_wait)
    { return ::verilator_utils::detail::time_awaiter{time_to_wait}; }

    /**
     * @brief 等待事件触发
     *
     * @tparam callback_t 事件回调类型
     * @param callback 事件回调函数
     * @return 可等待体
     */
    template <::verilator_utils::is_event_callback callback_t>
    [[nodiscard]] inline ::verilator_utils::detail::event_awaiter wait_event(callback_t&& callback)
    { return ::verilator_utils::detail::event_awaiter{::std::forward<callback_t>(callback)}; }

    /**
     * @brief 等待上升沿
     *
     * @param callback 事件回调函数
     * @param edge_to_wait 要等待到边沿数量
     * @return event_awaiter 可等待体
     */
    [[nodiscard]] inline ::verilator_utils::detail::event_awaiter wait_posedge(const ::verilator_utils::is_bit_slice auto& bit,
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
     * @param callback 事件回调函数
     * @param edge_to_wait 要等待到边沿数量
     * @return event_awaiter 可等待体
     */
    [[nodiscard]] inline ::verilator_utils::detail::event_awaiter wait_negedge(const ::verilator_utils::is_bit_slice auto& bit,
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
     * @param callback 事件回调函数
     * @param edge_to_wait 要等待到边沿数量
     * @return event_awaiter 可等待体
     */
    [[nodiscard]] inline ::verilator_utils::detail::event_awaiter wait_alledge(const ::verilator_utils::is_bit_slice auto& bit,
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
    [[nodiscard]] inline ::verilator_utils::detail::eval_stage_awaiter
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
     * @param activate_posedge 时钟沿极性，true表示上升沿有效，false表示下降沿有效
     * @param eval_stage 目标评估阶段
     * @return 同步任务
     * @note 等待到n个给定时钟边沿后的给定评估阶段
     */
    [[nodiscard]] inline ::verilator_utils::task
        wait_edge_and_eval_stage(::verilator_utils::bit_slice<::CData> clk,
                                 ::std::size_t edge_to_wait,
                                 bool activate_posedge,
                                 ::verilator_utils::eval_scheduler::eval_stage_enum eval_stage)
    {
        if(activate_posedge) [[likely]] { co_await ::verilator_utils::wait_posedge(clk, edge_to_wait); }
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
     * @brief 生成时钟信号，占空比为50%
     *
     * @param scheduler 调度器引用
     * @param clk 时钟信号切片
     * @param period 时钟周期
     * @return 生成时钟信号的任务
     */
    [[nodiscard]] inline ::verilator_utils::task generate_clock(::verilator_utils::bit_slice<::CData> clk,
                                                                ::verilator_utils::femtosecond_t period)
    {
        auto half_period{period / 2zu};
        clk = 0;
        while(true)
        {
            co_await ::verilator_utils::wait_time(half_period);
            clk = static_cast<::std::uint64_t>(!static_cast<bool>(clk));
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
    [[nodiscard]] inline ::verilator_utils::task generate_reset(::verilator_utils::bit_slice<::CData> reset,
                                                                ::verilator_utils::bit_slice<::CData> clk,
                                                                ::size_t cycle = 3,
                                                                bool active_high = true)
    {
        reset = static_cast<::std::uint64_t>(active_high);
        co_await ::verilator_utils::wait_negedge(clk, cycle);
        reset = static_cast<::std::uint64_t>(!active_high);
    }

    /**
     * @brief 等待到验证时机
     *
     * @param clk 时钟信号切片
     * @param edge_to_wait 要等待到边沿个数
     * @param activate_posedge 时钟沿极性，true表示上升沿有效，false表示下降沿有效
     * @param eval_stage 目标评估阶段
     * @return 同步任务
     * @note 等待到edge_to_wait个给定时钟边沿后的给定评估阶段
     * @note 默认为时钟上升沿且电路 **评估完成后 ** 进行验证
     */
    [[nodiscard]] inline ::verilator_utils::task
        wait_verify(const ::verilator_utils::bit_slice<::CData>& clk,
                    ::std::size_t edge_to_wait = 1,
                    bool activate_posedge = true,
                    ::verilator_utils::eval_scheduler::eval_stage_enum eval_stage =
                        ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval)
    { return ::verilator_utils::detail::wait_edge_and_eval_stage(clk, edge_to_wait, activate_posedge, eval_stage); }

    /**
     * @brief 等待到激励时机
     *
     * @param clk 时钟信号切片
     * @param edge_to_wait 要等待到边沿个数
     * @param activate_posedge 时钟沿极性，true表示上升沿有效，false表示下降沿有效
     * @param eval_stage 目标评估阶段
     * @return 同步任务
     * @note 等待到edge_to_wait个给定时钟边沿后的给定评估阶段
     * @note 默认为时钟下降沿且电路 **评估完成后 ** 进行激励，以避免可能的竞争状态，注意寄存器采样带来的延迟
     */
    [[nodiscard]] inline ::verilator_utils::task
        wait_stimulus(const ::verilator_utils::bit_slice<::CData>& clk,
                      ::std::size_t edge_to_wait = 1,
                      bool activate_posedge = false,
                      ::verilator_utils::eval_scheduler::eval_stage_enum eval_stage =
                          ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval)
    { return ::verilator_utils::detail::wait_edge_and_eval_stage(clk, edge_to_wait, activate_posedge, eval_stage); }

    /**
     * @brief 等待直到复位完成
     *
     * @param scheduler 调度器引用
     * @param rst 复位信号切片
     * @param active_high 复位信号的极性，true表示高电平有效，false表示低电平有效
     * @return 同步任务
     * @note 等待到复位信号无效且初始评估完成
     */
    [[nodiscard]] inline ::verilator_utils::task wait_reset_finish(::verilator_utils::bit_slice<::CData> rst,
                                                                   bool active_high = true)
    {
        co_await ::verilator_utils::wait_event([rst, active_high] { return rst != active_high; });
        co_await ::verilator_utils::wait_eval_stage(::verilator_utils::eval_scheduler::eval_stage_enum::after_initial_eval);
    }

    namespace detail
    {
        /// 3~64位LFSR m序列反馈系数表
        constexpr inline ::std::array lfsr_feedback_mask_table{::std::to_array({
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
    }  // namespace detail

    /**
     * @brief 斐波那契型LFSR生成器，输出序列长度无限
     *
     * @param width LFSR宽度，取值范围为[3, 64]
     * @param feedback_mask 反馈表达式，省略最高次项但包含常数项，为0表示使用m序列对应的反馈表达式
     * @param initial_value LFSR初始值
     * @return 生成器
     */
    [[nodiscard]] inline ::verilator_utils::generator<bool> fibonacci_lfsr_generator(::std::size_t width,
                                                                                     ::std::uint64_t feedback_mask = 0,
                                                                                     ::std::uint64_t initial_value = 1)
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
    [[nodiscard]] inline ::verilator_utils::generator<bool> galois_lfsr_generator(::std::size_t width,
                                                                                  ::std::uint64_t feedback_mask = 0,
                                                                                  ::std::uint64_t initial_value = 1)
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
        ::std::uint64_t value{initial_value};
        while(true)
        {
            auto out{value & 1zu};
            co_yield static_cast<bool>(out);
            auto masked_broadcast_out{(0 - out) & feedback_mask};
            value = (value ^ masked_broadcast_out) >> 1zu | out << (width - 1);
        }
    }
}  // namespace verilator_utils

export namespace verilator_utils
{
    /**
     * @brief 异步任务类型
     *
     */
    struct async_task
    {
        /// 协程柄类型
        using handle_t = ::verilator_utils::task::handle_t;

        /**
         * @brief 将同步任务转化为异步任务，并将任务添加到调度器就绪队列
         *
         * @note 任务必须处于initial_suspend状态，且不能有父任务
         * @param scheduler 调度器引用
         * @param task 同步任务对象
         */
        inline async_task(::verilator_utils::eval_scheduler& scheduler, ::verilator_utils::task task) :
            subhandle{task.get_handle()}
        {
            using namespace ::std::string_view_literals;
            REQUIRE_MESSAGE(static_cast<bool>(task), "该任务对象未绑定协程"sv);
            auto&& promise{task.get_promise()};
            REQUIRE_MESSAGE(promise.parent == nullptr, "该任务已经绑定到父任务，不能转化为异步任务"sv);
            REQUIRE_MESSAGE(promise.status == ::verilator_utils::task::status_enum::initial_suspend,
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
        inline ~async_task() noexcept { detach(); }

        inline async_task(const async_task&) noexcept = delete;
        inline async_task& operator= (const async_task&) noexcept = delete;
        inline async_task& operator= (async_task&& other) = delete;

        inline async_task(async_task&& other) noexcept : subhandle{::std::exchange(other.subhandle, nullptr)} {}

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
        [[nodiscard]] inline handle_t get_handle() const { return subhandle; }

        /**
         * @brief 获取任务的promise引用
         *
         * @return 任务的promise引用
         */
        [[nodiscard]] inline ::verilator_utils::task::promise_type& get_promise() const noexcept { return subhandle.promise(); }

        /**
         * @brief 判断子任务是否执行完
         *
         * @return 子任务是否执行完
         */
        [[nodiscard]] inline bool done() const noexcept { return subhandle.done(); }

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
        [[nodiscard]] inline bool joinable() const noexcept { return static_cast<bool>(subhandle); }

        /**
         * @brief 实现异步子任务的可等待体
         *
         */
        struct async_task_awaiter
        {
            /// 事件回调函数，判断子任务是否完成
            ::verilator_utils::default_event_callback callback;
            /// 子任务的协程柄
            handle_t subhandle;

            /**
             * @brief 创建异步任务的可等待体
             *
             * @param subhandle 子任务的协程柄
             */
            inline async_task_awaiter(handle_t subhandle) :
                callback{[subhandle] { return subhandle.done(); }}, subhandle{subhandle}
            {
            }

            /**
             * @brief 判断是否立即完成
             *
             * @return 子任务已执行完则立即完成
             */
            [[nodiscard]] inline bool await_ready() const { return subhandle.done(); }

            /**
             * @brief 向调度器事件队列中注册等待事件，然后挂起协程
             *
             * @param handle 当前任务的协程柄
             */
            inline void await_suspend(handle_t handle) { subhandle.promise().scheduler->register_event(callback, handle); }

            /**
             * @brief 恢复等待任务的执行
             *
             * @note 异步任务下，父子任务同时存在于调度队列中，不能在子任务完成前恢复父任务
             * @throws eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
             * @throws 若子任务抛出异常则重新抛出异常
             */
            inline void await_resume() const
            {
                // 协作式取消的优先级更高
                subhandle.promise().scheduler->throw_if_finish();
                subhandle.promise().rethrow_exception();
            }
        };

        inline async_task_awaiter operator co_await() const
        {
            REQUIRE(joinable());
            return async_task_awaiter{subhandle};
        }

    private:
        /// 子任务的协程柄
        handle_t subhandle;
    };

    namespace detail
    {
        /**
         * @brief 等待所有异步任务完成用的可等待体
         *
         */
        struct async_task_join_all_awaiter
        {
            inline async_task_join_all_awaiter(::std::span<::verilator_utils::async_task> tasks) :
                callback{[tasks] { return all_tasks_done(tasks); }}, tasks{tasks}
            {
                using namespace ::std::string_view_literals;
                REQUIRE_FALSE_MESSAGE(tasks.empty(), "任务集合不能为空"sv);
                auto* scheduler{tasks.front().get_promise().check_scheduler()};
                REQUIRE_MESSAGE(::std::ranges::all_of(tasks,
                                                      [scheduler](const ::verilator_utils::async_task& task) noexcept
                                                      { return task.get_promise().scheduler == scheduler; }),
                                "所有任务必须绑定同一个调度器"sv);
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
            inline void await_suspend(::verilator_utils::async_task::handle_t handle)
            {
                using namespace ::std::string_view_literals;

                auto* this_scheduler{handle.promise().scheduler};
                auto* task_scheduler{tasks.front().get_promise().scheduler};
                REQUIRE_MESSAGE(this_scheduler == task_scheduler, "所有任务必须绑定同一个调度器"sv);
                this_scheduler->register_event(callback, handle);
            }

            /// 若子任务存在未处理的异常则抛出该向量
            using unhandled_exception_vector = ::std::vector<::std::exception_ptr>;

            /**
             * @brief 恢复等待任务的执行
             *
             * @throws eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
             * @throws unhandled_exception_vector 子任务中未处理的异常
             */
            inline void await_resume()
            {
                tasks.front().get_promise().scheduler->throw_if_finish();
                auto vec{::std::views::filter(tasks,
                                              [](::verilator_utils::async_task& task) static noexcept
                                              { return task.get_promise().with_unhandled_exception(); }) |
                         ::std::views::transform([](::verilator_utils::async_task& task) static noexcept
                                                 { return task.get_promise().exception; }) |
                         ::std::ranges::to<unhandled_exception_vector>()};

                if(!vec.empty())
                {
                    throw vec;  // NOLINT(misc-throw-by-value-catch-by-reference,cert-err09-cpp,cert-err61-cpp)
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
        };

        /**
         * @brief 等待任意异步任务完成用的可等待体
         *
         */
        struct async_task_join_any_awaiter
        {
            inline async_task_join_any_awaiter(::std::span<::verilator_utils::async_task> tasks) :
                callback{[this] { return any_tasks_done(); }}, tasks{tasks}, iter{tasks.end()}
            {
                using namespace ::std::string_view_literals;
                REQUIRE_FALSE_MESSAGE(tasks.empty(), "任务集合不能为空"sv);
                auto* scheduler{tasks.front().get_promise().check_scheduler()};
                REQUIRE_MESSAGE(::std::ranges::all_of(tasks,
                                                      [scheduler](const ::verilator_utils::async_task& task) noexcept
                                                      { return task.get_promise().scheduler == scheduler; }),
                                "所有任务必须绑定同一个调度器"sv);
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
            inline void await_suspend(::verilator_utils::async_task::handle_t handle)
            {
                using namespace ::std::string_view_literals;

                auto* this_scheduler{handle.promise().scheduler};
                auto* task_scheduler{tasks.front().get_promise().scheduler};
                REQUIRE_MESSAGE(this_scheduler == task_scheduler, "所有任务必须绑定同一个调度器"sv);
                this_scheduler->register_event(callback, handle);
            }

            /**
             * @brief 恢复等待任务的执行
             *
             * @throws eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
             * @throws 子任务中未处理的异常
             */
            inline void await_resume()
            {
                auto&& promise{iter->get_promise()};
                promise.scheduler->throw_if_finish();
                promise.rethrow_exception();
            }

        private:
            /**
             * @brief 判断是否所有子任务都执行完毕
             *
             * @return 是否所有子任务都执行完毕
             */
            inline bool any_tasks_done()
            {
                iter = ::std::ranges::find(tasks, true, [](::verilator_utils::async_task& task) { return task.done(); });
                return iter != tasks.end();
            }

            /// 事件回调函数，判断子任务是否都完成
            ::verilator_utils::default_event_callback callback;
            /// 子任务视图
            ::std::span<::verilator_utils::async_task> tasks;

        protected:
            /// 首个完成任务的迭代器
            ::std::span<::verilator_utils::async_task>::iterator iter;
        };

        /**
         * @brief 将任务集合中所有任务托管给调度器的可等待体
         *
         */
        struct async_task_join_none_awaiter : ::std::suspend_always
        {
            inline async_task_join_none_awaiter(::std::span<::verilator_utils::async_task> tasks) : ::std::suspend_always{}
            {
                using namespace ::std::string_view_literals;
                REQUIRE_FALSE_MESSAGE(tasks.empty(), "任务集合不能为空"sv);
                auto* scheduler{tasks.front().get_promise().check_scheduler()};
                REQUIRE_MESSAGE(::std::ranges::all_of(tasks,
                                                      [scheduler](const ::verilator_utils::async_task& task) noexcept
                                                      { return task.get_promise().scheduler == scheduler; }),
                                "所有任务必须绑定同一个调度器"sv);
                ::std::ranges::for_each(tasks, [](::verilator_utils::async_task& task) { task.detach(); });
            }
        };
    }  // namespace detail

    /**
     * @brief 等待所有异步任务完成
     *
     * @param tasks 异步任务集合
     * @return 可等待体
     */
    [[nodiscard]] inline ::verilator_utils::detail::async_task_join_all_awaiter
        async_task_join_all(::std::span<::verilator_utils::async_task> tasks)
    { return ::verilator_utils::detail::async_task_join_all_awaiter{tasks}; }

    /**
     * @brief 等待任意异步任务完成
     *
     * @param tasks 异步任务集合
     * @return 可等待体
     */
    [[nodiscard]] inline ::verilator_utils::detail::async_task_join_any_awaiter
        async_task_join_any(::std::span<::verilator_utils::async_task> tasks)
    { return ::verilator_utils::detail::async_task_join_any_awaiter{tasks}; }

    /**
     * @brief 将任务集合中所有任务托管给调度器
     *
     * @param tasks 异步任务集合
     * @return 可等待体
     */
    [[nodiscard]] inline ::verilator_utils::detail::async_task_join_none_awaiter
        async_task_join_none(::std::span<::verilator_utils::async_task> tasks)
    { return ::verilator_utils::detail::async_task_join_none_awaiter{tasks}; }

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

    public:
        /**
         * @brief 构造异步任务池
         *
         * @param scheduler 调度器引用
         */
        inline spawn_pool(::verilator_utils::eval_scheduler& scheduler) noexcept : pool{}, scheduler{scheduler} {}

        inline spawn_pool(const spawn_pool&) = delete;
        inline spawn_pool& operator= (const spawn_pool&) = delete;
        inline spawn_pool& operator= (spawn_pool&&) = delete;
        inline spawn_pool(spawn_pool&& other) noexcept = default;
        inline ~spawn_pool() noexcept = default;

        /**
         * @brief 将同步任务转化为异步任务并添加到任务池中
         *
         * @param task 同步任务
         */
        inline void add_task(::verilator_utils::task task) { pool.emplace_back(scheduler, ::std::move(task)); }

        /**
         * @brief 实现等待任务池中所有任务完成使用的可等待体
         *
         */
        struct join_all_awaiter : ::verilator_utils::detail::async_task_join_all_awaiter
        {
            // 尽管pool_t和基类中的std::span引用了同一个范围，但为了复用代码，接受这一重复
            using base_t = ::verilator_utils::detail::async_task_join_all_awaiter;
            /// 任务池引用
            pool_t& pool;

            inline join_all_awaiter(pool_t& pool) : base_t{pool}, pool{pool} {}

            inline void await_resume()  // NOLINT(bugprone-derived-method-shadowing-base-method)
            {
                try
                {
                    base_t::await_resume();
                    pool.clear();
                }
                catch(...)
                {
                    pool.clear();
                    throw;
                }
            }
        };

        /**
         * @brief 实现等待任务池中任意任务完成使用的可等待体
         *
         */
        struct join_any_awaiter : ::verilator_utils::detail::async_task_join_any_awaiter
        {
            // 尽管pool_t和基类中的std::span引用了同一个范围，但为了复用代码，接受这一重复
            using base_t = ::verilator_utils::detail::async_task_join_any_awaiter;
            /// 任务池引用
            pool_t& pool;

            inline join_any_awaiter(pool_t& pool) : base_t{pool}, pool{pool} {}

            inline void await_resume()  // NOLINT(bugprone-derived-method-shadowing-base-method)
            {
                auto do_erase{
                    [this]
                    {
                        auto* ptr{::std::to_address(iter)};
                        ::std::destroy_at(ptr);
                        ::std::construct_at(ptr, ::std::move(pool.back()));
                        pool.pop_back();
                    },
                };
                try
                {
                    base_t::await_resume();
                    do_erase();
                }
                catch(...)
                {
                    do_erase();
                    throw;
                }
            }
        };

        /**
         * @brief 判断任务池是否为空
         *
         * @return 任务池是否为空
         */
        [[nodiscard]] inline bool empty() const { return pool.empty(); };

        /**
         * @brief 判断任务池是否可等待
         *
         * @return 任务池是否可等待
         */
        [[nodiscard]] inline bool joinable() const { return !pool.empty(); };

        /**
         * @brief 等待任务池中所有任务完成
         *
         * @return 可等待体
         */
        [[nodiscard]] inline join_all_awaiter join_all() { return join_all_awaiter{pool}; }

        /**
         * @brief 等待任务池中任意任务完成
         *
         * @return 可等待体
         */
        [[nodiscard]] inline join_any_awaiter join_any() { return join_any_awaiter{pool}; }

        /**
         * @brief 将池中所有任务托管给调度器
         *
         * @note 会挂起当前任务以便开始执行异步子任务
         * @return 可等待体
         */
        [[nodiscard]] inline ::std::suspend_always join_none()
        {
            using namespace ::std::string_view_literals;
            REQUIRE_MESSAGE(joinable(), "任务集合不能为空"sv);
            pool.clear();
            return ::std::suspend_always{};
        }
    };

    namespace detail
    {
        /**
         * @brief 实现无挂起异步任务池获取的可等待体
         *
         */
        struct get_spawn_pool_awaiter : ::verilator_utils::task::no_suspend_awaiter
        {
            using handle_t = ::verilator_utils::task::handle_t;
            /// 调度器指针
            ::verilator_utils::eval_scheduler* scheduler;

            inline void set_handle_impl(handle_t handle) { scheduler = handle.promise().check_scheduler(); }

            [[nodiscard]] inline ::verilator_utils::spawn_pool await_resume() const
            { return ::verilator_utils::spawn_pool{*scheduler}; }
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
    [[nodiscard]] inline ::verilator_utils::detail::get_spawn_pool_awaiter get_spawn_pool() noexcept
    { return ::verilator_utils::detail::get_spawn_pool_awaiter{}; }
}  // namespace verilator_utils
