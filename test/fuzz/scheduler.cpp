#include <doctest_macros.hpp>
import fuzz_support;
import unit_test;

namespace
{
    /// 任务创建类操作的最大嵌套深度，避免模糊测试输入构造无界递归
    constexpr ::std::size_t max_depth{2zu};

    /**
     * @brief 任务操作的操作码
     */
    enum class op_t : ::std::uint8_t
    {
        /// 等待一段时间
        wait_time,
        /// 等待事件
        wait_event,
        /// 唤醒所有等待者
        notify_all,
        /// 按FIFO唤醒一个等待者
        notify_one,
        /// 任务池：等待所有子任务
        spawn_join_all,
        /// 任务池：等待任一子任务
        spawn_join_any,
        /// 任务池：托管所有子任务
        spawn_join_none,
        /// 异步子任务，可能被取消
        async_subtask,
        /// 直接向调度器添加任务
        add_task,
        /// 获取信号量
        semaphore_get,
        /// 释放信号量
        semaphore_put,
        /// 信号边沿与时间读取
        signal_edge,
        /// 时间读取的可等待体形式
        time_query,
        /// 协程栈回溯与格式化
        stacktrace,
        /// 时钟与复位激励
        clock_reset,
        /// 验证与激励时机
        verify_stimulate,
        /// select_clock与边沿检测器接口
        select_clock,
        /// 邮箱读写
        mailbox,
        /// LFSR生成器
        lfsr,
        /// 最大仿真时间：触发仿真超时退出
        max_eval_time,
        /// 非阻塞接口与调度器状态查询
        nonblocking,
        /// 批量等待者：触发挂起队列的前缀回收与收缩路径
        bulk_waiter,
        /// 操作码总数
        total_ops,
    };

    /**
     * @brief 信号边沿与时间读取的操作码
     */
    enum class signal_op_t : ::std::uint8_t
    {
        /// 翻转共享信号
        toggle,
        /// 等待上升沿
        posedge,
        /// 等待下降沿
        negedge,
        /// 等待任意边沿
        alledge,
        /// 读取时间并等待评估阶段，4与5共用同一分支
        check_time,
        /// 与check_time共用同一分支
        check_time_extra,
        /// 操作码总数
        total_ops,
    };

    /**
     * @brief 时钟与复位激励的操作码
     */
    enum class clock_op_t : ::std::uint8_t
    {
        /// 时钟激励
        generate_clock,
        /// 同步复位
        generate_reset,
        /// 异步复位
        generate_async_reset,
        /// 等待复位结束
        wait_reset_finish,
        /// 操作码总数
        total_ops,
    };

    /**
     * @brief 验证与激励时机的操作码
     */
    enum class verify_op_t : ::std::uint8_t
    {
        /// 等待验证时机
        wait_verify,
        /// 等待激励时机
        wait_stimulate,
        /// 时钟上的校验
        verify_at_clock,
        /// 复位信号上的校验
        verify_at_reset,
        /// 操作码总数
        total_ops,
    };

    /**
     * @brief 邮箱读写的操作码
     */
    enum class mailbox_op_t : ::std::uint8_t
    {
        /// 阻塞写入
        put,
        /// 阻塞读取
        get,
        /// 阻塞窥视
        peek,
        /// 非阻塞读取
        try_get,
        /// 非阻塞写入
        try_put,
        /// 批量读写以触发空闲存储的回收路径
        recycle,
        /// 非阻塞窥视
        try_peek,
        /// 操作码总数
        total_ops,
    };

    /**
     * @brief 非阻塞接口与调度器状态查询的操作码
     */
    enum class nonblocking_op_t : ::std::uint8_t
    {
        /// 信号量的非阻塞获取
        semaphore_try_get,
        /// 收缩事件队列的空闲容量
        event_shrink,
        /// 空任务池不可join
        empty_pool,
        /// 断言报告路径
        assertion_reporting,
        /// 任务池汇总异常的报告路径
        pool_exceptions,
        /// 从根协程抛出异常
        root_exception,
        /// 协程状态对比较、时间配置校验与格式化路径
        misc,
        /// 读取时间
        check_time,
        /// 操作码总数
        total_ops,
    };

    /**
     * @brief 杂项操作的操作码
     */
    enum class misc_op_t : ::std::uint8_t
    {
        /// 协程状态对比较
        coroutine_pair,
        /// 时间配置校验
        time_config,
        /// 协程栈回溯的格式化
        stacktrace_format,
        /// 不可等待的评估阶段
        unawaitable_stage,
        /// 用真实的协程栈回溯构造协程异常
        coroutine_exception,
        /// 操作码总数
        total_ops,
    };

    /// 单个操作：操作码与两个参数
    struct op
    {
        op_t code{};
        ::std::uint64_t a{};
        ::std::uint64_t b{};
    };

    using op_list = ::std::vector<::op>;

    /// 空操作序列：作为立即完成的子任务的操作序列，必须比协程帧长寿
    const ::op_list empty_ops{};

    /// 一个模糊测试程序：根任务与可复用的子任务操作序列
    struct program
    {
        ::std::array<::op_list, 4> roots{};
        ::std::array<::op_list, 8> subtasks{};
    };

    /// 从输入中解码一段操作序列
    [[nodiscard]] ::op_list decode_ops(::fuzz::reader& input, ::std::size_t max_count)
    {
        const auto count{static_cast<::std::size_t>(input.range(0, max_count))};
        ::op_list ops{};
        ops.reserve(count);
        for(auto i{0zu}; i != count; ++i)
        {
            ops.push_back(::op{.code = ::fuzz::read_opcode<op_t>(input), .a = input.u64(), .b = input.uint(2)});
        }
        return ops;
    }

    /// 任务共享的上下文
    struct context
    {
        /// 共享事件
        ::std::array<::verilator_utils::event, 4>* events{};
        /// 共享信号量
        ::std::array<::verilator_utils::semaphore, 2>* semaphores{};
        /// 共享邮箱
        ::std::array<::verilator_utils::mailbox<::std::uint64_t>, 2>* mailboxes{};
        /// 触发激励的时钟切片
        ::verilator_utils::bit_slice<::CData>* clk{};
        /// 复位信号切片
        ::verilator_utils::bit_slice<::CData>* rst{};
        /// 共享信号
        ::CData* signal{};
        /// 调度器
        ::verilator_utils::eval_scheduler* scheduler{};
        /// 已启动的协程帧数量
        ::std::size_t* created{};
        /// 已析构的协程帧数量
        ::std::size_t* destroyed{};
        /// 观察到的子任务取消次数
        ::std::size_t* canceled{};
        /// 观察到的仿真超时次数
        ::std::size_t* timeouts{};
        /// 时间精度，单位为飞秒
        ::std::uint64_t precision_fs{1000zu};
        /// 已观察到的最大仿真时间
        ::std::uint64_t max_time{};
        /// 是否已经启动时钟激励
        bool clock_started{};
        /// 是否观察到仿真错误标志
        bool error_seen{};
    };

