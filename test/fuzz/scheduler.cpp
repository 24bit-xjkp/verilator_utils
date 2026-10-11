#include <doctest_macros.hpp>
import fuzz_support;
import unit_test;

namespace
{
    /// 任务创建类操作的最大嵌套深度，避免模糊测试输入构造无界递归
    constexpr ::std::size_t max_depth{2zu};

    /// 单个操作：操作码与两个参数
    struct op
    {
        ::std::uint8_t code{};
        ::std::uint64_t a{};
        ::std::uint64_t b{};
    };

    using op_list = ::std::vector<op>;

    /// 一个模糊测试程序：根任务与可复用的子任务操作序列
    struct program
    {
        ::std::array<op_list, 4> roots{};
        ::std::array<op_list, 8> subtasks{};
    };

    /// 从输入中解码一段操作序列
    [[nodiscard]] op_list decode_ops(::fuzz::reader& input, ::std::size_t max_count)
    {
        const auto count{static_cast<::std::size_t>(input.range(0, max_count))};
        op_list ops{};
        ops.reserve(count);
        for(::std::size_t i{}; i != count; ++i) { ops.push_back(op{.code = input.u8(), .a = input.u64(), .b = input.uint(2)}); }
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
        run_ops(const program& prog, const op_list& ops, context& ctx, ::std::size_t depth);

    /// 抛出探针异常的任务：用于检查任务池汇总异常的报告路径
    ::verilator_utils::task<void> run_probe_error(const context& ctx, int value);

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
        catch(const probe_error&)
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

    /// 检查任务池汇总异常中只包含取消类异常与探针异常
    void check_pool_exceptions(const ::verilator_utils::spawn_pool::join_all_exception& exception)
    {
        ::fuzz::check(!::std::string_view{exception.what()}.empty());
        for(const auto& exception_ptr: exception.exceptions())
        {
            if(!is_cancellation(exception_ptr) && !is_probe_error(exception_ptr))
            {
                ::fuzz::fail("pool subtask threw an unexpected exception");
            }
        }
    }

    /// 检查时间读取的一致性与单调性
    void check_time(context& ctx)
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
            inspect_assertion_error(error);
        }

