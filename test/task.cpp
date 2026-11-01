#include <doctest_macros.hpp>
import unit_test;

namespace
{
    /// 推进一个时钟周期：等待1ns后将时钟驱动到给定电平并执行评估
    ///
    /// @note 先推进时间再翻转电平，使边沿检测器在同一轮评估中观察到信号变化
    void step_clock(::verilator_utils::eval_scheduler& scheduler, signal_state& clk, ::CData value = 1u)
    {
        const auto advance{
            [&] -> ::verilator_utils::task<void> { co_await ::verilator_utils::wait_time(1_ns); },
        };
        scheduler.add_task(advance());
        scheduler.loop_once();
        clk.value = value;
        scheduler.loop_once();
    }

    /// 协程栈回溯测试辅助协程，用于构建根协程→子协程→孙协程的同步调用链

    /// 根协程：仅执行一次协程栈回溯，不产生子任务
    ::verilator_utils::task<void> stacktrace_root_only(::std::shared_ptr<::verilator_utils::coroutine_stacktrace>& captured,
                                                       int& expected_line)
    {
        const auto stacktrace_site{::std::source_location::current()};
        captured = ::std::make_shared<::verilator_utils::coroutine_stacktrace>(co_await ::verilator_utils::stacktrace());
        expected_line = static_cast<int>(stacktrace_site.line()) + 1;
    }

    /// 孙协程：记录自身的协程柄并执行协程栈回溯
    ::verilator_utils::task<void> stacktrace_grandchild(::std::shared_ptr<::verilator_utils::coroutine_stacktrace>& captured,
                                                        ::verilator_utils::task<void>::handle_t& self_handle,
                                                        int& expected_line)
    {
        self_handle = co_await ::verilator_utils::handle<::verilator_utils::task<void>::promise_type>();
        const auto stacktrace_site{::std::source_location::current()};
        captured = ::std::make_shared<::verilator_utils::coroutine_stacktrace>(co_await ::verilator_utils::stacktrace());
        expected_line = static_cast<int>(stacktrace_site.line()) + 1;
    }

    /// 子协程：记录自身的协程柄并等待孙协程
    ::verilator_utils::task<void> stacktrace_child(::std::shared_ptr<::verilator_utils::coroutine_stacktrace>& captured,
                                                   ::verilator_utils::task<void>::handle_t& self_handle,
                                                   ::verilator_utils::task<void>::handle_t& grandchild_handle,
                                                   int& expected_line)
    {
        self_handle = co_await ::verilator_utils::handle<::verilator_utils::task<void>::promise_type>();
        co_await stacktrace_grandchild(captured, grandchild_handle, expected_line);
    }

    /// 根协程：记录自身的协程柄并等待子协程
    ::verilator_utils::task<void> stacktrace_root(::std::shared_ptr<::verilator_utils::coroutine_stacktrace>& captured,
                                                  ::verilator_utils::task<void>::handle_t& child_handle,
                                                  ::verilator_utils::task<void>::handle_t& grandchild_handle,
                                                  int& expected_line)
    { co_await stacktrace_child(captured, child_handle, grandchild_handle, expected_line); }

    /// 异步子协程：记录自身的协程柄，执行协程栈回溯后挂起以保持父协程处于等待状态
    ::verilator_utils::task<void> stacktrace_async_child(::std::shared_ptr<::verilator_utils::coroutine_stacktrace>& captured,
                                                         ::verilator_utils::task<void>::handle_t& self_handle,
                                                         int& expected_line)
    {
        self_handle = co_await ::verilator_utils::handle<::verilator_utils::task<void>::promise_type>();
        const auto stacktrace_site{::std::source_location::current()};
        captured = ::std::make_shared<::verilator_utils::coroutine_stacktrace>(co_await ::verilator_utils::stacktrace());
        expected_line = static_cast<int>(stacktrace_site.line()) + 1;
        co_await ::verilator_utils::wait_time(1_ps);
    }

    /// 未被任何协程等待的异步协程：执行协程栈回溯后挂起
    ::verilator_utils::task<void> stacktrace_orphan_async(::std::shared_ptr<::verilator_utils::coroutine_stacktrace>& captured,
                                                          int& expected_line)
    {
        const auto stacktrace_site{::std::source_location::current()};
        captured = ::std::make_shared<::verilator_utils::coroutine_stacktrace>(co_await ::verilator_utils::stacktrace());
        expected_line = static_cast<int>(stacktrace_site.line()) + 1;
        co_await ::verilator_utils::wait_time(1_ps);
    }

    /// 根协程：分别以抛出异常与nothrow两种方式对同一协程链执行协程栈回溯
    ::verilator_utils::task<void>
        stacktrace_nothrow_root(::std::shared_ptr<::verilator_utils::coroutine_stacktrace>& throwing_captured,
                                ::std::shared_ptr<::verilator_utils::coroutine_stacktrace>& nothrow_captured)
    {
        const auto pair{::verilator_utils::detail::coroutine_pair{
            co_await ::verilator_utils::handle<::verilator_utils::task<void>::promise_type>()}};
        throwing_captured = ::std::make_shared<::verilator_utils::coroutine_stacktrace>(pair);
        nothrow_captured = ::std::make_shared<::verilator_utils::coroutine_stacktrace>(::std::nothrow, pair);
    }
}  // namespace