    [[nodiscard]] ::verilator_utils::task<void>
        run_ops(const ::program& prog, const ::op_list& ops, ::context& ctx, ::std::size_t depth);

    /// 抛出探针异常的任务：用于检查任务池汇总异常的报告路径
    ::verilator_utils::task<void> run_probe_error(const ::context& ctx, int value);

    /// 抛出框架断言失败的任务：用于检查任务池汇总异常对std::exception派生异常的报告路径
    ::verilator_utils::task<void> run_assertion_error(const ::context& ctx, ::std::uint64_t detail);

    /// 模糊测试使用的非标准异常类型：用于检查任务池汇总异常的报告路径
    struct probe_error
    { int value{}; };

    /// 判断异常是否为探针异常
    [[nodiscard]] bool is_probe_error(const ::std::exception_ptr& exception_ptr)
    {
        try
        {
            ::std::rethrow_exception(exception_ptr);
        }
        catch(const ::probe_error&)
        {
            return true;
        }
        catch(...)
        {
            return false;
        }
    }

    /// 判断异常是否为框架内部的取消类异常
    [[nodiscard]] bool is_cancellation(const ::std::exception_ptr& exception_ptr)
    {
        try
        {
            ::std::rethrow_exception(exception_ptr);
        }
        catch(const ::verilator_utils::eval_finish_exception&)
        {
            return true;
        }
        catch(const ::verilator_utils::task_cancel_exception&)
        {
            return true;
        }
        catch(const ::verilator_utils::subtask_cancel_exception&)
        {
            return true;
        }
        catch(...)
        {
            return false;
        }
    }

    /// 判断异常是否为框架的断言失败
    [[nodiscard]] bool is_assertion_failure(const ::std::exception_ptr& exception_ptr)
    {
        try
        {
            ::std::rethrow_exception(exception_ptr);
        }
        catch(const ::verilator_utils::assertion_error&)
        {
            return true;
        }
        catch(...)
        {
            return false;
        }
    }

    /// 判断异常是否为协程异常
    [[nodiscard]] bool is_coroutine_exception(const ::std::exception_ptr& exception_ptr)
    {
        try
        {
            ::std::rethrow_exception(exception_ptr);
        }
        catch(const ::verilator_utils::coroutine_exception&)
        {
            return true;
        }
        catch(...)
        {
            return false;
        }
    }

    /// 检查任务池汇总异常中只包含取消类异常与模糊测试主动抛出的异常
    void check_pool_exceptions(const ::verilator_utils::spawn_pool::join_all_exception& exception)
    {
        ::fuzz::check(!::std::string_view{exception.what()}.empty());
        for(const auto& exception_ptr: exception.exceptions())
        {
            if(!::is_cancellation(exception_ptr) && !::is_probe_error(exception_ptr) && !::is_assertion_failure(exception_ptr) &&
               !::is_coroutine_exception(exception_ptr))
            {
                ::fuzz::fail("pool subtask threw an unexpected exception");
            }
        }
    }

    /// 检查时间读取的一致性与单调性
    void check_time(::context& ctx)
    {
        const auto now{static_cast<::std::uint64_t>(ctx.scheduler->time_in_time_precision())};
        if(now < ctx.max_time) { ::fuzz::fail("simulation time went backwards"); }
        ctx.max_time = now;
        ::fuzz::check(ctx.scheduler->time_in_time_unit() >= 0.0);
        ::fuzz::check(!ctx.scheduler->time_in_string().empty());
        ::fuzz::check(ctx.scheduler->time_precision_fs() == ctx.precision_fs);
        ctx.error_seen = ctx.error_seen || ctx.scheduler->is_error();
    }

    /// 检查断言失败异常携带的信息
    void inspect_assertion_error(const ::verilator_utils::assertion_error& error)
    {
        ::fuzz::check(!error.message().empty());
        ::fuzz::check(error.location().file_name() != nullptr);
        ::fuzz::check(error.location().line() != 0);
        // 栈回溯已由fuzz_support关闭，因此断言异常不携带栈帧
        ::fuzz::check(error.trace().frames.empty());
        const ::std::string text{error.what()};
        ::fuzz::check(!text.empty());
        // 异常描述会被缓存，重复读取不应改变内容
        ::fuzz::check(::std::string{error.what()} == text);
    }

    /// 检查协程异常的报告与原始异常访问路径
    void check_coroutine_exception(const ::verilator_utils::coroutine_exception& error)
    {
        ::fuzz::check(static_cast<bool>(error.exception()));
        // 栈回溯由fuzz_support关闭，经过根协程传播的异常不携带协程栈帧
        ::fuzz::check(error.stacktrace().empty());
        const ::std::string text{error.what()};
        ::fuzz::check(!text.empty());
        ::fuzz::check(::std::string{error.what()} == text);
        // 只有原始异常自身的类型可以取出，其余类型必须返回空指针
        const bool is_assertion{error.as<::verilator_utils::assertion_error>() != nullptr};
        const bool is_probe{error.as<::probe_error>() != nullptr};
        ::fuzz::check(is_assertion != is_probe, static_cast<::std::uint64_t>(is_assertion));
        ::fuzz::check(error.as<::verilator_utils::eval_timeout_exception>() == nullptr);
        try
        {
            error.rethrow_exception();
            ::fuzz::fail("coroutine exception was not rethrown");
        }
        catch(const ::verilator_utils::assertion_error& exception)
        {
            ::fuzz::check(!::std::string_view{exception.what()}.empty());
        }
        catch(const ::probe_error& exception)
        {
            ::fuzz::check(exception.value >= 0);
        }
    }

    /// 检查调度器构造时对时间单位与时间精度的校验
    void check_time_config_rejection(::std::uint64_t selector)
    {
        // 奇数选择器让时间精度越界，偶数选择器让时间单位越界；
        // VerilatedContext按绝对值存储时间指数，因此越界值取-16
        const bool unit_out_of_range{selector % 2u == 0u};
        ::scheduler_fixture fixture{unit_out_of_range ? -16 : -9, unit_out_of_range ? -12 : -16};
        try
        {
            const auto scheduler{fixture.make_scheduler()};
            (void)scheduler;
            ::fuzz::fail("scheduler accepted an out-of-range time configuration");
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            ::inspect_assertion_error(error);
        }
    }