        try
        {
            ::verilator_utils::check{}(false);
            ::fuzz::fail("framework check did not fail");
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            inspect_assertion_error(error);
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
        for(::std::size_t i{}; i != count && iter != ::std::default_sentinel; ++i, ++iter)
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
    void check_lfsr(const op& current)
    {
        const auto width{static_cast<::std::size_t>(3zu + current.a % 62zu)};
        const auto width_mask{width == 64zu ? ~::std::uint64_t{} : (::std::uint64_t{1zu} << width) - 1zu};
        const auto initial_value{(current.a & width_mask) | 1zu};
        // 部分输入使用合法的反馈表达式覆盖生成逻辑，其余用于覆盖参数校验的拒绝路径
        const auto feedback_mask{(current.b % 4u == 0u) ? ((current.b & width_mask) | 1zu) : current.b};
        try
        {
            if((current.a & 1zu) != 0u)
            {
                auto generator{::verilator_utils::galois_lfsr_generator(width, feedback_mask, initial_value)};
                auto reference{::verilator_utils::galois_lfsr_generator(width, feedback_mask, initial_value)};
                ::fuzz::check(consume_generator(generator, 32zu) == consume_generator(reference, 32zu));
            }
            else
            {
                auto generator{::verilator_utils::fibonacci_lfsr_generator(width, feedback_mask, initial_value)};
                auto reference{::verilator_utils::fibonacci_lfsr_generator(width, feedback_mask, initial_value)};
                ::fuzz::check(consume_generator(generator, 32zu) == consume_generator(reference, 32zu));
            }
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            inspect_assertion_error(error);
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
    ::verilator_utils::task<void> run_ops(const program& prog, const op_list& ops, context& ctx, ::std::size_t depth)
    {
        // 在协程体开始时计数：被取消而未启动的协程帧不会构造任何局部对象
        ++*ctx.created;
        const ::frame_destruction_counter counter{ctx.destroyed};
        for(const auto& current: ops)
        {
            switch(current.code % 22u)
            {
                case 0:  // 等待一段时间
                    co_await ::verilator_utils::wait_time(
                        ::verilator_utils::femtosecond_t{(current.a % 20u + 1u) * ctx.precision_fs});
                    break;
                case 1:  // 等待事件
                    co_await (*ctx.events)[current.a % ctx.events->size()];
                    break;
                case 2:  // 唤醒所有等待者
                    (*ctx.events)[current.a % ctx.events->size()].notify_all();
                    break;
                case 3:  // 按FIFO唤醒一个等待者
                    (*ctx.events)[current.a % ctx.events->size()].notify_one();
                    break;
                case 4:  // 任务池：等待所有子任务
                case 5:  // 任务池：等待任一子任务
                case 6:  // 任务池：托管所有子任务
                {
                    if(depth >= max_depth) { break; }
                    auto pool{co_await ::verilator_utils::spawn_pool()};
                    const auto count{static_cast<::std::size_t>(current.b % 3u)};
                    for(::std::size_t i{}; i != count; ++i)
                    {
                        pool.add_task(run_ops(prog, prog.subtasks[(current.a + i) % prog.subtasks.size()], ctx, depth + 1));
                    }
                    if(pool.joinable())
                    {
                        if(current.code % 22u == 4)
                        {
                            try
                            {
                                co_await pool.join_all();
                            }
                            catch(const ::verilator_utils::spawn_pool::join_all_exception& exception)
                            {
                                check_pool_exceptions(exception);
                            }
                        }
                        else if(current.code % 22u == 5)
                        {
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
                case 7:  // 异步子任务，可能被取消
                {
                    if(depth >= max_depth) { break; }
                    auto child{co_await ::verilator_utils::to_async(
                        run_ops(prog, prog.subtasks[current.a % prog.subtasks.size()], ctx, depth + 1))};
                    ::fuzz::check(static_cast<bool>(child));
                    ::fuzz::check(!child.done());
                    // 尚未运行的任务总是可以取消，取消后应当报告收到了取消请求
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
                case 8:  // 直接向调度器添加任务
                    if(depth >= max_depth) { break; }
                    co_await ::verilator_utils::add_task(
                        run_ops(prog, prog.subtasks[(current.a + 5u) % prog.subtasks.size()], ctx, depth + 1));
                    break;
                case 9:  // 获取信号量
                    co_await (*ctx.semaphores)[current.a % ctx.semaphores->size()].get(current.a % 3u + 1u);
                    break;
                case 10:  // 释放信号量
                    (*ctx.semaphores)[current.a % ctx.semaphores->size()].put(current.a % 3u + 1u);
                    break;
                case 11:  // 信号边沿与时间读取
                {
                    const ::verilator_utils::bit_slice<::CData> bit{*ctx.signal, 0};
                    switch(current.b % 6u)
                    {
                        case 0: *ctx.signal = static_cast<::CData>(*ctx.signal == 0 ? 1 : 0); break;
                        case 1: co_await ::verilator_utils::wait_posedge(bit); break;
                        case 2: co_await ::verilator_utils::wait_negedge(bit); break;
                        case 3: co_await ::verilator_utils::wait_alledge(bit); break;
                        default:
                            check_time(ctx);
                            co_await ::verilator_utils::wait_eval_stage(
                                *ctx.scheduler,
                                ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval);
                            break;
                    }
                    break;
                }
                case 12:  // 时间读取的可等待体形式
                {
                    const auto time_string{co_await ::verilator_utils::time_in_string()};
                    const auto time_unit{co_await ::verilator_utils::time_in_time_unit()};
                    const auto time_precision{co_await ::verilator_utils::time_in_time_precision()};
                    ::fuzz::check(!time_string.empty());
                    ::fuzz::check(time_unit >= 0.0);
                    ::fuzz::check(time_precision == static_cast<::std::uint64_t>(ctx.scheduler->time_in_time_precision()));
                    check_time(ctx);
                    break;
                }
                case 13:  // 协程栈回溯与格式化
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
                case 14:  // 时钟与复位激励
                {
                    // 周期取时间精度的整数倍，保证占空比拆分后的高电平时间不小于一个时间精度
                    const auto period{::verilator_utils::femtosecond_t{(current.a % 8u + 1u) * 4zu * ctx.precision_fs}};
                    const auto duration{::verilator_utils::femtosecond_t{(current.a % 5u + 1u) * ctx.precision_fs}};
                    const auto active_high{(current.b & 1u) == 0u};
                    switch(current.b % 4u)
                    {
                        case 0:
                            // 时钟激励会一直运行，只需启动一次
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
                        case 1:
                            ctx.scheduler->add_task(
                                ::verilator_utils::generate_reset(*ctx.rst, *ctx.clk, current.a % 4u + 1u, active_high));
                            break;
                        case 2:
                            ctx.scheduler->add_task(::verilator_utils::generate_async_reset(*ctx.rst, duration, active_high));
                            break;
                        default: co_await ::verilator_utils::wait_reset_finish(*ctx.rst, active_high); break;
                    }
                    break;
                }
                case 15:  // 验证与激励时机
                {
                    const auto edge{(current.a & 1u) != 0u ? ::verilator_utils::edge_enum::rising
                                                           : ::verilator_utils::edge_enum::falling};
                    const auto edge_to_wait{static_cast<::std::size_t>(current.a % 3zu + 1zu)};
                    switch(current.b % 4u)
                    {
                        case 0:
                            co_await ::verilator_utils::wait_verify(
                                *ctx.clk,
                                edge_to_wait,
                                edge,
                                ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval);
                            break;
                        case 1: co_await ::verilator_utils::wait_stimulate(*ctx.clk, edge_to_wait, edge); break;
                        case 2:
                            co_await ::verilator_utils::verify_at(
                                *ctx.clk,
                                ::verilator_utils::default_event_callback{[&ctx] { return static_cast<bool>(*ctx.signal); }},
                                [] {});
                            break;
                        default: co_await ::verilator_utils::verify_at(*ctx.rst, [] {}); break;
                    }
                    break;
                }
                case 16:  // select_clock与边沿检测器接口
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
                case 17:  // 邮箱读写
                {
                    auto& mailbox{(*ctx.mailboxes)[current.a % ctx.mailboxes->size()]};
                    switch(current.b % 7u)
                    {
                        case 0: co_await mailbox.put(current.a); break;
                        case 1: co_await mailbox.get(); break;
                        case 2: co_await mailbox.peek(); break;
                        case 3:
                        {
                            const auto before{mailbox.num()};
                            const auto got{mailbox.try_get()};
                            ::fuzz::check(mailbox.num() == before - (got.has_value() ? 1zu : 0zu));
                            break;
                        }
                        case 4:
                        {
                            const auto before{mailbox.num()};
                            const auto accepted{mailbox.try_put(current.a)};
                            ::fuzz::check(mailbox.num() == before + (accepted ? 1zu : 0zu));
                            break;
                        }
                        case 5:
                        {
                            // 无限容量邮箱：批量读写以触发空闲存储的回收路径
                            auto& infinite{(*ctx.mailboxes)[0]};
                            const auto count{static_cast<::std::size_t>(520zu + current.a % 40zu)};
                            for(::std::size_t i{}; i != count; ++i) { (void)infinite.try_put(i); }
                            for(::std::size_t i{}; i != count; ++i) { ::fuzz::check(infinite.try_get().has_value()); }
                            break;
                        }
                        default:
                        {
                            const auto before{mailbox.num()};
                            const auto got{mailbox.try_peek()};
                            ::fuzz::check(got.has_value() == (before != 0zu));
                            ::fuzz::check(!::std::format("{}", mailbox).empty());
                            ::fuzz::check(!::std::format("{:#}", mailbox).empty());
                            break;
                        }
                    }
                    break;
                }
                case 18:  // LFSR生成器
                    check_lfsr(current);
                    break;
                case 19:  // 最大仿真时间：触发仿真超时退出
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
                    check_time(ctx);
                    break;
                }
                case 20:  // 非阻塞接口与调度器状态查询
                {
                    switch(current.b % 5u)
                    {
                        case 0:
                        {
                            auto& semaphore{(*ctx.semaphores)[current.a % ctx.semaphores->size()]};
                            const auto update{static_cast<::std::size_t>(current.a % 3u + 1u)};
                            if(semaphore.try_get(update)) { semaphore.put(update); }
                            break;
                        }
                        case 1: (*ctx.events)[current.a % ctx.events->size()].shrink_to_fit(); break;
                        case 2:
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
                                inspect_assertion_error(error);
                            }
                            break;
                        }
                        case 3: check_assertion_reporting(current.a); break;
                        case 4:
                        {
                            // 任务池中的非标准异常必须被汇总并通过join_all_exception报告
                            auto pool{co_await ::verilator_utils::spawn_pool()};
                            pool.add_task(run_probe_error(ctx, static_cast<int>(current.a % 7u)));
                            bool reported{};
                            try
                            {
                                co_await pool.join_all();
                            }
                            catch(const ::verilator_utils::spawn_pool::join_all_exception& error)
                            {
                                reported = true;
                                ::fuzz::check(!::std::string_view{error.what()}.empty());
                                ::fuzz::check(!error.exceptions().empty());
                                ::fuzz::check(is_probe_error(error.exceptions().front()));
                            }
                            ::fuzz::check(reported);
                            break;
                        }
                        default: check_time(ctx); break;
                    }
                    break;
                }
                default:  // 批量等待者：触发挂起队列的前缀回收与收缩路径
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
                        for(::std::size_t i{}; i != count; ++i) { ctx.scheduler->add_task(waiter()); }
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
                            for(::std::size_t i{}; i != count; ++i) { (*ctx.events)[index].notify_one(); }
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
                        for(::std::size_t i{}; i != count; ++i) { ctx.scheduler->add_task(waiter()); }
                        // 等待者需要先运行并挂起到信号量队列上，发放才有意义
                        co_await ::verilator_utils::wait_eval_stage(
                            *ctx.scheduler,
                            ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval);
                        (*ctx.semaphores)[semaphore_index].put(count);
                    }
                    break;
                }
            }
        }
    }

    /// 抛出探针异常的任务：用于检查任务池汇总异常的报告路径
    ::verilator_utils::task<void> run_probe_error(const context& ctx, int value)
    {
        ++*ctx.created;
        const ::frame_destruction_counter counter{ctx.destroyed};
        // 刻意抛出非标准异常类型，用于覆盖汇总异常中未知异常名称的报告路径
        // NOLINTNEXTLINE(bugprone-std-exception-baseclass)
        throw probe_error{.value = value};
        co_return;  // 协程体必须包含协程关键字，该语句不会被执行
    }

    /// 看门狗任务：有限时间后结束仿真
    ::verilator_utils::task<void> run_watchdog(const context& ctx, ::std::uint64_t delay)
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
    void drive_simulation(const program& prog,
                          ::verilator_utils::eval_scheduler& scheduler,
                          sync_objects& objects,
                          ::std::size_t& created,
                          ::std::size_t& destroyed,
                          ::std::size_t& canceled,
                          ::std::size_t& timeouts,
                          bool use_initial_eval,
                          ::std::uint64_t watchdog_delay)
    {
        context ctx{.events = &objects.events,
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
        scheduler.add_task(run_watchdog(ctx, watchdog_delay));
        for(const auto& root: prog.roots) { scheduler.add_task(run_ops(prog, root, ctx, 0zu)); }
        // 初始化评估会立即执行就绪队列中的任务，直到它们挂起
        if(use_initial_eval) { scheduler.initial_eval(); }

        try
        {
            scheduler.loop_until_finish();
        }
        catch(const ::verilator_utils::eval_finish_exception&)
        {
            // 仿真正常结束时的协作式取消：看门狗已调用eval_finish
            ++canceled;
        }
        // 无论是否经由异常退出，看门狗都保证仿真已经结束
        ::fuzz::check(scheduler.is_finish());
        ctx.error_seen = ctx.error_seen || scheduler.is_error();
    }

    /// 执行一个模糊测试程序
    void run_program(const program& prog, ::std::uint64_t watchdog_delay, destruction_order order, bool use_initial_eval)
    {
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

        ::scheduler_fixture fixture{};
        // 两种析构顺序都应当安全，由输入决定本次使用哪一种
        if(order == destruction_order::scheduler_first)
        {
            // 同步体比调度器长寿：调度器析构时回收全部挂起任务并向同步体解除绑定
            sync_objects objects{};
            {
                auto scheduler{fixture.make_scheduler()};
                drive_simulation(prog,
                                 scheduler,
                                 objects,
                                 created,
                                 destroyed,
                                 canceled,
                                 timeouts,
                                 use_initial_eval,
                                 watchdog_delay);
            }
        }
        else
        {
            // 同步体比调度器先析构：仿真结束时同步体把挂起任务交回调度器
            auto scheduler{fixture.make_scheduler()};
            sync_objects objects{};
            drive_simulation(prog, scheduler, objects, created, destroyed, canceled, timeouts, use_initial_eval, watchdog_delay);
        }

        // 每个协程帧都必须恰好回收一次
        ::fuzz::check(created == destroyed, created);
    }

    int do_fuzz(const ::std::uint8_t* data, ::std::size_t size)
    {
        if(size < 2) { return 0; }
        ::fuzz::reader input{data, size};

        program prog{};
        for(auto& root: prog.roots) { root = decode_ops(input, 5zu); }
        for(auto& subtask: prog.subtasks) { subtask = decode_ops(input, 4zu); }
        const auto watchdog_delay{input.range(5, 300)};
        const auto order{input.boolean() ? destruction_order::scheduler_first : destruction_order::sync_object_first};
        run_program(prog, watchdog_delay, order, input.boolean());
        return 0;
    }
}  // namespace

TEST_SUITE("verilator_utils/fuzz")
{
    TEST_CASE("scheduler") { ::fuzz::run_fuzz_test(&::do_fuzz); }
}
