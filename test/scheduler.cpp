#include "doctest_macros.hpp"
import verilator_utils.full;

namespace
{
    using namespace ::verilator_utils::verilator;

    struct fake_dut final : ::VerilatedModel
    {
        explicit fake_dut(::VerilatedContext& context) : ::VerilatedModel{context} {}

        void eval() {}

        [[nodiscard]] const char* hierName() const final { return "fake_dut"; }

        [[nodiscard]] const char* modelName() const final { return "fake_dut"; }

        [[nodiscard]] unsigned threads() const final { return 1u; }

        void prepareClone() const { contextp()->prepareClone(); }

        void atClone() const { contextp()->threadPoolpOnClone(); }
    };

    struct scheduler_fixture
    {
        ::VerilatedContext context{};
        fake_dut dut{context};

        explicit scheduler_fixture(::std::int32_t time_unit = -9, ::std::int32_t time_precision = -12)
        {
            context.timeunit(time_unit);
            context.timeprecision(time_precision);
        }

        [[nodiscard]] ::verilator_utils::eval_scheduler make_scheduler() noexcept
        { return ::verilator_utils::eval_scheduler{dut}; }
    };

    struct signal_state
    { ::CData value{}; };
}  // namespace

TEST_SUITE("verilator_utils/scheduler")
{
    using namespace ::verilator_utils::literals;

    TEST_CASE("task supports handle access and nested await")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        auto child{[](this auto) -> ::verilator_utils::task<void> { co_return; }()};

        ::verilator_utils::task<void>::handle_t parent_handle;
        const auto parent{[&] -> ::verilator_utils::task<void>
                          {
                              auto handle{co_await ::verilator_utils::get_handle<::verilator_utils::task<void>::promise_type>()};
                              CHECK(handle);
                              parent_handle = handle;
                              CHECK_EQ(handle.promise().status, ::verilator_utils::task<void>::status_enum::running);
                              co_await child;
                          }};
        auto parent_task{parent()};
        auto expected_parent_handle{parent_task.get_handle()};

        CHECK(parent_task);
        CHECK(child);
        CHECK_EQ(parent_task.get_promise().status, ::verilator_utils::task<void>::status_enum::initial_suspend);
        CHECK_EQ(child.get_promise().status, ::verilator_utils::task<void>::status_enum::initial_suspend);
        scheduler.add_task(::std::move(parent_task));
        scheduler.loop_until_finish();
        CHECK_EQ(parent_handle, expected_parent_handle);
        CHECK(child.done());
    }

    TEST_CASE("task returns values from nested coroutines after suspension")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        int result{};

        auto child{[](this auto) -> ::verilator_utils::task<int>
                   {
                       co_await ::verilator_utils::wait_time(2_ps);
                       co_return 42;
                   }()};
        const auto parent{[&] -> ::verilator_utils::task<void> { result = co_await child; }};
        scheduler.add_task(parent());

        scheduler.loop_until_finish();
        CHECK_EQ(result, 42);
        CHECK_EQ(scheduler.time_in_time_precision(), 2u);
        CHECK(child.done());
    }

    TEST_CASE("task transfers move-only return values")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::std::unique_ptr<int> result;

        auto child{[](this auto) -> ::verilator_utils::task<::std::unique_ptr<int>> { co_return ::std::make_unique<int>(17); }()};
        const auto parent{[&] -> ::verilator_utils::task<void> { result = co_await child; }};
        scheduler.add_task(parent());

        scheduler.loop_until_finish();
        REQUIRE(result);
        CHECK_EQ(*result, 17);
        CHECK(child.done());
    }

    TEST_CASE("task preserves mutable references through suspension and nested awaits")
    {
        static_assert(::std::same_as<::verilator_utils::task<int&>::promise_type::return_type, int&>);

        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        int value{17};
        int* result{};

        auto child{[&](this auto) -> ::verilator_utils::task<int&>
                   {
                       co_await ::verilator_utils::wait_time(1_ps);
                       co_return value;
                   }()};
        auto forwarding_task{[&](this auto) -> ::verilator_utils::task<int&> { co_return co_await child; }()};
        const auto parent{[&] -> ::verilator_utils::task<void>
                          {
                              int& reference{co_await forwarding_task};
                              result = ::std::addressof(reference);
                              reference = 23;
                          }};
        scheduler.add_task(parent());

        scheduler.loop_until_finish();
        CHECK_EQ(result, ::std::addressof(value));
        CHECK_EQ(value, 23);
        CHECK(child.done());
        CHECK(forwarding_task.done());
        CHECK_EQ(::std::addressof(child.get_promise().get_result()), ::std::addressof(value));
        CHECK_EQ(::std::addressof(forwarding_task.get_promise().get_result()), ::std::addressof(value));
    }

    TEST_CASE("task preserves const references")
    {
        static_assert(::std::same_as<::verilator_utils::task<const int&>::promise_type::return_type, const int&>);

        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        const int value{31};
        const int* result{};

        auto child{[&](this auto) -> ::verilator_utils::task<const int&> { co_return value; }()};
        const auto parent{[&] -> ::verilator_utils::task<void>
                          {
                              const int& reference{co_await child};
                              result = ::std::addressof(reference);
                          }};
        scheduler.add_task(parent());

        scheduler.loop_until_finish();
        CHECK_EQ(result, ::std::addressof(value));
        CHECK_EQ(*result, 31);
        CHECK(child.done());
        CHECK_EQ(::std::addressof(child.get_promise().get_result()), ::std::addressof(value));
    }

    TEST_CASE("value-returning task propagates exceptions to its parent")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool observed_exception{};
        bool consumed_result{};

        auto child{[](this auto) -> ::verilator_utils::task<int>
                   {
                       co_await ::verilator_utils::wait_time(1_ps);
                       throw ::std::runtime_error{"value task failure"};
                       co_return 0;
                   }()};
        const auto parent{[&] -> ::verilator_utils::task<void>
                          {
                              try
                              {
                                  static_cast<void>(co_await child);
                                  consumed_result = true;
                              }
                              catch(const ::std::runtime_error& exception)
                              {
                                  observed_exception = ::std::string_view{exception.what()} == "value task failure";
                              }
                          }};
        scheduler.add_task(parent());

        scheduler.loop_until_finish();
        CHECK(observed_exception);
        CHECK_FALSE(consumed_result);
        CHECK(child.done());
    }

    TEST_CASE("task supports move construction assignment detach and destroy")
    {
        auto task{[](this auto) -> ::verilator_utils::task<void> { co_return; }()};
        auto original_handle{task.get_handle()};

        ::verilator_utils::task<void> moved{::std::move(task)};
        // The moved-from state is part of task's move-construction contract.
        // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved)
        CHECK_FALSE(task);
        CHECK(moved);
        CHECK_EQ(moved.get_handle(), original_handle);

        ::verilator_utils::task<void> assigned{[](this auto) -> ::verilator_utils::task<void> { co_return; }()};
        original_handle = assigned.get_handle();
        auto detached_handle{assigned.detach()};
        CHECK_FALSE(assigned);
        CHECK_EQ(detached_handle, original_handle);
        detached_handle.destroy();

        auto destroy_task{[](this auto) -> ::verilator_utils::task<void> { co_return; }()};
        destroy_task.destroy();
        CHECK_FALSE(destroy_task);
    }

    TEST_CASE("task records regular exceptions and ignores finish exceptions when rethrowing")
    {
        auto failing_task{[](this auto) -> ::verilator_utils::task<void>
                          {
                              throw ::std::runtime_error{"regular failure"};
                              co_return;
                          }()};
        failing_task.resume();
        CHECK(failing_task.done());
        CHECK(failing_task.get_promise().with_unhandled_exception());
        CHECK_THROWS_AS(failing_task.rethrow_exception(), ::std::runtime_error);

        auto finish_task{[](this auto) -> ::verilator_utils::task<void>
                         {
                             throw ::verilator_utils::eval_finish_exception{};
                             co_return;
                         }()};
        finish_task.resume();
        CHECK(finish_task.done());
        CHECK(finish_task.get_promise().is_eval_finish_exception);
        CHECK_FALSE(finish_task.get_promise().with_unhandled_exception());
        CHECK_NOTHROW(finish_task.rethrow_exception());
    }

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

        CHECK_EQ(detector.get_edge_to_detect(), ::verilator_utils::edge_detector::rising);
        CHECK_EQ(detector.set_edge_to_detect(::verilator_utils::edge_detector::falling),
                 ::verilator_utils::edge_detector::rising);
        CHECK_EQ(detector.get_edge_to_detect(), ::verilator_utils::edge_detector::falling);

        signal.value = 1u;
        CHECK_FALSE(detector());
        signal.value = 0u;
        CHECK(detector());
    }

    TEST_CASE("time waits advance the simulated time and format correctly")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};

        CHECK_EQ(scheduler.time_in_time_precision(), 0u);
        CHECK_EQ(scheduler.time_in_time_unit(), 0.0);
        CHECK_EQ(scheduler.time_in_string(), "0ns");

        const auto&& task{
            [&] -> ::verilator_utils::task<void>
            {
                co_await ::verilator_utils::wait_time(5_ps);
                CHECK_EQ(scheduler.time_in_time_precision(), 5u);
                CHECK_EQ(scheduler.time_in_string(), "0.005ns");
                co_await ::verilator_utils::wait_time(2_ns);
                CHECK_EQ(scheduler.time_in_time_precision(), 2'005u);
                CHECK_EQ(scheduler.time_in_string(), "2.005ns");
            },
        };

        scheduler.add_task(task());
        scheduler.loop_until_finish();
    }

    TEST_CASE("time formatting keeps the unit selected by timeunit")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        fixture.context.timeunit(-12);

        const auto&& task{[&] -> ::verilator_utils::task<void>
                          {
                              CHECK_EQ(scheduler.time_in_string(), "1ns");
                              co_await ::verilator_utils::wait_time(999_ns);
                              CHECK_EQ(scheduler.time_in_string(), "1000ns");
                          }};

        fixture.context.time(1'000u);
        scheduler.add_task(task());
        scheduler.loop_until_finish();
    }

    TEST_CASE("time formatting groups decimal timeunits by SI unit")
    {
        struct test_case
        {
            ::std::int32_t time_unit;
            ::std::string_view expected;
        };

        constexpr static ::std::array test_cases{
            test_case{-7,  "100ns"},
            test_case{-8,  "10ns" },
            test_case{-9,  "1ns"  },
            test_case{-10, "100ps"},
            test_case{-11, "10ps" },
            test_case{-12, "1ps"  },
        };

        for(auto&& [time_unit, expected]: test_cases)
        {
            CAPTURE(time_unit);
            scheduler_fixture fixture{time_unit, time_unit};
            auto scheduler{fixture.make_scheduler()};
            fixture.context.time(1u);
            CHECK_EQ(scheduler.time_in_string(), expected);
        }
    }

    TEST_CASE("scheduler uses configured time unit for normalized time")
    {
        scheduler_fixture fixture{-6, -12};
        auto scheduler{fixture.make_scheduler()};

        const auto&& task{
            [&] -> ::verilator_utils::task<void>
            {
                co_await ::verilator_utils::wait_time(1'000_ns);
                CHECK_EQ(scheduler.time_in_time_precision(), 1'000'000u);
                CHECK_EQ(scheduler.time_in_time_unit(), doctest::Approx{1.0});
                CHECK_EQ(scheduler.time_in_string(), "1us");
            },
        };

        scheduler.add_task(task());
        scheduler.loop_until_finish();
    }

    TEST_CASE("femtosecond waits accept aligned values")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};

        const auto&& task{
            [&] -> ::verilator_utils::task<void>
            {
                co_await ::verilator_utils::wait_time(2_ps);
                CHECK_EQ(scheduler.time_in_time_precision(), 2u);
            },
        };

        scheduler.add_task(task());
        scheduler.loop_until_finish();
    }

    TEST_CASE("wait queue resumes tasks by target time and groups equal deadlines")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::std::vector<::std::uint64_t> observed_times;
        ::std::vector<int> completed_tasks;

        const auto task_a{
            [&] -> ::verilator_utils::task<void>
            {
                co_await ::verilator_utils::wait_time(3_ps);
                observed_times.push_back(scheduler.time_in_time_precision());
                completed_tasks.push_back(1);
            },
        };
        const auto task_b{
            [&] -> ::verilator_utils::task<void>
            {
                co_await ::verilator_utils::wait_time(1_ps);
                observed_times.push_back(scheduler.time_in_time_precision());
                completed_tasks.push_back(2);
            },
        };
        const auto task_c{
            [&] -> ::verilator_utils::task<void>
            {
                co_await ::verilator_utils::wait_time(3_ps);
                observed_times.push_back(scheduler.time_in_time_precision());
                completed_tasks.push_back(3);
            },
        };

        scheduler.add_task(task_a());
        scheduler.add_task(task_b());
        scheduler.add_task(task_c());

        scheduler.loop_once();
        CHECK_EQ(observed_times, ::std::vector<::std::uint64_t>{1u});
        CHECK_EQ(completed_tasks, ::std::vector<int>{2});
        scheduler.loop_once();
        CHECK_EQ(observed_times, (::std::vector<::std::uint64_t>{1u, 3u, 3u}));
        CHECK_EQ(completed_tasks.size(), 3u);
        CHECK(::std::ranges::contains(completed_tasks, 1));
        CHECK(::std::ranges::contains(completed_tasks, 3));
    }

    TEST_CASE("event waits wake when callback becomes ready")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        signal_state signal{};
        bool observed_ready{};

        const auto&& task{
            [&] -> ::verilator_utils::task<void>
            {
                co_await ::verilator_utils::wait_event(
                    [&]
                    {
                        observed_ready = signal.value != 0;
                        return signal.value != 0;
                    });
                CHECK(observed_ready);
                CHECK(signal.value != 0);
            },
        };

        scheduler.add_task(task());
        scheduler.loop_once();
        signal.value = 1;
        scheduler.loop_once();
    }

    TEST_CASE("event queue wakes multiple ready tasks in one evaluation")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool event_ready{};
        ::std::vector<int> resumed_tasks;

        const auto&& make_task{
            [&](int task_id) -> ::verilator_utils::task<void>
            {
                co_await ::verilator_utils::wait_event([&event_ready] { return event_ready; });
                resumed_tasks.push_back(task_id);
            },
        };

        scheduler.add_task(make_task(1));
        scheduler.add_task(make_task(2));

        scheduler.loop_once();
        CHECK(resumed_tasks.empty());
        event_ready = true;
        scheduler.loop_once();

        CHECK_EQ(resumed_tasks.size(), 2u);
        CHECK(::std::ranges::contains(resumed_tasks, 1));
        CHECK(::std::ranges::contains(resumed_tasks, 2));
        CHECK(scheduler.empty());
    }

    TEST_CASE("event waits that are immediately ready do not enter the scheduler queue")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool resumed{};

        const auto&& task{
            [&] -> ::verilator_utils::task<void>
            {
                co_await ::verilator_utils::wait_event([] { return true; });
                resumed = true;
            },
        };

        scheduler.add_task(task());
        scheduler.loop_once();
        CHECK(resumed);
        CHECK(scheduler.empty());
    }

    TEST_CASE("wait_stimulate waits for the requested falling edges")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::CData clk{};
        ::std::uint64_t resumed_times{};
        ::verilator_utils::bit_slice clk_ref{clk};

        const auto&& stimulus_task{
            [&] -> ::verilator_utils::task<void>
            {
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

    TEST_CASE("stage waits observe scheduler phases")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool seen_before_eval{};
        bool seen_after_eval{};

        const auto&& task{
            [&] -> ::verilator_utils::task<void>
            {
                co_await ::verilator_utils::wait_eval_stage(::verilator_utils::eval_scheduler::eval_stage_enum::before_dut_eval);
                seen_before_eval = true;
                co_await ::verilator_utils::wait_eval_stage(::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval);
                seen_after_eval = true;
            },
        };

        scheduler.add_task(task());
        scheduler.loop_once();
        CHECK(seen_before_eval);
        CHECK(seen_after_eval);
    }

    TEST_CASE("on_dut_eval is observable from dut eval without scheduling")
    {
        scheduler_fixture fixture{};
        bool seen_on_dut_eval{};

        struct observing_dut final : ::VerilatedModel
        {
            explicit observing_dut(::VerilatedContext& context,
                                   ::verilator_utils::eval_scheduler* scheduler,
                                   bool* seen_on_dut_eval) :
                ::VerilatedModel{context}, scheduler{scheduler}, seen_on_dut_eval{seen_on_dut_eval}
            {
            }

            void eval() const
            {
                CHECK_EQ(scheduler->get_eval_stage(), ::verilator_utils::eval_scheduler::eval_stage_enum::on_dut_eval);
                *seen_on_dut_eval = true;
            }

            [[nodiscard]] const char* hierName() const final { return "observing_dut"; }

            [[nodiscard]] const char* modelName() const final { return "observing_dut"; }

            [[nodiscard]] unsigned threads() const final { return 1u; }

            ::verilator_utils::eval_scheduler* scheduler;
            bool* seen_on_dut_eval;
        } dut{fixture.context, nullptr, &seen_on_dut_eval};

        auto scheduler{::verilator_utils::eval_scheduler{dut}};
        dut.scheduler = &scheduler;

        scheduler.loop_once();
        CHECK(seen_on_dut_eval);
        CHECK_EQ(scheduler.get_eval_stage(), ::verilator_utils::eval_scheduler::eval_stage_enum::eval_end);
    }

    TEST_CASE("stage waits do not interfere with on_dut_eval observation")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool seen_before_eval{};
        bool seen_after_eval{};

        const auto&& task{
            [&] -> ::verilator_utils::task<void>
            {
                co_await ::verilator_utils::wait_eval_stage(::verilator_utils::eval_scheduler::eval_stage_enum::before_dut_eval);
                seen_before_eval = true;
                co_await ::verilator_utils::wait_eval_stage(::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval);
                seen_after_eval = true;
            },
        };

        scheduler.add_task(task());
        scheduler.loop_once();
        CHECK(seen_before_eval);
        CHECK(seen_after_eval);
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
            [&] -> ::verilator_utils::task<void>
            {
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
        scheduler.loop_until_finish();

        CHECK_EQ(scheduler.time_in_time_precision(), 4u);
        CHECK(scheduler.is_finish());
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
            [&] -> ::verilator_utils::task<void>
            {
                co_await ::verilator_utils::wait_stimulate(clk_ref);
                falling_stage = scheduler.get_eval_stage();
            },
        };
        const auto rising_task{
            [&] -> ::verilator_utils::task<void>
            {
                co_await ::verilator_utils::wait_stimulate(clk_ref, 1, ::verilator_utils::edge_enum::rising);
                rising_stage = scheduler.get_eval_stage();
            },
        };
        scheduler.add_task(falling_task());
        scheduler.add_task(rising_task());

        scheduler.loop_once();
        clk = 1;
        scheduler.loop_once();
        CHECK_EQ(rising_stage, ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval);
        clk = 0;
        scheduler.loop_once();
        CHECK_EQ(falling_stage, ::verilator_utils::eval_scheduler::eval_stage_enum::before_dut_eval);
    }

    TEST_CASE("scheduler exposes stage transitions and empty state")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};

        CHECK(scheduler.empty());
        CHECK_EQ(scheduler.get_eval_stage(), ::verilator_utils::eval_scheduler::eval_stage_enum::not_begin);
        scheduler.loop_once();
        CHECK(scheduler.empty());
        CHECK_EQ(scheduler.get_eval_stage(), ::verilator_utils::eval_scheduler::eval_stage_enum::eval_end);
        CHECK_FALSE(scheduler.is_finish());
        scheduler.finish();
        CHECK(scheduler.is_finish());
        CHECK_THROWS_AS(scheduler.throw_if_finish(), ::verilator_utils::eval_finish_exception);
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
            [&] -> ::verilator_utils::task<void>
            {
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

    TEST_CASE("root coroutine resumes after a synchronous child completes")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool resumed{};
        auto child{[](this auto) -> ::verilator_utils::task<void> { co_return; }()};
        const auto&& root{
            [&] -> ::verilator_utils::task<void>
            {
                co_await child;
                resumed = true;
            },
        };

        scheduler.add_task(root());
        scheduler.loop_once();
        CHECK(resumed);
        CHECK(child.done());
        CHECK(scheduler.empty());
    }

    TEST_CASE("async_task created inside a coroutine resumes its parent and propagates exceptions")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool completed{};
        bool caught{};

        const auto successful_child{[] -> ::verilator_utils::task<void> { co_await ::verilator_utils::wait_time(1_ps); }};
        const auto failing_child{
            [] -> ::verilator_utils::task<void>
            {
                co_await ::verilator_utils::wait_time(1_ps);
                throw ::std::runtime_error{"async child failure"};
            },
        };

        const auto& parent{
            [&] -> ::verilator_utils::task<void>
            {
                auto pool{co_await ::verilator_utils::get_spawn_pool()};
                pool.add_task(successful_child());
                co_await pool.join_any();
                completed = true;

                pool.add_task(failing_child());
                try
                {
                    co_await pool.join_any();
                }
                catch(const ::std::runtime_error& exception)
                {
                    caught = exception.what() == ::std::string{"async child failure"};
                }
            },
        };

        scheduler.add_task(parent());
        scheduler.loop_until_finish();
        CHECK(completed);
        CHECK(caught);
    }

    TEST_CASE("spawn_pool join_all waits for every child and collects exceptions")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool joined{};
        ::std::size_t exception_count{};

        const auto successful_child{[] -> ::verilator_utils::task<void> { co_await ::verilator_utils::wait_time(1_ps); }};
        const auto first_failing_child{
            [] -> ::verilator_utils::task<void>
            {
                co_await ::verilator_utils::wait_time(2_ps);
                throw ::std::runtime_error{"first failure"};
            },
        };
        const auto second_failing_child{
            [] -> ::verilator_utils::task<void>
            {
                co_await ::verilator_utils::wait_time(3_ps);
                throw ::std::logic_error{"second failure"};
            },
        };

        const auto parent{
            [&] -> ::verilator_utils::task<void>
            {
                auto pool{co_await ::verilator_utils::get_spawn_pool()};
                pool.add_task(successful_child());
                pool.add_task(first_failing_child());
                pool.add_task(second_failing_child());
                try
                {
                    co_await pool.join_all();
                }
                catch(const ::std::vector<::std::exception_ptr>& exceptions)
                {
                    exception_count = exceptions.size();
                }
                joined = pool.empty();
            },
        };
        scheduler.add_task(parent());
        scheduler.loop_until_finish();
        CHECK(joined);
        CHECK_EQ(exception_count, 2u);
    }

    TEST_CASE("spawn_pool join_any removes the completed child regardless of position")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::std::vector<int> completed;
        bool joined{};

        const auto first_child{
            [&] -> ::verilator_utils::task<void>
            {
                co_await ::verilator_utils::wait_time(2_ps);
                completed.emplace_back(1);
            },
        };
        const auto second_child{
            [&] -> ::verilator_utils::task<void>
            {
                co_await ::verilator_utils::wait_time(1_ps);
                completed.emplace_back(2);
            },
        };

        const auto parent{
            [&] -> ::verilator_utils::task<void>
            {
                auto pool{co_await ::verilator_utils::get_spawn_pool()};
                pool.add_task(first_child());
                pool.add_task(second_child());
                co_await pool.join_any();
                co_await pool.join_any();
                joined = pool.empty();
            },
        };
        scheduler.add_task(parent());
        scheduler.loop_until_finish();
        CHECK(joined);
        CHECK_EQ(completed, (::std::vector<int>{2, 1}));
    }

    TEST_CASE("finish cooperatively cancels waiting tasks")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool resumed{};

        const auto task{
            [&] -> ::verilator_utils::task<void>
            {
                try
                {
                    co_await ::verilator_utils::wait_time(10_ps);
                }
                catch(const ::verilator_utils::eval_finish_exception&)
                {
                    resumed = true;
                    throw;
                }
            },
        };

        scheduler.add_task(task());
        scheduler.finish();
        scheduler.loop_once();
        CHECK(resumed);
    }
}