    /// 触发框架断言并检查报告路径
    void check_assertion_reporting(::std::uint64_t detail)
    {
        try
        {
            ::verilator_utils::check{}(false, "模糊测试触发的断言{}"sv, detail);
            ::fuzz::fail("framework check did not fail");
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            ::inspect_assertion_error(error);
        }

        try
        {
            ::verilator_utils::check{}(false);
            ::fuzz::fail("framework check did not fail");
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            ::inspect_assertion_error(error);
        }

        // nothrow重载：条件成立时只经过报告路径，不终止进程
        ::verilator_utils::check{}(::std::nothrow, true);
        ::verilator_utils::check{}(::std::nothrow, true, "条件成立时的断言{}"sv, detail);
    }

    /// 消费生成器开头的若干个输出，返回按输出顺序拼接的位
    template <typename generator_t>
    [[nodiscard]] ::std::uint64_t consume_generator(generator_t& generator, ::std::size_t count)
    {
        ::std::uint64_t bits{};
        auto iter{generator.begin()};
        for(auto i{0zu}; i != count && iter != ::std::default_sentinel; ++i, ++iter)
        {
            bits = (bits << 1zu) | (*iter ? 1zu : 0zu);
        }
        return bits;
    }

    /**
     * @brief 检查LFSR生成器的参数校验与输出序列
     *
     * @param current 当前操作
     */
    void check_lfsr(const ::op& current)
    {
        // 由输入选择参数组合：合法参数覆盖生成逻辑，零掩码覆盖默认反馈表达式，
        // 其余组合覆盖参数校验的拒绝路径
        const auto width_selector{current.a % 4u};
        auto width{static_cast<::std::size_t>(3zu + current.a % 62zu)};
        if(width_selector == 0u) { width = 64zu; }      // 宽度上边界
        else if(width_selector == 1u) { width = 2zu; }  // 宽度不足
        const auto mask_width{::std::min(width, ::std::size_t{64zu})};
        const auto width_mask{mask_width == 64zu ? ~::std::uint64_t{} : (::std::uint64_t{1zu} << mask_width) - 1zu};
        const auto value_selector{(current.a >> 8zu) % 3u};
        auto initial_value{(current.a & width_mask) | 1zu};
        if(value_selector == 0u) { initial_value = 0; }  // 零初始值
        else if(value_selector == 1u && mask_width != 64zu)
        {
            initial_value = (current.a & width_mask) | (::std::uint64_t{1zu} << mask_width);  // 超出宽度
        }
        // 零掩码表示使用默认的m序列反馈表达式，非零掩码则直接作为反馈表达式使用
        const auto mask_selector{current.b % 4u};
        auto feedback_mask{current.b};
        if(mask_selector == 0u) { feedback_mask = 0; }
        else if(mask_selector == 1u) { feedback_mask = (current.b & width_mask) | 1zu; }
        try
        {
            if((current.a & 1zu) != 0u)
            {
                auto generator{::verilator_utils::galois_lfsr_generator(width, feedback_mask, initial_value)};
                auto reference{::verilator_utils::galois_lfsr_generator(width, feedback_mask, initial_value)};
                ::fuzz::check(::consume_generator(generator, 32zu) == ::consume_generator(reference, 32zu));
            }
            else
            {
                auto generator{::verilator_utils::fibonacci_lfsr_generator(width, feedback_mask, initial_value)};
                auto reference{::verilator_utils::fibonacci_lfsr_generator(width, feedback_mask, initial_value)};
                ::fuzz::check(::consume_generator(generator, 32zu) == ::consume_generator(reference, 32zu));
            }
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            ::inspect_assertion_error(error);
        }
    }