TEST_SUITE("verilator_utils/task")
{
    using namespace ::std::string_view_literals;

    TEST_CASE("edge_detector reports rising falling and both edges")
    {
        signal_state signal{};
        ::verilator_utils::edge_detector rising_detector{::verilator_utils::bit_slice<::CData>{signal.value},
                                                         ::verilator_utils::edge_detector::rising};
        ::verilator_utils::edge_detector falling_detector{::verilator_utils::bit_slice<::CData>{signal.value},
                                                          ::verilator_utils::edge_detector::falling};
        ::verilator_utils::edge_detector both_detector{::verilator_utils::bit_slice<::CData>{signal.value},
                                                       ::verilator_utils::edge_detector::both};

        CHECK_FALSE(rising_detector());
        CHECK_FALSE(falling_detector());
        CHECK_FALSE(both_detector());

        signal.value = 1u;
        CHECK(rising_detector());
        CHECK_FALSE(rising_detector());
        CHECK_FALSE(falling_detector());
        CHECK(both_detector());

        signal.value = 0u;
        CHECK_FALSE(rising_detector());
        CHECK(falling_detector());
        CHECK(both_detector());
    }

    TEST_CASE("edge_detector exposes and updates selected edge")
    {
        signal_state signal{};
        ::verilator_utils::edge_detector detector{::verilator_utils::bit_slice<::CData>{signal.value},
                                                  ::verilator_utils::edge_detector::rising};

        CHECK_EQ(detector.edge_to_detect(), ::verilator_utils::edge_detector::rising);
        detector.set_edge_to_detect(::verilator_utils::edge_detector::falling);
        CHECK_EQ(detector.edge_to_detect(), ::verilator_utils::edge_detector::falling);

        signal.value = 1u;
        CHECK_FALSE(detector());
        signal.value = 0u;
        CHECK(detector());
    }

    TEST_CASE("wait_stimulate waits for the requested falling edges")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::CData clk{};
        ::std::uint64_t resumed_times{};
        ::verilator_utils::bit_slice clk_ref{clk};

        const auto&& stimulus_task{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_stimulate(clk_ref, 2);
                resumed_times = scheduler.time_in_time_precision();
                co_await ::verilator_utils::eval_finish();
            },
        };

        scheduler.add_task(::verilator_utils::generate_clock(clk_ref, 4_ps));
        scheduler.add_task(stimulus_task());

        scheduler.loop_until_finish();

        // 一个时钟周期4ps，等待2个时钟周期
        CHECK_EQ(resumed_times, 8);
    }

    TEST_CASE("edge wait helpers select the expected default evaluation stages")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::CData clk{};
        ::verilator_utils::bit_slice clk_ref{clk};
        ::verilator_utils::eval_scheduler::eval_stage_enum falling_stage{};
        ::verilator_utils::eval_scheduler::eval_stage_enum rising_stage{};

        const auto falling_task{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_stimulate(clk_ref);
                falling_stage = scheduler.eval_stage();
            },
        };
        const auto rising_task{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_stimulate(clk_ref, 1, ::verilator_utils::edge_enum::rising);
                rising_stage = scheduler.eval_stage();
            },
        };
        scheduler.add_task(falling_task());
        scheduler.add_task(rising_task());

        scheduler.loop_once();
        clk = 1;  // NOLINT(clang-analyzer-deadcode.DeadStores)
        scheduler.loop_once();
        CHECK_EQ(rising_stage, ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval);
        clk = 0;  // NOLINT(clang-analyzer-deadcode.DeadStores)
        scheduler.loop_once();
        CHECK_EQ(falling_stage, ::verilator_utils::eval_scheduler::eval_stage_enum::before_dut_eval);
    }

    TEST_CASE("wait_stimulate honors an explicitly requested evaluation stage")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::CData clk{};
        ::verilator_utils::bit_slice clk_ref{clk};
        ::verilator_utils::eval_scheduler::eval_stage_enum observed_stage{};

        const auto task{
            [&] -> ::verilator_utils::task<void> {
                // 下降沿默认在评估前返回，这里显式要求在评估完成后返回
                co_await ::verilator_utils::wait_stimulate(clk_ref,
                                                           1,
                                                           ::verilator_utils::edge_enum::falling,
                                                           ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval);
                observed_stage = scheduler.eval_stage();
            },
        };
        scheduler.add_task(task());

        scheduler.loop_once();
        clk = 1;  // NOLINT(clang-analyzer-deadcode.DeadStores)
        scheduler.loop_once();
        clk = 0;  // NOLINT(clang-analyzer-deadcode.DeadStores)
        scheduler.loop_once();

        CHECK_EQ(observed_stage, ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval);
    }

    TEST_CASE("wait_stimulate rejects waiting for both clock edges")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::CData clk{};
        ::verilator_utils::bit_slice clk_ref{clk};

        const auto task{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_stimulate(clk_ref, 1, ::verilator_utils::edge_enum::both);
            },
        };
        scheduler.add_task(task());

        bool rejected{};
        try
        {
            scheduler.loop_until_finish();
        }
        catch(const ::verilator_utils::coroutine_exception& exception)
        {
            const auto* const original{exception.as<::verilator_utils::assertion_error>()};
            REQUIRE_NE(original, nullptr);
            rejected = ::std::string_view{original->message()}.contains("不支持等待双边沿"sv);
        }
        CHECK(rejected);
    }

    TEST_CASE("posedge negedge and alledge count edges")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        signal_state clk{};
        ::std::size_t posedge_count{};
        ::std::size_t negedge_count{};
        ::std::size_t alledge_count{};

        const auto&& task{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_posedge(::verilator_utils::bit_slice<::CData>{clk.value}, 2);
                posedge_count = 2;
                co_await ::verilator_utils::wait_negedge(::verilator_utils::bit_slice<::CData>{clk.value}, 1);
                negedge_count = 1;
                co_await ::verilator_utils::wait_alledge(::verilator_utils::bit_slice<::CData>{clk.value}, 2);
                alledge_count = 2;
            },
        };

        scheduler.add_task(task());
        scheduler.loop_once();
        CHECK_EQ(posedge_count, 0u);
        clk.value = 1;
        scheduler.loop_once();
        CHECK_EQ(posedge_count, 0u);
        clk.value = 0;
        scheduler.loop_once();
        CHECK_EQ(posedge_count, 0u);
        clk.value = 1;
        scheduler.loop_once();
        CHECK_EQ(posedge_count, 2u);
        CHECK_EQ(negedge_count, 0u);
        clk.value = 0;
        scheduler.loop_once();
        CHECK_EQ(negedge_count, 1u);
        clk.value = 1;
        scheduler.loop_once();
        CHECK_EQ(alledge_count, 0u);
        clk.value = 0;
        scheduler.loop_once();
        CHECK_EQ(alledge_count, 2u);
    }

    TEST_CASE("clock and reset helpers drive expected signal sequences")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::CData clk{};
        ::CData reset{};
        ::CData reset_n{};
        ::verilator_utils::bit_slice clk_ref{clk};
        ::verilator_utils::bit_slice reset_ref{reset};
        ::verilator_utils::bit_slice reset_n_ref{reset_n};

        scheduler.add_task(::verilator_utils::generate_clock(clk_ref, 4_ps));
        scheduler.add_task(::verilator_utils::generate_reset(reset_ref, clk_ref, 1, true));
        scheduler.add_task(::verilator_utils::generate_reset(reset_n_ref, clk_ref, 1, false));

        scheduler.loop_once();
        CHECK_EQ(clk, 1);
        CHECK_EQ(reset, 1);
        CHECK_EQ(reset_n, 0);

        scheduler.loop_once();
        CHECK_EQ(clk, 0);
        CHECK_EQ(reset, 0);
        CHECK_EQ(reset_n, 1);

        scheduler.loop_once();
        CHECK_EQ(clk, 1);
        CHECK_EQ(reset, 0);
        CHECK_EQ(reset_n, 1);

        scheduler.finish();
        scheduler.loop_once();
    }

    TEST_CASE("clock generation honors its startup delay")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::CData clk{1};
        ::verilator_utils::bit_slice clk_ref{clk};

        const auto&& probe_task{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_time(2_ps);
                CHECK_EQ(clk, 0);
            },
        };

        scheduler.add_task(::verilator_utils::generate_clock(clk_ref, 4_ps, 3_ps));
        scheduler.add_task(probe_task());

        scheduler.loop_once();
        CHECK_EQ(scheduler.time_in_time_precision(), 2u);
        CHECK_EQ(clk, 0);

        scheduler.loop_once();
        CHECK_EQ(scheduler.time_in_time_precision(), 3u);
        CHECK_EQ(clk, 0);

        scheduler.loop_once();
        CHECK_EQ(scheduler.time_in_time_precision(), 5u);
        CHECK_EQ(clk, 1);

        scheduler.loop_once();
        CHECK_EQ(scheduler.time_in_time_precision(), 7u);
        CHECK_EQ(clk, 0);

        scheduler.finish();
        scheduler.loop_once();
    }

    TEST_CASE("clock generation honors its duty ratio")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::CData clk{};
        ::verilator_utils::bit_slice clk_ref{clk};

        scheduler.add_task(::verilator_utils::generate_clock(clk_ref, 10_ps, 0_fs, 0.3));

        scheduler.loop_once();
        CHECK_EQ(scheduler.time_in_time_precision(), 7u);
        CHECK_EQ(clk, 1);
        scheduler.loop_once();
        CHECK_EQ(scheduler.time_in_time_precision(), 10u);
        CHECK_EQ(clk, 0);
        scheduler.loop_once();
        CHECK_EQ(scheduler.time_in_time_precision(), 17u);
        CHECK_EQ(clk, 1);

        scheduler.finish();
        scheduler.loop_once();
    }

    TEST_CASE("clock generation rejects an invalid duty ratio")
    {
        // 占空比必须严格位于(0, 1)之间，端点与越界值都应被拒绝
        for(const auto duty_ratio: {0.0, 1.0, -0.1, 1.1})
        {
            CAPTURE(duty_ratio);
            scheduler_fixture fixture{};
            auto scheduler{fixture.make_scheduler()};
            ::CData clk{};
            ::verilator_utils::bit_slice clk_ref{clk};
            scheduler.add_task(::verilator_utils::generate_clock(clk_ref, 4_ps, 0_fs, duty_ratio));

            bool rejected{};
            try
            {
                scheduler.loop_until_finish();
            }
            catch(const ::verilator_utils::coroutine_exception& exception)
            {
                const auto* const original{exception.as<::verilator_utils::assertion_error>()};
                REQUIRE_NE(original, nullptr);
                rejected = ::std::string_view{original->message()}.contains("时钟占空比"sv);
            }
            CHECK(rejected);
        }
    }

    TEST_CASE("asynchronous reset asserts immediately and releases after its duration")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::CData reset{};
        ::verilator_utils::bit_slice reset_ref{reset};

        scheduler.add_task(::verilator_utils::generate_async_reset(reset_ref, 3_ps));

        scheduler.initial_eval();
        CHECK_EQ(reset, 1);
        CHECK_EQ(scheduler.time_in_time_precision(), 0u);
        scheduler.loop_once();
        CHECK_EQ(reset, 0);
        CHECK_EQ(scheduler.time_in_time_precision(), 3u);
    }

    TEST_CASE("asynchronous reset supports active-low polarity")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::CData reset{};
        ::verilator_utils::bit_slice reset_ref{reset};

        scheduler.add_task(::verilator_utils::generate_async_reset(reset_ref, 2_ps, false));

        scheduler.initial_eval();
        CHECK_EQ(reset, 0);
        scheduler.loop_once();
        CHECK_EQ(reset, 1);
        CHECK_EQ(scheduler.time_in_time_precision(), 2u);
    }

    TEST_CASE("maximum evaluation time finishes the scheduler")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};

        scheduler.add_task(::verilator_utils::max_eval_time(4_ps));
        // 仿真超时异常按原样传播到调度器外，不包装为coroutine_exception
        CHECK_THROWS_AS(scheduler.loop_until_finish(), ::verilator_utils::eval_timeout_exception);

        CHECK_EQ(scheduler.time_in_time_precision(), 4u);
        CHECK(scheduler.is_error());
        CHECK(scheduler.is_finish());
    }

    TEST_CASE("eval_timeout_exception from a subtask propagates unchanged")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};

        const auto timeout_child{[] -> ::verilator_utils::task<void> {
            throw ::verilator_utils::eval_timeout_exception{};
            co_return;
        }};
        const auto timeout_parent{[&] -> ::verilator_utils::task<void> { co_await timeout_child(); }};

        scheduler.add_task(timeout_parent());
        CHECK_THROWS_AS(scheduler.loop_until_finish(), ::verilator_utils::eval_timeout_exception);
    }

    TEST_CASE("stacktrace_frame formatter renders coroutine role, function and location")
    {
        using frame_t = ::verilator_utils::coroutine_stacktrace::frame;
        static_assert(::std::formattable<frame_t, char>);

        const auto location{::std::source_location::current()};
        const frame_t frame{nullptr, location, ::verilator_utils::detail::promise_base::coroutine_type_enum::sub_coroutine};

        const auto plain{::std::format("{}"sv, frame)};
        CHECK(plain.contains("sub"sv));
        CHECK(plain.contains(location.function_name()));
        CHECK(plain.contains(location.file_name()));
        CHECK(plain.ends_with(::std::format(":{}:{}"sv, location.line(), location.column())));

        const auto colored{::std::format("{:#}"sv, frame)};
        // 嵌套参数动态选择是否输出ANSI颜色
        CHECK_EQ(::std::format("{:{}}"sv, frame, false), plain);
        CHECK_EQ(::std::format("{:{}}"sv, frame, true), colored);
        using namespace ::verilator_utils::detail::assertion_color;
        // 各字段的着色与Stack trace一致：指针和行列号为蓝色，函数名为黄色，文件路径为绿色
        const auto pointer_text{::std::format("{:#0{}x}"sv, 0zu, 2 * sizeof(void*) + 2)};
        CHECK(colored.contains(::std::format("{}{}{}"sv, blue, pointer_text, reset)));
        CHECK(colored.contains(::std::format("{}{}{}"sv, yellow, location.function_name(), reset)));
        CHECK(colored.contains(::std::format("{}{}{}"sv, green, location.file_name(), reset)));
        CHECK(colored.contains(::std::format(":{}{}{}"sv, blue, location.line(), reset)));
        CHECK(colored.contains(::std::format(":{}{}{}"sv, blue, location.column(), reset)));
        CHECK(colored.ends_with(reset));
        CHECK_FALSE(colored.contains(cyan));
    }

    TEST_CASE("coroutine formatters write into a raw character buffer")
    {
        using frame_t = ::verilator_utils::coroutine_stacktrace::frame;

        const frame_t frame{nullptr,
                            ::std::source_location::current(),
                            ::verilator_utils::detail::promise_base::coroutine_type_enum::root_coroutine};

        ::std::array<char, 512> buffer{};
        auto* const frame_end{::std::format_to(buffer.data(), "{}"sv, frame)};
        CHECK_EQ(::std::string_view{buffer.data(), static_cast<::std::size_t>(::std::distance(buffer.data(), frame_end))},
                 ::std::format("{}"sv, frame));

        ::verilator_utils::coroutine_stacktrace trace{};

        ::std::array<char, 512> detail_buffer{};
        auto* const trace_end{::std::format_to(detail_buffer.data(), "{}"sv, trace)};
        CHECK_EQ(::std::string_view{detail_buffer.data(),
                                    static_cast<::std::size_t>(::std::distance(detail_buffer.data(), trace_end))},
                 ::std::format("{}"sv, trace));
    }

    TEST_CASE("stacktrace_frame formatter renders a default frame deterministically")
    {
        using frame_t = ::verilator_utils::coroutine_stacktrace::frame;

        const frame_t frame{};
        CHECK_EQ(::std::format("{}"sv, frame), "0x0000000000000000 (root)  in  at :0:0"sv);

        // doctest的StringMaker会根据全局颜色配置决定是否输出ANSI转义序列，因此只校验内容而非精确字符串
        const auto converted{::doctest::StringMaker<frame_t>::convert(frame)};
        CHECK_NE(converted.size(), 0u);
        CHECK(::std::string_view{converted.c_str()}.contains("root"sv));
    }

    TEST_CASE("stacktrace_frame formatter zero pads the pointer and aligns the coroutine type")
    {
        using frame_t = ::verilator_utils::coroutine_stacktrace::frame;
        using type_t = ::verilator_utils::detail::promise_base::coroutine_type_enum;

        const auto location{::std::source_location::current()};
        const frame_t root{nullptr, location, type_t::root_coroutine};
        const frame_t sub{nullptr, location, type_t::sub_coroutine};
        const frame_t async{nullptr, location, type_t::async_coroutine};

        // 指针值按指针的十六进制位数补0，使不同帧的指针字段等宽
        const auto pointer_digits{2 * sizeof(void*)};
        const auto plain_root{::std::format("{}"sv, root)};
        CHECK(plain_root.starts_with(::std::format("0x{:0{}x} "sv, 0zu, pointer_digits)));

        // 协程类型宽度不同，右侧补空格使各帧的in关键字对齐
        const auto in_position{[](const ::std::string& rendered) { return rendered.find(" in "sv); }};
        const auto sub_position{in_position(::std::format("{}"sv, sub))};
        CHECK_NE(sub_position, ::std::string::npos);
        CHECK_EQ(in_position(plain_root), sub_position);
        CHECK_EQ(sub_position, in_position(::std::format("{}"sv, async)));
        // 补空格不改变帧的其余内容
        CHECK(plain_root.ends_with(::std::format(" at {}:{}:{}"sv, location.file_name(), location.line(), location.column())));
    }

    TEST_CASE("stacktrace_frame formatter rejects unsupported format specifiers")
    {
        using frame_t = ::verilator_utils::coroutine_stacktrace::frame;

        frame_t frame{};

        CHECK_THROWS_AS(static_cast<void>(::std::vformat("{:x}"sv, ::std::make_format_args(frame))), ::std::format_error);
    }

    TEST_CASE("stacktrace() from a running root task captures the call site as the only frame")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::std::shared_ptr<::verilator_utils::coroutine_stacktrace> captured{};
        int expected_line{};

        scheduler.add_task(stacktrace_root_only(captured, expected_line));
        scheduler.loop_until_finish();

        REQUIRE(captured);
        REQUIRE_EQ(captured->frames.size(), 1u);
        const auto& frame{captured->frames.front()};
        CHECK_EQ(frame.type, ::verilator_utils::detail::promise_base::coroutine_type_enum::root_coroutine);
        CHECK(::std::string_view{frame.location.file_name()}.ends_with("task.cpp"sv));
        CHECK(::std::string_view{frame.location.function_name()}.contains("stacktrace_root_only"sv));
        // 当前帧的位置被覆盖为调用stacktrace()的源代码位置
        CHECK_EQ(static_cast<int>(frame.location.line()), expected_line);
        CHECK_EQ(::std::format("{}"sv, frame.type), "root"sv);
    }

    TEST_CASE("stacktrace() walks the full nested sync parent chain")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::std::shared_ptr<::verilator_utils::coroutine_stacktrace> captured{};
        ::verilator_utils::task<void>::handle_t child_handle{};
        ::verilator_utils::task<void>::handle_t grandchild_handle{};
        int expected_line{};

        auto root_task{stacktrace_root(captured, child_handle, grandchild_handle, expected_line)};
        const auto root_handle{root_task.handle()};
        scheduler.add_task(::std::move(root_task));
        scheduler.loop_until_finish();

        REQUIRE(captured);
        REQUIRE_EQ(captured->frames.size(), 3u);
        using type_t = ::verilator_utils::detail::promise_base::coroutine_type_enum;
        // 帧顺序：当前协程在最前，逐层回溯到根协程
        CHECK_EQ(captured->frames[0].type, type_t::sub_coroutine);
        CHECK_EQ(captured->frames[1].type, type_t::sub_coroutine);
        CHECK_EQ(captured->frames[2].type, type_t::root_coroutine);
        // 帧柄构成逐层向上的调用链
        CHECK_EQ(captured->frames[0].coroutine_frame_ptr, grandchild_handle.address());
        CHECK_EQ(captured->frames[1].coroutine_frame_ptr, child_handle.address());
        CHECK_EQ(captured->frames[2].coroutine_frame_ptr, root_handle.address());
        // 每帧的挂起位置对应各自函数中co_await的调用处
        CHECK(::std::string_view{captured->frames[0].location.function_name()}.contains("stacktrace_grandchild"sv));
        CHECK_EQ(static_cast<int>(captured->frames[0].location.line()), expected_line);
        CHECK(::std::string_view{captured->frames[1].location.function_name()}.contains("stacktrace_child"sv));
        CHECK_GT(static_cast<int>(captured->frames[1].location.line()), 0);
        CHECK(::std::string_view{captured->frames[2].location.function_name()}.contains("stacktrace_root"sv));
        CHECK_GT(static_cast<int>(captured->frames[2].location.line()), 0);
        for(const auto& frame: captured->frames)
        {
            CHECK(::std::string_view{frame.location.file_name()}.ends_with("task.cpp"sv));
        }
    }

    TEST_CASE("stacktrace() exposes an awaited async task as a sub coroutine above its root parent")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::std::shared_ptr<::verilator_utils::coroutine_stacktrace> captured{};
        ::verilator_utils::task<void>::handle_t async_child_handle{};
        int expected_line{};

        auto root{[&](this auto) -> ::verilator_utils::task<void> {
            // 异步任务必须在协程上下文中创建：父协程为当前协程
            auto child{co_await ::verilator_utils::to_async(stacktrace_async_child(captured, async_child_handle, expected_line))};
            co_await child;
        }()};
        scheduler.add_task(::std::move(root));
        scheduler.loop_until_finish();

        REQUIRE(captured);
        REQUIRE_EQ(captured->frames.size(), 2u);
        using type_t = ::verilator_utils::detail::promise_base::coroutine_type_enum;
        // 异步子协程被父协程等待后变为带父协程的子协程
        CHECK_EQ(captured->frames[0].type, type_t::sub_coroutine);
        CHECK_EQ(captured->frames[0].coroutine_frame_ptr, async_child_handle.address());
        CHECK_EQ(static_cast<int>(captured->frames[0].location.line()), expected_line);
        CHECK(::std::string_view{captured->frames[0].location.function_name()}.contains("stacktrace_async_child"sv));
        // 父协程为根协程，挂起位置为等待子协程的co_await调用处
        CHECK_EQ(captured->frames[1].type, type_t::root_coroutine);
        CHECK_GT(static_cast<int>(captured->frames[1].location.line()), 0);
        CHECK(::std::string_view{captured->frames[1].location.file_name()}.ends_with("task.cpp"sv));
    }

    TEST_CASE("stacktrace() classifies an unawaited async task as an async coroutine")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::std::shared_ptr<::verilator_utils::coroutine_stacktrace> captured{};
        int expected_line{};

        // 异步任务在协程上下文中创建且不被等待：父协程在子协程完成前保持存活，
        // 因此子协程保持异步协程的身份并可通过父协程回溯
        auto root{[&](this auto) -> ::verilator_utils::task<void> {
            [[maybe_unused]] const auto child{
                co_await ::verilator_utils::to_async(stacktrace_orphan_async(captured, expected_line))};
            co_await ::verilator_utils::wait_time(2_ps);
        }()};
        scheduler.add_task(::std::move(root));
        scheduler.loop_until_finish();

        REQUIRE(captured);
        REQUIRE_EQ(captured->frames.size(), 2u);
        using type_t = ::verilator_utils::detail::promise_base::coroutine_type_enum;
        // 未被等待的异步协程为异步协程帧，其上方为创建它的根协程
        CHECK_EQ(captured->frames[0].type, type_t::async_coroutine);
        CHECK_EQ(static_cast<int>(captured->frames[0].location.line()), expected_line);
        CHECK(::std::string_view{captured->frames[0].location.function_name()}.contains("stacktrace_orphan_async"sv));
        CHECK_EQ(captured->frames[1].type, type_t::root_coroutine);
    }

    TEST_CASE("coroutine_stacktrace nothrow construction captures the same frames")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::std::shared_ptr<::verilator_utils::coroutine_stacktrace> throwing_captured{};
        ::std::shared_ptr<::verilator_utils::coroutine_stacktrace> nothrow_captured{};

        scheduler.add_task(stacktrace_nothrow_root(throwing_captured, nothrow_captured));
        CHECK_NOTHROW(scheduler.loop_until_finish());

        REQUIRE(throwing_captured);
        REQUIRE(nothrow_captured);
        // nothrow构造只吞掉异常而不吞掉栈帧：两种构造方式捕获完全相同的调用链
        REQUIRE_FALSE(throwing_captured->empty());
        REQUIRE_EQ(nothrow_captured->frames.size(), throwing_captured->frames.size());
        for(const auto& [throwing_frame, nothrow_frame]: ::std::views::zip(throwing_captured->frames, nothrow_captured->frames))
        {
            CHECK_EQ(nothrow_frame.coroutine_frame_ptr, throwing_frame.coroutine_frame_ptr);
            CHECK_EQ(nothrow_frame.type, throwing_frame.type);
            CHECK_EQ(nothrow_frame.location.line(), throwing_frame.location.line());
        }
    }

    TEST_CASE("coroutine_stacktrace formatter renders every frame with its index")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::std::shared_ptr<::verilator_utils::coroutine_stacktrace> captured{};
        int expected_line{};

        scheduler.add_task(stacktrace_root_only(captured, expected_line));
        scheduler.loop_until_finish();
        REQUIRE(captured);

        static_assert(::std::formattable<::verilator_utils::coroutine_stacktrace, char>);
        const auto rendered{::std::format("{}"sv, *captured)};
        CHECK(rendered.starts_with("Coroutine stack trace (most recent call first):\n#0 "sv));
        // 单帧的编号宽度为1，帧编号与指针之间只有一个分隔空格
        CHECK(rendered.contains("\n#0 0x"sv));
        CHECK(rendered.contains("root"sv));
        CHECK(rendered.contains("stacktrace_root_only"sv));
        CHECK(rendered.ends_with('\n'));
        CHECK_FALSE(rendered.contains("\033["sv));

        const auto colored{::std::format("{:#}"sv, *captured)};
        // 嵌套参数动态选择是否输出ANSI颜色
        CHECK_EQ(::std::format("{:{}}"sv, *captured, false), rendered);
        CHECK_EQ(::std::format("{:{}}"sv, *captured, true), colored);
        using namespace ::verilator_utils::detail::assertion_color;
        // 着色与Stack trace一致：指针为蓝色，函数名为黄色，文件路径为绿色
        CHECK(colored.contains(blue));
        CHECK(colored.contains(green));
        CHECK(colored.contains(yellow));
        CHECK(colored.contains("stacktrace_root_only"sv));
    }

    TEST_CASE("coroutine_stacktrace formatter widens the frame index to the total frame count")
    {
        using type_t = ::verilator_utils::detail::promise_base::coroutine_type_enum;
        const auto location{::std::source_location::current()};

        ::verilator_utils::coroutine_stacktrace trace{};
        constexpr auto frame_count{11zu};
        trace.frames.reserve(frame_count);
        for(auto i{0zu}; i < frame_count; ++i)
        {
            // 最后一个帧为根协程，其余为同步子协程，覆盖宽度不同的协程类型
            const auto type{i + 1 == frame_count ? type_t::root_coroutine : type_t::sub_coroutine};
            trace.frames.emplace_back(nullptr, location, type);
        }

        // 帧编号按总条目数自适应宽度，个位编号补空格后与两位编号的指针字段对齐
        const auto rendered{::std::format("{}"sv, trace)};
        CHECK(rendered.contains("\n#0  0x"sv));
        CHECK(rendered.contains("\n#10 0x"sv));
    }

    TEST_CASE("coroutine_type_enum formatter rejects unsupported format specifiers")
    {
        const auto type{::verilator_utils::detail::promise_base::coroutine_type_enum::sub_coroutine};
        CHECK_EQ(::std::format("{}"sv, type), "sub"sv);
        CHECK_EQ(::std::format("{}"sv, ::verilator_utils::detail::promise_base::coroutine_type_enum::async_coroutine), "async"sv);
        CHECK_THROWS_AS(static_cast<void>(::std::vformat("{:x}"sv, ::std::make_format_args(type))), ::std::format_error);
    }

    TEST_CASE("select_clock wakes on the detected edge of a single clock")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        signal_state clk{};
        ::std::vector<::std::vector<bool>> results;

        ::verilator_utils::select_clock clock_selector{};
        clock_selector.add_clock(::verilator_utils::bit_slice<::CData>{clk.value}, ::verilator_utils::edge_enum::rising);

        auto selector_task{[&](this auto) -> ::verilator_utils::task<void> {
            for(auto i{0zu}; i != 3; ++i)
            {
                auto triggered{co_await clock_selector};
                results.emplace_back(triggered | ::std::ranges::to<::std::vector<bool>>());
            }
        }()};
        scheduler.add_task(::std::move(selector_task));

        scheduler.loop_once();
        CHECK_EQ(results.size(), 0u);

        // 上升沿触发
        clk.value = 1u;
        scheduler.loop_once();
        REQUIRE_EQ(results.size(), 1u);
        REQUIRE_EQ(results[0].size(), 1u);
        CHECK_EQ(results[0][0], true);

        // 下降沿不触发上升沿检测
        clk.value = 0u;
        scheduler.loop_once();
        CHECK_EQ(results.size(), 1u);

        // 再次上升沿触发
        clk.value = 1u;
        scheduler.loop_once();
        REQUIRE_EQ(results.size(), 2u);
        REQUIRE_EQ(results[1].size(), 1u);
        CHECK_EQ(results[1][0], true);

        // 信号保持不变不会触发
        scheduler.loop_once();
        CHECK_EQ(results.size(), 2u);

        // 第三次上升沿触发，任务结束
        clk.value = 0u;
        scheduler.loop_once();
        CHECK_EQ(results.size(), 2u);
        clk.value = 1u;
        scheduler.loop_once();
        REQUIRE_EQ(results.size(), 3u);
        REQUIRE_EQ(results[2].size(), 1u);
        CHECK_EQ(results[2][0], true);
    }

    TEST_CASE("select_clock wakes when any tracked clock triggers")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        signal_state clk_a{};
        signal_state clk_b{};
        ::std::vector<::std::vector<bool>> results;

        ::verilator_utils::select_clock clock_selector{};
        clock_selector.add_clock(::verilator_utils::bit_slice<::CData>{clk_a.value}, ::verilator_utils::edge_enum::rising);
        clock_selector.add_clock(::verilator_utils::bit_slice<::CData>{clk_b.value}, ::verilator_utils::edge_enum::falling);

        auto selector_task{[&](this auto) -> ::verilator_utils::task<void> {
            for(auto i{0zu}; i != 2; ++i)
            {
                auto triggered{co_await clock_selector};
                results.emplace_back(triggered | ::std::ranges::to<::std::vector<bool>>());
            }
        }()};
        scheduler.add_task(::std::move(selector_task));

        scheduler.loop_once();
        CHECK_EQ(results.size(), 0u);

        // 仅 clk_a 触发上升沿，clk_b 无下降沿
        clk_a.value = 1u;
        scheduler.loop_once();
        REQUIRE_EQ(results.size(), 1u);
        REQUIRE_EQ(results[0].size(), 2u);
        CHECK_EQ(results[0][0], true);
        CHECK_EQ(results[0][1], false);

        // clk_b 先拉高，无下降沿不触发
        clk_b.value = 1u;
        scheduler.loop_once();
        CHECK_EQ(results.size(), 1u);

        // clk_b 下降沿触发，clk_a 无上升沿
        clk_b.value = 0u;
        scheduler.loop_once();
        REQUIRE_EQ(results.size(), 2u);
        REQUIRE_EQ(results[1].size(), 2u);
        CHECK_EQ(results[1][0], false);
        CHECK_EQ(results[1][1], true);
    }

    TEST_CASE("select_clock reports all clocks triggered simultaneously")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        signal_state clk_a{};
        signal_state clk_b{};
        ::std::vector<bool> results;

        ::verilator_utils::select_clock clock_selector{};
        clock_selector.add_clock(::verilator_utils::bit_slice<::CData>{clk_a.value}, ::verilator_utils::edge_enum::rising);
        clock_selector.add_clock(::verilator_utils::bit_slice<::CData>{clk_b.value}, ::verilator_utils::edge_enum::rising);

        auto selector_task{[&](this auto) -> ::verilator_utils::task<void> {
            auto triggered{co_await clock_selector};
            results = triggered | ::std::ranges::to<::std::vector<bool>>();
        }()};
        scheduler.add_task(::std::move(selector_task));

        scheduler.loop_once();
        CHECK(results.empty());

        clk_a.value = 1u;
        clk_b.value = 1u;
        scheduler.loop_once();
        REQUIRE_EQ(results.size(), 2u);
        CHECK_EQ(results[0], true);
        CHECK_EQ(results[1], true);
    }

    TEST_CASE("select_clock completes immediately when the edge already occurred")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        signal_state clk{};
        ::std::vector<bool> results;

        ::verilator_utils::select_clock clock_selector{};
        clock_selector.add_clock(::verilator_utils::bit_slice<::CData>{clk.value}, ::verilator_utils::edge_enum::rising);

        // 检测器创建后、任务运行前时钟已跳变，等待时无需挂起
        clk.value = 1u;

        auto selector_task{[&](this auto) -> ::verilator_utils::task<void> {
            auto triggered{co_await clock_selector};
            results = triggered | ::std::ranges::to<::std::vector<bool>>();
        }()};
        scheduler.add_task(::std::move(selector_task));

        scheduler.loop_once();
        REQUIRE_EQ(results.size(), 1u);
        CHECK_EQ(results[0], true);
    }

    TEST_CASE("verify_at keeps polling the event callback at each clock edge until it reports ready")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        signal_state clk{};
        ::verilator_utils::bit_slice<::CData> clk_ref{clk.value};
        // 事件回调在第2个上升沿才报告就绪，因此该用例同时覆盖首个边沿未就绪的路径
        constexpr static ::std::size_t ready_at_edge{2};
        ::std::size_t edge_count{};
        ::std::size_t verify_count{};
        ::std::string verify_time{};
        ::std::vector<::CData> event_clock_values;

        const auto&& task{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::verify_at(
                    clk_ref,
                    [&] {
                        ++edge_count;
                        event_clock_values.push_back(clk.value);
                        return edge_count >= ready_at_edge;
                    },
                    [&] {
                        ++verify_count;
                        verify_time = scheduler.time_in_string();
                    });
            },
        };

        scheduler.add_task(task());
        CHECK_EQ(verify_count, 0u);

        // 第1个上升沿：事件回调未就绪，验证不应执行，任务继续等待
        step_clock(scheduler, clk);
        CHECK_EQ(edge_count, 1u);
        CHECK_EQ(verify_count, 0u);
        CHECK_FALSE(scheduler.empty());

        // 第2个上升沿：事件回调报告就绪，验证在该边沿执行
        step_clock(scheduler, clk, 0u);
        step_clock(scheduler, clk);
        CHECK_EQ(edge_count, 2u);
        CHECK_EQ(verify_count, 1u);
        CHECK_EQ(verify_time, "3ns"sv);
        CHECK(scheduler.empty());

        // 验证只执行一次，任务结束后信号继续变化也不再触发
        step_clock(scheduler, clk, 0u);
        step_clock(scheduler, clk);
        CHECK_EQ(verify_count, 1u);

        // 事件回调只在上升沿求值，且每个边沿至多一次
        CHECK_EQ(event_clock_values, (::std::vector<::CData>{1u, 1u}));
    }

    TEST_CASE("verify_at waits for a clock edge before evaluating the event callback")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        signal_state clk{};
        ::verilator_utils::bit_slice<::CData> clk_ref{clk.value};
        ::std::size_t event_count{};
        ::std::size_t verify_count{};
        bool clock_high_during_verify{};
        ::verilator_utils::eval_scheduler::eval_stage_enum verify_stage{};

        const auto&& task{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::verify_at(
                    clk_ref,
                    [&] {
                        ++event_count;
                        return true;
                    },
                    [&] {
                        ++verify_count;
                        clock_high_during_verify = clk.value == 1;
                        verify_stage = scheduler.eval_stage();
                    });
            },
        };

        scheduler.add_task(task());

        // 即使事件回调立即就绪，也必须先等待一个时钟边沿
        CHECK_EQ(event_count, 0u);
        CHECK_EQ(verify_count, 0u);
        step_clock(scheduler, clk);
        CHECK_EQ(event_count, 1u);
        CHECK_EQ(verify_count, 1u);
        CHECK(clock_high_during_verify);
        // 验证发生在电路评估完成后，即默认评估阶段
        CHECK_EQ(verify_stage, ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval);
        CHECK(scheduler.empty());

        // 任务结束后不再重复触发
        step_clock(scheduler, clk, 0u);
        step_clock(scheduler, clk);
        CHECK_EQ(verify_count, 1u);
    }

    TEST_CASE("verify_at honors the requested edge polarity for a level signal")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        signal_state clk{};
        clk.value = 1;
        ::verilator_utils::bit_slice<::CData> clk_ref{clk.value};
        ::std::vector<::CData> sampled_clock;

        const auto&& task{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::verify_at(
                    clk_ref,
                    [] { return true; },
                    [&] { sampled_clock.push_back(clk.value); },
                    ::verilator_utils::edge_enum::falling);
            },
        };

        scheduler.add_task(task());
        CHECK(sampled_clock.empty());

        // 下降沿才是该重载的触发时机，验证在时钟处于低电平时执行
        step_clock(scheduler, clk, 0u);
        CHECK_EQ(sampled_clock, ::std::vector<::CData>{0u});
        CHECK(scheduler.empty());

        step_clock(scheduler, clk);
        step_clock(scheduler, clk, 0u);
        CHECK_EQ(sampled_clock, ::std::vector<::CData>{0u});
    }

    TEST_CASE("verify_at waits for the requested number of edges before running the callback")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        signal_state clk{};
        ::verilator_utils::bit_slice<::CData> clk_ref{clk.value};
        ::std::size_t verify_count{};
        ::verilator_utils::eval_scheduler::eval_stage_enum verify_stage{};

        const auto&& task{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::verify_at(
                    clk_ref,
                    [&] {
                        ++verify_count;
                        verify_stage = scheduler.eval_stage();
                    },
                    2);
            },
        };

        scheduler.add_task(task());
        CHECK_EQ(verify_count, 0u);

        // 第1个上升沿：还未达到请求的边沿数，回调不执行
        step_clock(scheduler, clk);
        CHECK_EQ(verify_count, 0u);
        CHECK_FALSE(scheduler.empty());

        // 第2个上升沿：回调在电路评估完成后执行且只执行一次
        step_clock(scheduler, clk, 0u);
        step_clock(scheduler, clk);
        CHECK_EQ(verify_count, 1u);
        CHECK_EQ(verify_stage, ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval);
        CHECK(scheduler.empty());

        step_clock(scheduler, clk, 0u);
        step_clock(scheduler, clk);
        CHECK_EQ(verify_count, 1u);
    }

    TEST_CASE("wait_reset_finish returns once the reset signal is deasserted")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        signal_state rst{};
        rst.value = 1;
        ::verilator_utils::bit_slice<::CData> rst_ref{rst.value};
        bool finished{};

        const auto reset_lambda{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_reset_finish(rst_ref);
                finished = true;
            },
        };

        scheduler.add_task(reset_lambda());
        scheduler.initial_eval();
        CHECK_FALSE(finished);

        // 复位仍然有效时事件回调不就绪
        scheduler.loop_once();
        CHECK_FALSE(finished);

        // 释放复位后事件回调就绪，等待任务继续执行
        rst.value = 0;
        scheduler.loop_once();
        CHECK(finished);
    }

    TEST_CASE("wait_reset_finish honors the active-low polarity")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        signal_state rst{};
        rst.value = 1;
        ::verilator_utils::bit_slice<::CData> rst_ref{rst.value};
        bool finished{};

        const auto reset_lambda{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_reset_finish(rst_ref, false);
                finished = true;
            },
        };

        scheduler.add_task(reset_lambda());
        scheduler.initial_eval();
        // 低电平有效的复位在信号为高时已经完成
        scheduler.loop_once();
        CHECK(finished);
    }

    TEST_CASE("tasks read the simulation time in dut time units and precision")
    {
        scheduler_fixture fixture{-9, -12};
        auto scheduler{fixture.make_scheduler()};
        double time_in_unit{};
        ::std::uint64_t time_in_precision{};
        ::std::string time_in_string{};

        const auto&& task{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_time(3_ns);
                time_in_unit = co_await ::verilator_utils::time_in_time_unit();
                time_in_precision = co_await ::verilator_utils::time_in_time_precision();
                time_in_string = co_await ::verilator_utils::time_in_string();
            },
        };

        scheduler.add_task(task());
        scheduler.loop_until_finish();

        // 时间精度为ps，时间单位为ns
        CHECK_EQ(time_in_precision, 3'000u);
        CHECK_EQ(time_in_unit, 3.0);
        CHECK_EQ(time_in_string, "3ns"sv);
    }

    TEST_CASE("wait durations round to the nearest time precision with ties to even")
    {
        // 时间精度为ps
        scheduler_fixture fixture{-9, -12};
        auto scheduler{fixture.make_scheduler()};
        ::std::vector<::std::uint64_t> resumed_times{};

        const auto&& task{
            [&] -> ::verilator_utils::task<void> {
                // 1.5ps按四舍六入五成双向偶数舍入为2ps
                co_await ::verilator_utils::wait_time(1'500_fs);
                resumed_times.push_back(scheduler.time_in_time_precision());
                // 1.5ps从2ps继续，舍入为2ps
                co_await ::verilator_utils::wait_time(1'500_fs);
                resumed_times.push_back(scheduler.time_in_time_precision());
                // 2.5ps位于两个偶数之间，向偶数舍入为2ps
                co_await ::verilator_utils::wait_time(2'500_fs);
                resumed_times.push_back(scheduler.time_in_time_precision());
                // 1.4ps与1.6ps分别向更近的整数舍入
                co_await ::verilator_utils::wait_time(1'400_fs);
                resumed_times.push_back(scheduler.time_in_time_precision());
                co_await ::verilator_utils::wait_time(1'600_fs);
                resumed_times.push_back(scheduler.time_in_time_precision());
            },
        };

        scheduler.add_task(task());
        scheduler.loop_until_finish();

        CHECK_EQ(resumed_times, (::std::vector<::std::uint64_t>{2u, 4u, 6u, 7u, 9u}));
    }
}