    /**
     * @brief 执行一段操作序列
     *
     * @param prog 模糊测试程序
     * @param ops 本次执行的操作序列
     * @param ctx 共享上下文
     * @param depth 子任务嵌套深度，用于限制递归
     */
    // 递归深度由max_depth限制，不会无界展开
    // NOLINTNEXTLINE(misc-no-recursion)
    ::verilator_utils::task<void> run_ops(const ::program& prog, const ::op_list& ops, ::context& ctx, ::std::size_t depth)
    {
        // 在协程体开始时计数：被取消而未启动的协程帧不会构造任何局部对象
        ++*ctx.created;
        const ::frame_destruction_counter counter{ctx.destroyed};
        for(const auto& current: ops)
        {
            switch(current.code)
            {
                case op_t::wait_time:  // 等待一段时间
                    co_await ::verilator_utils::wait_time(
                        ::verilator_utils::femtosecond_t{(current.a % 20u + 1u) * ctx.precision_fs});
                    break;
                case op_t::wait_event:  // 等待事件
                    co_await (*ctx.events)[current.a % ctx.events->size()];
                    break;
                case op_t::notify_all:  // 唤醒所有等待者
                    (*ctx.events)[current.a % ctx.events->size()].notify_all();
                    break;
                case op_t::notify_one:  // 按FIFO唤醒一个等待者
                    (*ctx.events)[current.a % ctx.events->size()].notify_one();
                    break;
                case op_t::spawn_join_all:   // 任务池：等待所有子任务
                case op_t::spawn_join_any:   // 任务池：等待任一子任务
                case op_t::spawn_join_none:  // 任务池：托管所有子任务
                {
                    if(depth >= ::max_depth) { break; }
                    auto pool{co_await ::verilator_utils::spawn_pool()};
                    const auto count{static_cast<::std::size_t>(current.b % 3u)};
                    for(auto i{0zu}; i != count; ++i)
                    {
                        pool.add_task(::run_ops(prog, prog.subtasks[(current.a + i) % prog.subtasks.size()], ctx, depth + 1));
                    }
                    if(pool.joinable())
                    {
                        if(current.code == op_t::spawn_join_all)
                        {
                            try
                            {
                                co_await pool.join_all();
                            }
                            catch(const ::verilator_utils::spawn_pool::join_all_exception& exception)
                            {
                                ::check_pool_exceptions(exception);
                            }
                        }
                        else if(current.code == op_t::spawn_join_any)
                        {
                            // 追加一个立即完成的子任务，使join_any走"子任务已就绪"的快速路径
                            pool.add_task(::run_ops(prog, ::empty_ops, ctx, depth + 1));
                            co_await ::verilator_utils::wait_eval_stage(
                                *ctx.scheduler,
                                ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval);
                            try
                            {
                                co_await pool.join_any();
                            }
                            catch(const ::verilator_utils::subtask_cancel_exception&)
                            {
                                ++*ctx.canceled;
                            }
                        }
                        else
                        {
                            co_await pool.join_none();
                        }
                    }
                    break;
                }
                case op_t::async_subtask:  // 异步子任务，可能被取消
                {
                    if(depth >= ::max_depth) { break; }
                    auto child{co_await ::verilator_utils::to_async(
                        ::run_ops(prog, prog.subtasks[current.a % prog.subtasks.size()], ctx, depth + 1))};
                    ::fuzz::check(static_cast<bool>(child));
                    ::fuzz::check(!child.done());
                    if((current.b & 4u) != 0u)
                    {
                        // 让子任务先运行并挂起，使其进入suspended状态后再取消
                        co_await ::verilator_utils::wait_eval_stage(
                            *ctx.scheduler,
                            ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval);
                    }
                    // 尚未运行或已挂起的任务可以取消，取消后应当报告收到了取消请求
                    if((current.b & 1u) != 0 && child.cancel_possible())
                    {
                        child.cancel();
                        ::fuzz::check(child.cancel_requested());
                    }
                    try
                    {
                        co_await child;
                    }
                    catch(const ::verilator_utils::subtask_cancel_exception&)
                    {
                        ++*ctx.canceled;
                    }
                    break;
                }
                case op_t::add_task:  // 直接向调度器添加任务
                    if(depth >= ::max_depth) { break; }
                    co_await ::verilator_utils::add_task(
                        ::run_ops(prog, prog.subtasks[(current.a + 5u) % prog.subtasks.size()], ctx, depth + 1));
                    break;
                case op_t::semaphore_get:  // 获取信号量
                    co_await (*ctx.semaphores)[current.a % ctx.semaphores->size()].get(current.a % 3u + 1u);
                    break;
                case op_t::semaphore_put:  // 释放信号量
                    (*ctx.semaphores)[current.a % ctx.semaphores->size()].put(current.a % 3u + 1u);
                    break;
                case op_t::signal_edge:  // 信号边沿与时间读取
                {
                    const ::verilator_utils::bit_slice<::CData> bit{*ctx.signal, 0};
                    switch(::fuzz::decode_opcode<signal_op_t>(current.b))
                    {
                        case signal_op_t::toggle: *ctx.signal = static_cast<::CData>(*ctx.signal == 0 ? 1 : 0); break;
                        case signal_op_t::posedge: co_await ::verilator_utils::wait_posedge(bit); break;
                        case signal_op_t::negedge: co_await ::verilator_utils::wait_negedge(bit); break;
                        case signal_op_t::alledge: co_await ::verilator_utils::wait_alledge(bit); break;
                        case signal_op_t::check_time:
                        case signal_op_t::check_time_extra:
                            ::check_time(ctx);
                            co_await ::verilator_utils::wait_eval_stage(
                                *ctx.scheduler,
                                ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval);
                            break;
                        case signal_op_t::total_ops: ::std::unreachable();  // 取模映射不会产生该取值
                    }
                    break;
                }
                case op_t::time_query:  // 时间读取的可等待体形式
                {
                    const auto time_string{co_await ::verilator_utils::time_in_string()};
                    const auto time_unit{co_await ::verilator_utils::time_in_time_unit()};
                    const auto time_precision{co_await ::verilator_utils::time_in_time_precision()};
                    ::fuzz::check(!time_string.empty());
                    ::fuzz::check(time_unit >= 0.0);
                    ::fuzz::check(time_precision == static_cast<::std::uint64_t>(ctx.scheduler->time_in_time_precision()));
                    ::check_time(ctx);
                    break;
                }
                case op_t::stacktrace:  // 协程栈回溯与格式化
                {
                    const auto trace{co_await ::verilator_utils::stacktrace()};
                    ::fuzz::check(!trace.empty());
                    ::fuzz::check(!trace.frames.empty());
                    ::fuzz::check(trace.frames.front().location.line() != 0);
                    const auto plain{::std::format("{}", trace)};
                    const auto detailed{::std::format("{:#}", trace)};
                    ::fuzz::check(!plain.empty() && !detailed.empty());
                    ::fuzz::check(::std::format("{}", trace.frames.front()) != ::std::format("{:#}", trace.frames.front()));
                    break;
                }
                case op_t::clock_reset:  // 时钟与复位激励
                {
                    // 周期取时间精度的整数倍，保证占空比拆分后的高电平时间不小于一个时间精度
                    const auto period{::verilator_utils::femtosecond_t{(current.a % 8u + 1u) * 4zu * ctx.precision_fs}};
                    const auto duration{::verilator_utils::femtosecond_t{(current.a % 5u + 1u) * ctx.precision_fs}};
                    const auto active_high{(current.b & 1u) == 0u};
                    switch(::fuzz::decode_opcode<clock_op_t>(current.b))
                    {
                        case clock_op_t::generate_clock:
                            // 时钟激励会一直运行，只需启动一次
                            if((current.b & 4u) != 0u)
                            {
                                // 占空比越界：校验在协程首次恢复时抛出，不会进入无限循环
                                const auto duty_ratio{(current.b & 8u) != 0u ? 1.0 : 0.0};
                                try
                                {
                                    co_await ::verilator_utils::generate_clock(
                                        *ctx.clk,
                                        period,
                                        ::verilator_utils::femtosecond_t{current.a % 3u * ctx.precision_fs},
                                        duty_ratio);
                                    ::fuzz::fail("invalid duty ratio was accepted");
                                }
                                catch(const ::verilator_utils::assertion_error& error)
                                {
                                    ::inspect_assertion_error(error);
                                }
                                break;
                            }
                            if(!ctx.clock_started)
                            {
                                ctx.clock_started = true;
                                ctx.scheduler->add_task(::verilator_utils::generate_clock(
                                    *ctx.clk,
                                    period,
                                    ::verilator_utils::femtosecond_t{current.a % 3u * ctx.precision_fs},
                                    0.25 + static_cast<double>(current.b % 3u) * 0.25));
                            }
                            break;
                        case clock_op_t::generate_reset:
                            ctx.scheduler->add_task(
                                ::verilator_utils::generate_reset(*ctx.rst, *ctx.clk, current.a % 4u + 1u, active_high));
                            break;
                        case clock_op_t::generate_async_reset:
                            ctx.scheduler->add_task(::verilator_utils::generate_async_reset(*ctx.rst, duration, active_high));
                            break;
                        case clock_op_t::wait_reset_finish:
                            co_await ::verilator_utils::wait_reset_finish(*ctx.rst, active_high);
                            break;
                        case clock_op_t::total_ops: ::std::unreachable();  // 取模映射不会产生该取值
                    }
                    break;
                }
                case op_t::verify_stimulate:  // 验证与激励时机
                {
                    const auto edge{(current.a & 1u) != 0u ? ::verilator_utils::edge_enum::rising
                                                           : ::verilator_utils::edge_enum::falling};
                    const auto edge_to_wait{static_cast<::std::size_t>(current.a % 3zu + 1zu)};
                    switch(::fuzz::decode_opcode<verify_op_t>(current.b))
                    {
                        case verify_op_t::wait_verify:
                            co_await ::verilator_utils::wait_verify(
                                *ctx.clk,
                                edge_to_wait,
                                edge,
                                ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval);
                            break;
                        case verify_op_t::wait_stimulate:
                            // 显式指定评估阶段时不再按边沿极性推导
                            co_await ::verilator_utils::wait_stimulate(
                                *ctx.clk,
                                edge_to_wait,
                                edge,
                                (current.b & 2u) != 0u ? ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval
                                                       : ::verilator_utils::eval_scheduler::eval_stage_enum::before_dut_eval);
                            break;
                        case verify_op_t::verify_at_clock:
                            co_await ::verilator_utils::verify_at(
                                *ctx.clk,
                                ::verilator_utils::default_event_callback{[&ctx] { return static_cast<bool>(*ctx.signal); }},
                                [] {});
                            break;
                        case verify_op_t::verify_at_reset: co_await ::verilator_utils::verify_at(*ctx.rst, [] {}); break;
                        case verify_op_t::total_ops: ::std::unreachable();  // 取模映射不会产生该取值
                    }
                    break;
                }
                case op_t::select_clock:  // select_clock与边沿检测器接口
                {
                    const auto edge{(current.a & 1u) != 0u ? ::verilator_utils::edge_enum::rising
                                                           : ::verilator_utils::edge_enum::falling};
                    ::verilator_utils::edge_detector detector{*ctx.clk, edge};
                    detector.set_edge_to_detect(edge);
                    ::fuzz::check(detector.edge_to_detect() == edge);

                    // 翻转信号前后构造的检测器必然检测到双边沿
                    const ::verilator_utils::bit_slice<::CData> bit{*ctx.signal, 0};
                    ::verilator_utils::edge_detector any_edge{bit, ::verilator_utils::edge_enum::both};
                    *ctx.signal = static_cast<::CData>(*ctx.signal == 0 ? 1 : 0);
                    ::fuzz::check(any_edge());

                    ::verilator_utils::select_clock selector{};
                    selector.add_clock(*ctx.clk, ::verilator_utils::edge_enum::rising);
                    selector.add_clock(bit, edge);
                    auto triggered{co_await selector};
                    ::fuzz::check(::std::ranges::distance(triggered) == 2);
                    break;
                }
                case op_t::mailbox:  // 邮箱读写
                {
                    auto& mailbox{(*ctx.mailboxes)[current.a % ctx.mailboxes->size()]};
                    switch(::fuzz::decode_opcode<mailbox_op_t>(current.b))
                    {
                        case mailbox_op_t::put: co_await mailbox.put(current.a); break;
                        case mailbox_op_t::get: co_await mailbox.get(); break;
                        case mailbox_op_t::peek: co_await mailbox.peek(); break;
                        case mailbox_op_t::try_get:
                        {
                            const auto before{mailbox.num()};
                            const auto got{mailbox.try_get()};
                            ::fuzz::check(mailbox.num() == before - (got.has_value() ? 1zu : 0zu));
                            break;
                        }
                        case mailbox_op_t::try_put:
                        {
                            const auto before{mailbox.num()};
                            const auto accepted{mailbox.try_put(current.a)};
                            ::fuzz::check(mailbox.num() == before + (accepted ? 1zu : 0zu));
                            break;
                        }
                        case mailbox_op_t::recycle:
                        {
                            // 无限容量邮箱：批量读写以触发空闲存储的回收路径，
                            // 容量必须超过收缩水位线才会重建缓冲区
                            auto& infinite{(*ctx.mailboxes)[0]};
                            const auto count{static_cast<::std::size_t>(2049zu + current.a % 64zu)};
                            for(auto i{0zu}; i != count; ++i) { (void)infinite.try_put(i); }
                            for(auto i{0zu}; i != count; ++i) { ::fuzz::check(infinite.try_get().has_value()); }
                            break;
                        }
                        case mailbox_op_t::try_peek:
                        {
                            const auto before{mailbox.num()};
                            const auto got{mailbox.try_peek()};
                            ::fuzz::check(got.has_value() == (before != 0zu));
                            ::fuzz::check(!::std::format("{}", mailbox).empty());
                            ::fuzz::check(!::std::format("{:#}", mailbox).empty());
                            break;
                        }
                        case mailbox_op_t::total_ops: ::std::unreachable();  // 取模映射不会产生该取值
                    }
                    break;
                }
                case op_t::lfsr:  // LFSR生成器
                    ::check_lfsr(current);
                    break;
                case op_t::max_eval_time:  // 最大仿真时间：触发仿真超时退出
                {
                    const auto duration{::verilator_utils::femtosecond_t{(current.a % 50u + 1u) * ctx.precision_fs}};
                    try
                    {
                        co_await ::verilator_utils::max_eval_time(duration);
                        ::fuzz::fail("max_eval_time did not time out");
                    }
                    catch(const ::verilator_utils::eval_timeout_exception& error)
                    {
                        ::fuzz::check(::std::string_view{error.what()} == "仿真超时退出"sv);
                        ++*ctx.timeouts;
                    }
                    ::check_time(ctx);
                    break;
                }
                case op_t::nonblocking:  // 非阻塞接口与调度器状态查询
                {
                    switch(::fuzz::decode_opcode<nonblocking_op_t>(current.b))
                    {
                        case nonblocking_op_t::semaphore_try_get:
                        {
                            auto& semaphore{(*ctx.semaphores)[current.a % ctx.semaphores->size()]};
                            const auto update{static_cast<::std::size_t>(current.a % 3u + 1u)};
                            if(semaphore.try_get(update)) { semaphore.put(update); }
                            break;
                        }
                        case nonblocking_op_t::event_shrink: (*ctx.events)[current.a % ctx.events->size()].shrink_to_fit(); break;
                        case nonblocking_op_t::empty_pool:
                        {
                            auto pool{co_await ::verilator_utils::spawn_pool()};
                            ::fuzz::check(pool.empty());
                            ::fuzz::check(!pool.joinable());
                            // 空任务池不可join，框架应当拒绝该操作
                            try
                            {
                                co_await pool.join_all();
                                ::fuzz::fail("join_all accepted an empty pool");
                            }
                            catch(const ::verilator_utils::assertion_error& error)
                            {
                                ::inspect_assertion_error(error);
                            }
                            break;
                        }
                        case nonblocking_op_t::assertion_reporting: ::check_assertion_reporting(current.a); break;
                        case nonblocking_op_t::pool_exceptions:
                        {
                            // 任务池中的非标准异常必须被汇总并通过join_all_exception报告
                            auto pool{co_await ::verilator_utils::spawn_pool()};
                            pool.add_task(::run_probe_error(ctx, static_cast<int>(current.a % 7u)));
                            // 框架断言失败派生自std::exception，走消息复制的报告路径
                            pool.add_task(::run_assertion_error(ctx, current.a));
                            bool reported{};
                            try
                            {
                                co_await pool.join_all();
                            }
                            catch(const ::verilator_utils::spawn_pool::join_all_exception& error)
                            {
                                reported = true;
                                ::fuzz::check(!::std::string_view{error.what()}.empty());
                                ::fuzz::check(error.exceptions().size() == 2zu);
                                ::fuzz::check(::is_probe_error(error.exceptions().front()));
                                ::fuzz::check(::is_assertion_failure(error.exceptions().back()));
                            }
                            ::fuzz::check(reported);
                            break;
                        }
                        case nonblocking_op_t::root_exception:  // 从根协程抛出异常：覆盖根协程的异常传播与coroutine_exception
                            // 子协程抛出的异常可能无人观察，调度器析构时会终止进程，因此只让根协程抛出
                            if(depth != 0zu) { break; }
                            // 异常类型由a选择：b的取值已用于分派本用例
                            if((current.a & 4u) != 0u)
                            {
                                // 直接抛出携带协程信息的异常：传播时不应再次包装
                                throw ::verilator_utils::coroutine_exception{
                                    ::std::make_exception_ptr(::probe_error{.value = static_cast<int>(current.a % 7u)})};
                            }
                            if((current.a & 2u) != 0u) { ::verilator_utils::check{}(false, "模糊测试触发的断言{}"sv, current.a); }
                            // NOLINTNEXTLINE(bugprone-std-exception-baseclass)
                            throw ::probe_error{.value = static_cast<int>(current.a % 7u)};
                        case nonblocking_op_t::misc:  // 杂项：协程状态对比较、时间配置校验与格式化路径
                        {
                            // 子协程可能被提前回收，只在根协程中执行
                            if(depth != 0zu) { break; }
                            switch(::fuzz::decode_opcode<misc_op_t>(current.b))
                            {
                                case misc_op_t::coroutine_pair:
                                {
                                    if(depth >= ::max_depth) { break; }
                                    auto left{co_await ::verilator_utils::to_async(
                                        ::run_ops(prog, prog.subtasks[current.a % prog.subtasks.size()], ctx, depth + 1))};
                                    auto right{co_await ::verilator_utils::to_async(
                                        ::run_ops(prog, prog.subtasks[(current.a + 1u) % prog.subtasks.size()], ctx, depth + 1))};
                                    const ::verilator_utils::detail::coroutine_pair left_pair{left.handle()};
                                    const ::verilator_utils::detail::coroutine_pair right_pair{right.handle()};
                                    const ::verilator_utils::detail::coroutine_pair empty_pair{};
                                    ::fuzz::check(left_pair != right_pair);
                                    ::fuzz::check((left_pair <=> right_pair) != 0);
                                    ::fuzz::check(!(left_pair == nullptr));
                                    ::fuzz::check(!(right_pair == nullptr));
                                    ::fuzz::check(empty_pair == nullptr);
                                    left.cancel();
                                    right.cancel();
                                    try
                                    {
                                        co_await left;
                                    }
                                    catch(const ::verilator_utils::subtask_cancel_exception&)
                                    {
                                        ++*ctx.canceled;
                                    }
                                    try
                                    {
                                        co_await right;
                                    }
                                    catch(const ::verilator_utils::subtask_cancel_exception&)
                                    {
                                        ++*ctx.canceled;
                                    }
                                    break;
                                }
                                case misc_op_t::time_config: ::check_time_config_rejection(current.a); break;
                                case misc_op_t::stacktrace_format:
                                {
                                    const auto trace{co_await ::verilator_utils::stacktrace()};
                                    ::fuzz::check(!trace.frames.empty());
                                    ::fuzz::check(!::std::format("{}", trace).empty());
                                    // doctest的值转换接口
                                    ::fuzz::check(
                                        ::doctest::StringMaker<::verilator_utils::coroutine_stacktrace>::convert(trace).size() !=
                                        0zu);
                                    ::fuzz::check(::doctest::StringMaker<::verilator_utils::coroutine_stacktrace::frame>::convert(
                                                      trace.frames.front())
                                                      .size() != 0zu);
                                    // 协程类型枚举的格式化
                                    using coroutine_type = ::verilator_utils::detail::promise_base::coroutine_type_enum;
                                    const auto type{static_cast<coroutine_type>(current.a % 3u)};
                                    ::fuzz::check(!::std::format("{}", type).empty());
                                    // 空栈回溯与手工构造的栈帧：覆盖帧编号宽度的空值分支与三参数帧构造
                                    const ::verilator_utils::coroutine_stacktrace empty_trace{};
                                    ::fuzz::check(!::std::format("{}", empty_trace).empty());
                                    ::fuzz::check(!::std::format("{:#}", empty_trace).empty());
                                    ::verilator_utils::coroutine_stacktrace manual{};
                                    manual.frames.emplace_back(nullptr,
                                                               ::std::source_location::current(),
                                                               coroutine_type::sub_coroutine);
                                    ::fuzz::check(!::std::format("{}", manual).empty());
                                    // 非法格式符与未闭合的格式串必须被格式化器拒绝
                                    try
                                    {
                                        (void)::std::vformat("{:x}", ::std::make_format_args(trace, type));
                                        ::fuzz::fail("invalid format specifier was accepted");
                                    }
                                    catch(const ::std::format_error& error)
                                    {
                                        ::fuzz::check(!::std::string_view{error.what()}.empty());
                                    }
                                    try
                                    {
                                        (void)::std::vformat("{:#", ::std::make_format_args(trace));
                                        ::fuzz::fail("unterminated format string was accepted");
                                    }
                                    catch(const ::std::format_error& error)
                                    {
                                        ::fuzz::check(!::std::string_view{error.what()}.empty());
                                    }
                                    break;
                                }
                                case misc_op_t::unawaitable_stage:
                                {
                                    // 不可等待的评估阶段必须被拒绝
                                    try
                                    {
                                        co_await ::verilator_utils::wait_eval_stage(
                                            *ctx.scheduler,
                                            ::verilator_utils::eval_scheduler::eval_stage_enum::eval_end);
                                        ::fuzz::fail("unawaitable eval stage was accepted");
                                    }
                                    catch(const ::verilator_utils::assertion_error& error)
                                    {
                                        ::inspect_assertion_error(error);
                                    }
                                    break;
                                }
                                case misc_op_t::coroutine_exception:
                                {
                                    // 用真实的协程栈回溯构造协程异常，覆盖带栈帧的消息组合路径
                                    const auto trace{co_await ::verilator_utils::stacktrace()};
                                    ::fuzz::check(!trace.empty());
                                    const ::verilator_utils::coroutine_exception error{
                                        ::std::make_exception_ptr(::probe_error{.value = static_cast<int>(current.a % 7u)}),
                                        trace};
                                    ::fuzz::check(!error.stacktrace().empty());
                                    ::fuzz::check(::std::string{error.what()}.contains("Coroutine stack trace"));
                                    break;
                                }
                                case misc_op_t::total_ops: ::std::unreachable();  // 取模映射不会产生该取值
                            }
                            break;
                        }
                        case nonblocking_op_t::check_time: ::check_time(ctx); break;
                        case nonblocking_op_t::total_ops: ::std::unreachable();  // 取模映射不会产生该取值
                    }
                    break;
                }
                case op_t::bulk_waiter:  // 批量等待者：触发挂起队列的前缀回收与收缩路径
                {
                    // 只有大规模输入才能触及挂起队列的水位线，小规模输入用于保持模糊测试的吞吐
                    const bool bulk{(current.b & 2u) != 0u};
                    const auto count{bulk ? static_cast<::std::size_t>(1100zu + current.a % 200zu)
                                          : static_cast<::std::size_t>(1zu + current.a % 24zu)};
                    // 等待者会在本次操作返回后继续存活，因此索引必须按值捕获
                    if((current.b & 1u) != 0u)
                    {
                        const auto index{static_cast<::std::size_t>(current.a % ctx.events->size())};
                        const auto waiter{[&ctx, index](this auto) -> ::verilator_utils::task<void> {
                            ++*ctx.created;
                            const ::frame_destruction_counter counter{ctx.destroyed};
                            co_await (*ctx.events)[index];
                        }};
                        for(auto i{0zu}; i != count; ++i) { ctx.scheduler->add_task(waiter()); }
                        // 等待者需要先运行并挂起到事件队列上，唤醒才有意义
                        co_await ::verilator_utils::wait_eval_stage(
                            *ctx.scheduler,
                            ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval);
                        if(bulk && (current.a & 1u) != 0u)
                        {
                            // 一次唤醒全部等待者：触发事件的空闲容量回收
                            (*ctx.events)[index].notify_all();
                        }
                        else
                        {
                            // 逐个唤醒：触发事件挂起队列的前缀回收
                            for(auto i{0zu}; i != count; ++i) { (*ctx.events)[index].notify_one(); }
                        }
                    }
                    else
                    {
                        const auto semaphore_index{static_cast<::std::size_t>(current.a % ctx.semaphores->size())};
                        const auto waiter{[&ctx, semaphore_index](this auto) -> ::verilator_utils::task<void> {
                            ++*ctx.created;
                            const ::frame_destruction_counter counter{ctx.destroyed};
                            co_await (*ctx.semaphores)[semaphore_index].get(1zu);
                        }};
                        for(auto i{0zu}; i != count; ++i) { ctx.scheduler->add_task(waiter()); }
                        // 等待者需要先运行并挂起到信号量队列上，发放才有意义
                        co_await ::verilator_utils::wait_eval_stage(
                            *ctx.scheduler,
                            ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval);
                        (*ctx.semaphores)[semaphore_index].put(count);
                    }
                    break;
                }
                case op_t::total_ops: ::std::unreachable();  // 取模映射不会产生该取值
            }
        }
    }

    /// 抛出探针异常的任务：用于检查任务池汇总异常的报告路径
    ::verilator_utils::task<void> run_probe_error(const ::context& ctx, int value)
    {
        ++*ctx.created;
        const ::frame_destruction_counter counter{ctx.destroyed};
        // 刻意抛出非标准异常类型，用于覆盖汇总异常中未知异常名称的报告路径
        // NOLINTNEXTLINE(bugprone-std-exception-baseclass)
        throw ::probe_error{.value = value};
        co_return;  // 协程体必须包含协程关键字，该语句不会被执行
    }

    /// 抛出框架断言失败的任务：用于检查任务池汇总异常对std::exception派生异常的报告路径
    ::verilator_utils::task<void> run_assertion_error(const ::context& ctx, ::std::uint64_t detail)
    {
        ++*ctx.created;
        const ::frame_destruction_counter counter{ctx.destroyed};
        ::verilator_utils::check{}(false, "模糊测试触发的断言{}"sv, detail);
        co_return;  // 协程体必须包含协程关键字，该语句不会被执行
    }

    /// 看门狗任务：有限时间后结束仿真
    ::verilator_utils::task<void> run_watchdog(const ::context& ctx, ::std::uint64_t delay)
    {
        ++*ctx.created;
        const ::frame_destruction_counter counter{ctx.destroyed};
        co_await ::verilator_utils::wait_time(::verilator_utils::femtosecond_t{delay * ctx.precision_fs});
        co_await ::verilator_utils::eval_finish();
    }

    /// 同步体与调度器的析构顺序
    enum class destruction_order : ::std::uint8_t
    {
        /// 同步体先析构：仿真结束时同步体把挂起任务交回调度器
        sync_object_first,
        /// 调度器先析构：调度器析构时回收挂起任务并解除同步体的绑定
        scheduler_first,
    };

    /// 共享的同步对象与激励信号
    struct sync_objects
    {
        ::std::array<::verilator_utils::event, 4> events{};
        ::std::array<::verilator_utils::semaphore, 2> semaphores{::verilator_utils::semaphore{0},
                                                                 ::verilator_utils::semaphore{2}};
        ::std::array<::verilator_utils::mailbox<::std::uint64_t>, 2> mailboxes{::verilator_utils::mailbox<::std::uint64_t>{0},
                                                                               ::verilator_utils::mailbox<::std::uint64_t>{4}};
        ::CData signal{};
        ::CData clk_signal{};
        ::CData rst_signal{};
        // 时钟与复位切片被生成任务长期引用，因此必须与信号一同存放
        ::verilator_utils::bit_slice<::CData> clk{clk_signal, 0};
        ::verilator_utils::bit_slice<::CData> rst{rst_signal, 0};
    };

    /// 运行仿真，直到看门狗结束仿真
    void drive_simulation(const ::program& prog,
                          ::verilator_utils::eval_scheduler& scheduler,
                          ::context& ctx,
                          ::sync_objects& objects,
                          ::std::size_t& created,
                          ::std::size_t& destroyed,
                          ::std::size_t& canceled,
                          ::std::size_t& timeouts,
                          ::std::size_t& coroutine_errors,
                          bool use_initial_eval,
                          ::std::uint64_t watchdog_delay)
    {
        ctx = ::context{.events = &objects.events,
                        .semaphores = &objects.semaphores,
                        .mailboxes = &objects.mailboxes,
                        .clk = &objects.clk,
                        .rst = &objects.rst,
                        .signal = &objects.signal,
                        .scheduler = &scheduler,
                        .created = &created,
                        .destroyed = &destroyed,
                        .canceled = &canceled,
                        .timeouts = &timeouts,
                        .precision_fs = 1000zu,
                        .max_time = 0zu};

        // 看门狗保证仿真一定结束
        scheduler.add_task(::run_watchdog(ctx, watchdog_delay));
        for(const auto& root: prog.roots) { scheduler.add_task(::run_ops(prog, root, ctx, 0zu)); }

        try
        {
            // 初始化评估会立即执行就绪队列中的任务，直到它们挂起
            if(use_initial_eval) { scheduler.initial_eval(); }
            scheduler.loop_until_finish();
        }
        catch(const ::verilator_utils::eval_finish_exception&)
        {
            // 仿真正常结束时的协作式取消：看门狗已调用eval_finish
            ++canceled;
        }
        catch(const ::verilator_utils::coroutine_exception& error)
        {
            // 根协程抛出的非取消异常：检查协程异常的报告与原始异常访问路径
            ::check_coroutine_exception(error);
            ++coroutine_errors;
            scheduler.finish();
        }
        // 无论是否经由异常退出，看门狗都保证仿真已经结束
        ::fuzz::check(scheduler.is_finish());
        ctx.error_seen = ctx.error_seen || scheduler.is_error();
    }

    /// 执行一个模糊测试程序
    void run_program(const ::program& prog,
                     ::std::uint64_t watchdog_delay,
                     ::destruction_order order,
                     bool use_initial_eval,
                     bool force_colors,
                     bool no_colors)
    {
        // 断言消息的颜色配置由输入决定：覆盖彩色输出与强制非彩色输出两条路径
        ::verilator_utils::set_assertion_config({.force_colors = force_colors, .no_colors = no_colors, .stacktrace = false});

        // 框架中不抛出的运行时校验通过与doctest的集成点报告失败，这里提供测试上下文使报告可读
        static ::doctest::Context doctest_context{};
        static const bool doctest_configured{[] {
            doctest_context.setAsDefaultForAssertsOutOfTestCases();
            return true;
        }()};
        ::fuzz::check(doctest_configured);

        ::std::size_t created{};
        ::std::size_t destroyed{};
        ::std::size_t canceled{};
        ::std::size_t timeouts{};
        ::std::size_t coroutine_errors{};

        ::scheduler_fixture fixture{};
        // 调度器析构时会恢复挂起的任务，这些任务仍会访问上下文，
        // 因此上下文必须比调度器长寿；同步体的析构顺序仍由输入决定
        ::context ctx{};
        // 两种析构顺序都应当安全，由输入决定本次使用哪一种
        if(order == ::destruction_order::scheduler_first)
        {
            // 同步体比调度器长寿：调度器析构时回收全部挂起任务并向同步体解除绑定
            ::sync_objects objects{};
            {
                auto scheduler{fixture.make_scheduler()};
                ::drive_simulation(prog,
                                   scheduler,
                                   ctx,
                                   objects,
                                   created,
                                   destroyed,
                                   canceled,
                                   timeouts,
                                   coroutine_errors,
                                   use_initial_eval,
                                   watchdog_delay);
            }
        }
        else
        {
            // 同步体比调度器先析构：仿真结束时同步体把挂起任务交回调度器
            auto scheduler{fixture.make_scheduler()};
            {
                ::sync_objects objects{};
                ::drive_simulation(prog,
                                   scheduler,
                                   ctx,
                                   objects,
                                   created,
                                   destroyed,
                                   canceled,
                                   timeouts,
                                   coroutine_errors,
                                   use_initial_eval,
                                   watchdog_delay);
            }
        }

        // 每个协程帧都必须恰好回收一次
        ::fuzz::check(created == destroyed, created);
    }

    int do_fuzz(const ::std::uint8_t* data, ::std::size_t size)
    {
        if(size < 2) { return 0; }
        ::fuzz::reader input{data, size};

        ::program prog{};
        for(auto& root: prog.roots) { root = ::decode_ops(input, 5zu); }
        for(auto& subtask: prog.subtasks) { subtask = ::decode_ops(input, 4zu); }
        const auto watchdog_delay{input.range(5, 300)};
        const auto order{input.boolean() ? ::destruction_order::scheduler_first : ::destruction_order::sync_object_first};
        const auto force_colors{input.boolean()};
        const auto no_colors{input.boolean()};
        ::run_program(prog, watchdog_delay, order, input.boolean(), force_colors, no_colors);
        return 0;
    }
}  // namespace

TEST_SUITE("verilator_utils/fuzz")
{
    TEST_CASE("scheduler") { ::fuzz::run_fuzz_test(&::do_fuzz); }
}
