#include <doctest_macros.hpp>
import unit_test;

namespace
{
    /**
     * @brief 返回bool值的可等待体，用于测试await_suspend的bool分支
     *
     */
    struct bool_suspend_awaiter
    {
        bool do_suspend;

        static bool await_ready() noexcept { return false; }

        bool await_suspend(auto /* handle */) noexcept { return do_suspend; }

        static void await_resume() noexcept {}
    };

    /**
     * @brief 判断挂起点位置是否为空（等价于未记录挂起点）
     *
     */
    [[nodiscard]] bool is_default_suspend_location(::std::source_location location) noexcept
    { return location.line() == 0u && location.column() == 0u && ::std::string_view{location.file_name()}.empty(); }

}  // namespace

TEST_SUITE("verilator_utils/scheduler")
{
    using namespace ::verilator_utils::literals;

    TEST_CASE("task supports handle access and nested await")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        auto child{[] -> ::verilator_utils::task<void> { co_return; }()};

        ::verilator_utils::task<void>::handle_t parent_handle;
        const auto parent{[&] -> ::verilator_utils::task<void> {
            const auto handle{co_await ::verilator_utils::get_handle<::verilator_utils::task<void>::promise_type>()};
            CHECK(handle);
            parent_handle = handle;
            CHECK_EQ(handle.promise().status, ::verilator_utils::task<void>::status_enum::running);
            co_await child;
        }};
        auto parent_task{parent()};
        const auto expected_parent_handle{parent_task.get_handle()};

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

        auto child{[] -> ::verilator_utils::task<int> {
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

        auto child{[] -> ::verilator_utils::task<::std::unique_ptr<int>> { co_return ::std::make_unique<int>(17); }()};
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

        auto child{[&](this auto) -> ::verilator_utils::task<int&> {
            co_await ::verilator_utils::wait_time(1_ps);
            co_return value;
        }()};
        auto forwarding_task{[&](this auto) -> ::verilator_utils::task<int&> { co_return co_await child; }()};
        const auto parent{[&] -> ::verilator_utils::task<void> {
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
        const auto parent{[&] -> ::verilator_utils::task<void> {
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

        auto child{[] -> ::verilator_utils::task<int> {
            co_await ::verilator_utils::wait_time(1_ps);
            throw ::std::runtime_error{"value task failure"};
            co_return 0;
        }()};
        const auto parent{[&] -> ::verilator_utils::task<void> {
            try
            {
                static_cast<void>(co_await child);
                consumed_result = true;
            }
            catch(const ::std::runtime_error& exception)
            {
                observed_exception = ::std::string_view{exception.what()} == "value task failure"sv;
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
        auto task{[] -> ::verilator_utils::task<void> { co_return; }()};
        auto original_handle{task.get_handle()};

        const ::verilator_utils::task<void> moved{::std::move(task)};
        // The moved-from state is part of task's move-construction contract.
        // NOLINTNEXTLINE(bugprone-use-after-move)
        CHECK_FALSE(task);
        CHECK(moved);
        CHECK_EQ(moved.get_handle(), original_handle);

        ::verilator_utils::task<void> assigned{[] -> ::verilator_utils::task<void> { co_return; }()};
        original_handle = assigned.get_handle();
        const auto detached_handle{assigned.detach()};
        CHECK_FALSE(assigned);
        CHECK_EQ(detached_handle, original_handle);
        detached_handle.destroy();

        auto destroy_task{[] -> ::verilator_utils::task<void> { co_return; }()};
        destroy_task.destroy();
        CHECK_FALSE(destroy_task);
    }

    TEST_CASE("task accessors reject a task that was moved from")
    {
        auto source{[] -> ::verilator_utils::task<void> { co_return; }()};
        const auto owner{::std::move(source)};

        CHECK(owner.joinable());
        // 移动后源对象不再绑定协程，需要协程的访问器都触发断言而不是解引用空句柄
        // NOLINTBEGIN(bugprone-use-after-move)
        CHECK_FALSE(source.joinable());
        CHECK_THROWS_WITH_AS(static_cast<void>(source.done()),
                             ::doctest::Contains{"不能检查是否完成"},
                             ::verilator_utils::assertion_error);
        CHECK_THROWS_WITH_AS(source.resume(), ::doctest::Contains{"不能恢复执行"}, ::verilator_utils::assertion_error);
        // rethrow_exception转发给get_promise，因此报告的是承诺体的检查
        CHECK_THROWS_WITH_AS(source.rethrow_exception(),
                             ::doctest::Contains{"不能获取承诺体"},
                             ::verilator_utils::assertion_error);
        CHECK_THROWS_WITH_AS(static_cast<void>(source.get_promise()),
                             ::doctest::Contains{"不能获取承诺体"},
                             ::verilator_utils::assertion_error);
        CHECK_THROWS_WITH_AS(static_cast<void>(source.cancel_possible()),
                             ::doctest::Contains{"不能检查取消状态"},
                             ::verilator_utils::assertion_error);
        CHECK_THROWS_WITH_AS(static_cast<void>(source.cancel_requested()),
                             ::doctest::Contains{"不能检查取消状态"},
                             ::verilator_utils::assertion_error);
        CHECK_THROWS_WITH_AS(source.cancel(), ::doctest::Contains{"不能取消"}, ::verilator_utils::assertion_error);
        // NOLINTEND(bugprone-use-after-move)
    }

    TEST_CASE("task accessors reject a detached or destroyed task")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};

        auto detached{[] -> ::verilator_utils::task<void> { co_return; }()};
        const auto handle{detached.detach()};
        // 句柄所有权转移后任务不再绑定协程
        CHECK_FALSE(detached.joinable());
        CHECK_THROWS_WITH_AS(static_cast<void>(detached.cancel_requested()),
                             ::doctest::Contains{"不能检查取消状态"},
                             ::verilator_utils::assertion_error);
        CHECK_THROWS_WITH_AS(detached.resume(), ::doctest::Contains{"不能恢复执行"}, ::verilator_utils::assertion_error);
        handle.destroy();

        auto destroyed{[] -> ::verilator_utils::task<void> { co_return; }()};
        destroyed.destroy();
        CHECK_FALSE(destroyed.joinable());
        CHECK_THROWS_WITH_AS(static_cast<void>(destroyed.done()),
                             ::doctest::Contains{"不能检查是否完成"},
                             ::verilator_utils::assertion_error);
        CHECK_THROWS_WITH_AS(destroyed.rethrow_exception(),
                             ::doctest::Contains{"不能获取承诺体"},
                             ::verilator_utils::assertion_error);
        // 调度器入口同样依赖该检查拒绝空任务
        CHECK_THROWS_WITH_AS(scheduler.add_task(::std::move(destroyed)),
                             ::doctest::Contains{"不能获取承诺体"},
                             ::verilator_utils::assertion_error);
    }

    TEST_CASE("task records regular exceptions and ignores finish exceptions when rethrowing")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool caught_regular{};

        // 任务必须绑定到调度器：由父协程等待子任务，从而在子任务帧仍存活时检查承诺体状态
        auto failing_task{[] -> ::verilator_utils::task<void> {
            throw ::std::runtime_error{"regular failure"};
            co_return;
        }()};
        const auto failing_parent{
            [&](this auto) -> ::verilator_utils::task<void> {
                try
                {
                    co_await failing_task;
                }
                catch(const ::std::runtime_error& exception)
                {
                    caught_regular = ::std::string_view{exception.what()} == "regular failure"sv;
                }
            },
        };
        scheduler.add_task(failing_parent());
        scheduler.loop_until_finish();

        CHECK(caught_regular);
        CHECK(failing_task.done());
        CHECK(failing_task.get_promise().with_unhandled_exception());
        CHECK_THROWS_AS(failing_task.rethrow_exception(), ::std::runtime_error);

        // eval_finish_exception不会被记录为未处理异常，因此重新抛出时被忽略
        auto finish_task{[] -> ::verilator_utils::task<void> { co_await eval_finish(); }()};
        const auto finish_parent{[&](this auto) -> ::verilator_utils::task<void> { co_await finish_task; }};
        scheduler.add_task(finish_parent());
        scheduler.loop_until_finish();

        CHECK(finish_task.done());
        CHECK_FALSE(finish_task.get_promise().with_unhandled_exception());
        CHECK_NOTHROW(finish_task.rethrow_exception());
    }

    TEST_CASE("time waits advance the simulated time and format correctly")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};

        CHECK_EQ(scheduler.time_in_time_precision(), 0u);
        CHECK_EQ(scheduler.time_in_time_unit(), 0.0);
        CHECK_EQ(scheduler.time_in_string(), "0ns"sv);

        const auto&& task{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_time(5_ps);
                CHECK_EQ(scheduler.time_in_time_precision(), 5u);
                CHECK_EQ(scheduler.time_in_string(), "0.005ns"sv);
                co_await ::verilator_utils::wait_time(2_ns);
                CHECK_EQ(scheduler.time_in_time_precision(), 2'005u);
                CHECK_EQ(scheduler.time_in_string(), "2.005ns"sv);
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

        const auto&& task{[&] -> ::verilator_utils::task<void> {
            CHECK_EQ(scheduler.time_in_string(), "1ns"sv);
            co_await ::verilator_utils::wait_time(999_ns);
            CHECK_EQ(scheduler.time_in_string(), "1000ns"sv);
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
            test_case{-7,  "100ns"sv},
            test_case{-8,  "10ns"sv },
            test_case{-9,  "1ns"sv  },
            test_case{-10, "100ps"sv},
            test_case{-11, "10ps"sv },
            test_case{-12, "1ps"sv  },
        };

        for(auto&& [time_unit, expected]: test_cases)
        {
            CAPTURE(time_unit);
            scheduler_fixture fixture{time_unit, time_unit};
            const auto scheduler{fixture.make_scheduler()};
            fixture.context.time(1u);
            CHECK_EQ(scheduler.time_in_string(), expected);
        }
    }

    TEST_CASE("scheduler uses configured time unit for normalized time")
    {
        scheduler_fixture fixture{-6, -12};
        auto scheduler{fixture.make_scheduler()};

        const auto&& task{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_time(1'000_ns);
                CHECK_EQ(scheduler.time_in_time_precision(), 1'000'000u);
                CHECK_EQ(scheduler.time_in_time_unit(), ::doctest::Approx{1.0});
                CHECK_EQ(scheduler.time_in_string(), "1us"sv);
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
            [&] -> ::verilator_utils::task<void> {
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
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_time(3_ps);
                observed_times.push_back(scheduler.time_in_time_precision());
                completed_tasks.push_back(1);
            },
        };
        const auto task_b{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_time(1_ps);
                observed_times.push_back(scheduler.time_in_time_precision());
                completed_tasks.push_back(2);
            },
        };
        const auto task_c{
            [&] -> ::verilator_utils::task<void> {
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
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_event([&] {
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
            [&](int task_id) -> ::verilator_utils::task<void> {
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
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_event([] { return true; });
                resumed = true;
            },
        };

        scheduler.add_task(task());
        scheduler.loop_once();
        CHECK(resumed);
        CHECK(scheduler.empty());
    }

    TEST_CASE("stage waits observe scheduler phases")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool seen_before_eval{};
        bool seen_after_eval{};

        using enum ::verilator_utils::eval_scheduler::eval_stage_enum;
        const auto&& task{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_eval_stage(scheduler, before_dut_eval);
                seen_before_eval = true;
                co_await ::verilator_utils::wait_eval_stage(scheduler, after_dut_eval);
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

        struct observing_dut final : ::fake_dut
        {
            explicit observing_dut(::VerilatedContext& context,
                                   ::verilator_utils::eval_scheduler* scheduler,
                                   bool* seen_on_dut_eval) :
                ::fake_dut{context}, scheduler{scheduler}, seen_on_dut_eval{seen_on_dut_eval}
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

        using enum ::verilator_utils::eval_scheduler::eval_stage_enum;
        const auto&& task{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_eval_stage(scheduler, before_dut_eval);
                seen_before_eval = true;
                co_await ::verilator_utils::wait_eval_stage(scheduler, after_dut_eval);
                seen_after_eval = true;
            },
        };

        scheduler.add_task(task());
        scheduler.loop_once();
        CHECK(seen_before_eval);
        CHECK(seen_after_eval);
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

    TEST_CASE("root coroutine resumes after a synchronous child completes")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool resumed{};
        auto child{[](this auto) -> ::verilator_utils::task<void> { co_return; }()};
        const auto&& root{
            [&] -> ::verilator_utils::task<void> {
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

    TEST_CASE("finish cooperatively cancels waiting tasks")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool resumed{};

        const auto task{
            [&] -> ::verilator_utils::task<void> {
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
        // initial_suspend阶段就被取消
        CHECK_FALSE(resumed);
    }

    TEST_CASE("cleanup unfinished task when scheduler destroyed")
    {
        ::std::size_t destroyed{};
        const auto just_wait{
            [&] -> ::verilator_utils::task<void> {
                const frame_destruction_counter counter{&destroyed};
                co_await ::verilator_utils::wait_time(1_ns);
            },
        };
        {
            scheduler_fixture fixture{};
            auto scheduler{fixture.make_scheduler()};
            scheduler.add_task(just_wait());
            // 只执行就绪队列：根任务挂起在等待队列上，尚未完成
            scheduler.initial_eval();
        }
        // 调度器析构时应通过协作式取消回收未完成的根协程帧
        CHECK_EQ(destroyed, 1zu);
    }

    TEST_CASE("suspending on an event records suspended status and source location")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        signal_state signal{};
        ::std::uint32_t recorded_line{};

        const auto child_lambda{
            [&] -> ::verilator_utils::task<void> {
                [[maybe_unused]] const auto handle{
                    co_await ::verilator_utils::get_handle<::verilator_utils::task<void>::promise_type>()};
                recorded_line = ::std::source_location::current().line();
                co_await ::verilator_utils::wait_event([&] { return signal.value != 0; });
            },
        };
        auto child{child_lambda()};
        const auto parent{[&] -> ::verilator_utils::task<void> { co_await child; }};
        scheduler.add_task(parent());
        const auto& promise{child.get_promise()};

        CHECK_EQ(promise.status, ::verilator_utils::task<void>::status_enum::initial_suspend);
        CHECK(is_default_suspend_location(promise.suspend_location));

        scheduler.loop_once();
        CHECK_EQ(promise.status, ::verilator_utils::task<void>::status_enum::suspended);
        REQUIRE_NE(promise.suspend_location.line(), 0u);
        CHECK_EQ(promise.suspend_location.line(), recorded_line + 1);
        REQUIRE_NE(promise.suspend_location.file_name(), nullptr);
        CHECK(::std::string_view{promise.suspend_location.file_name()}.ends_with("scheduler.cpp"sv));

        signal.value = 1;
        scheduler.loop_once();
        CHECK(child.done());
        CHECK_EQ(promise.status, ::verilator_utils::task<void>::status_enum::finished);
        CHECK(is_default_suspend_location(promise.suspend_location));
    }

    TEST_CASE("resuming a wait clears the suspend location and restores running status")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::std::optional<::std::source_location> observed_location;
        ::verilator_utils::task<void>::status_enum observed_status{};

        const auto child_lambda{
            [&] -> ::verilator_utils::task<void> {
                const auto handle{co_await ::verilator_utils::get_handle<::verilator_utils::task<void>::promise_type>()};
                co_await ::verilator_utils::wait_time(1_ps);
                observed_status = handle.promise().status;
                observed_location = handle.promise().suspend_location;
            },
        };
        auto child{child_lambda()};
        const auto parent{[&] -> ::verilator_utils::task<void> { co_await child; }};
        scheduler.add_task(parent());

        scheduler.loop_until_finish();
        REQUIRE(observed_location.has_value());
        CHECK_EQ(observed_status, ::verilator_utils::task<void>::status_enum::running);
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        CHECK(is_default_suspend_location(*observed_location));
        CHECK(child.done());
    }

    TEST_CASE("non-suspending awaiters keep status running and suspend location empty")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};

        const auto child_lambda{
            [&] -> ::verilator_utils::task<void> {
                const auto handle{co_await ::verilator_utils::get_handle<::verilator_utils::task<void>::promise_type>()};
                const auto& promise{handle.promise()};
                CHECK_EQ(promise.status, ::verilator_utils::task<void>::status_enum::running);
                CHECK(is_default_suspend_location(promise.suspend_location));

                co_await ::std::suspend_never{};
                CHECK_EQ(promise.status, ::verilator_utils::task<void>::status_enum::running);
                CHECK(is_default_suspend_location(promise.suspend_location));

                co_await ::verilator_utils::wait_event([] { return true; });
                CHECK_EQ(promise.status, ::verilator_utils::task<void>::status_enum::running);
                CHECK(is_default_suspend_location(promise.suspend_location));
            },
        };
        auto child{child_lambda()};
        const auto parent{[&] -> ::verilator_utils::task<void> { co_await child; }};
        scheduler.add_task(parent());

        scheduler.loop_until_finish();
        CHECK(child.done());
    }

    TEST_CASE("awaiting a child task records the parent suspend point")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        signal_state signal{};

        const auto child_lambda{
            [&] -> ::verilator_utils::task<void> {
                [[maybe_unused]] const auto handle{
                    co_await ::verilator_utils::get_handle<::verilator_utils::task<void>::promise_type>()};
                co_await ::verilator_utils::wait_event([&] { return signal.value != 0; });
            },
        };
        auto child{child_lambda()};
        ::std::uint32_t recorded_line{};
        const auto parent{
            [&] -> ::verilator_utils::task<void> {
                recorded_line = ::std::source_location::current().line();
                co_await child;
            },
        };
        auto parent_task{parent()};
        const auto& promise{parent_task.get_promise()};
        scheduler.add_task(::std::move(parent_task));

        scheduler.loop_once();
        CHECK_EQ(promise.status, ::verilator_utils::task<void>::status_enum::suspended);
        REQUIRE_NE(promise.suspend_location.line(), 0u);
        CHECK_EQ(promise.suspend_location.line(), recorded_line + 1);

        signal.value = 1;
        scheduler.loop_once();
        CHECK(child.done());
    }

    TEST_CASE("bool-suspending awaiters inject the location only when they suspend")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::std::optional<::verilator_utils::task<void>::status_enum> status_after_false_resume;
        ::std::optional<::std::source_location> location_after_false_resume;
        bool resumed_after_true{};
        ::std::uint32_t recorded_line{};

        const auto task_lambda{
            [&] -> ::verilator_utils::task<void> {
                const auto handle{co_await ::verilator_utils::get_handle<::verilator_utils::task<void>::promise_type>()};
                auto& promise{handle.promise()};
                co_await bool_suspend_awaiter{false};
                status_after_false_resume = promise.status;
                location_after_false_resume = promise.suspend_location;
                recorded_line = ::std::source_location::current().line();
                co_await bool_suspend_awaiter{true};
                resumed_after_true = true;
            },
        };
        auto task{task_lambda()};
        auto& promise{task.get_promise()};

        // 任务必须绑定到调度器：手动驱动任务前先完成绑定（与add_task的绑定方式一致）
        promise.scheduler = ::std::addressof(scheduler);
        task.resume();
        CHECK_FALSE(resumed_after_true);
        REQUIRE(status_after_false_resume.has_value());
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        CHECK_EQ(*status_after_false_resume, ::verilator_utils::task<void>::status_enum::running);
        REQUIRE(location_after_false_resume.has_value());
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        CHECK(is_default_suspend_location(*location_after_false_resume));
        CHECK_EQ(promise.status, ::verilator_utils::task<void>::status_enum::suspended);
        REQUIRE_NE(promise.suspend_location.line(), 0u);
        CHECK_EQ(promise.suspend_location.line(), recorded_line + 1);
    }

    // --- 事件挂起跟踪与协程帧回收 (suspend_queue) ---

    TEST_CASE("scheduler reclaims a root coroutine suspended on a never-fired event")
    {
        ::std::size_t destroyed{};
        {
            scheduler_fixture fixture{};
            auto scheduler{fixture.make_scheduler()};
            ::verilator_utils::event event{};
            const auto waiter_lambda{[&] -> ::verilator_utils::task<void> {
                const frame_destruction_counter counter{&destroyed};
                co_await event;
            }};
            scheduler.add_task(waiter_lambda());
            // 事件永远不会被通知，协程挂起在调度器外；仅剩挂起协程时循环应正常终止
            scheduler.loop_until_finish();
            CHECK(scheduler.empty());
            scheduler.finish();
        }
        // 调度器析构时应通过挂起队列回收该协程帧，否则帧随事件一起泄漏
        CHECK_EQ(destroyed, 1zu);
    }

    TEST_CASE("scheduler reclaims an event-suspended chain without double-destroying external subtasks")
    {
        ::std::size_t destroyed{};
        // 外部持有的子协程由task对象管理生命周期，而调度器在析构时才会通过协作式取消
        // 释放挂在event上的挂起协程，因此该所有者必须先于子协程、后于调度器析构
        ::std::unique_ptr<::verilator_utils::task<void>> child{};
        {
            scheduler_fixture fixture{};
            auto scheduler{fixture.make_scheduler()};
            // 协程设施的生命周期必须短于调度器
            ::verilator_utils::event event{};
            const auto child_lambda{[&] -> ::verilator_utils::task<void> {
                const frame_destruction_counter counter{&destroyed};
                co_await event;
            }};
            child = ::std::make_unique<::verilator_utils::task<void>>(child_lambda());
            const auto parent{[&](this auto) -> ::verilator_utils::task<void> {
                const frame_destruction_counter counter{&destroyed};
                co_await *child;
            }};
            scheduler.add_task(parent());
            scheduler.initial_eval();

            // 子协程已被父协程等待并挂起在event上，尚未完成
            CHECK_FALSE(child->done());
            CHECK_EQ(destroyed, 0zu);
            scheduler.finish();
        }
        // 调度器析构时取消整条链：根协程被调度器销毁，外部子协程退出但帧仍由所有者持有
        CHECK(child->done());
        // 外部持有的子协程由task对象负责销毁：调度器不得接管，也不得重复销毁
        child->destroy();
        CHECK_EQ(destroyed, 2zu);
    }

    TEST_CASE("scheduler reclaims coroutines blocked in mailbox events")
    {
        ::std::size_t destroyed{};
        {
            scheduler_fixture fixture{};
            auto scheduler{fixture.make_scheduler()};
            ::verilator_utils::mailbox<int> mailbox{};
            const auto waiter_lambda{[&] -> ::verilator_utils::task<void> {
                const frame_destruction_counter counter{&destroyed};
                [[maybe_unused]] const auto value{co_await mailbox.get()};
            }};
            auto task{waiter_lambda()};
            scheduler.add_task(::std::move(task));
            scheduler.initial_eval();
            scheduler.finish();
        }
        CHECK_EQ(destroyed, 1zu);
    }

    TEST_CASE("notify_all removes every waiter from suspend tracking")
    {
        ::std::size_t destroyed{};
        ::std::size_t wake_count{};
        {
            scheduler_fixture fixture{};
            auto scheduler{fixture.make_scheduler()};
            ::verilator_utils::event event{};

            const auto make_waiter{[&] -> ::verilator_utils::task<void> {
                const frame_destruction_counter counter{&destroyed};
                co_await event;
                ++wake_count;
            }};
            scheduler.add_task(make_waiter());
            scheduler.add_task(make_waiter());
            scheduler.loop_once();
            CHECK_EQ(wake_count, 0u);

            event.notify_all();
            scheduler.loop_once();
            CHECK_EQ(wake_count, 2zu);
            scheduler.finish();
        }
        // 通知时挂起条目被清除，协程帧在完成时回收，析构调度器时不会二次销毁
        CHECK_EQ(destroyed, 2zu);
    }

    TEST_CASE("notify_one removes only the notified waiter from suspend tracking")
    {
        ::std::size_t destroyed{};
        ::std::vector<int> wake_order;
        {
            scheduler_fixture fixture{};
            auto scheduler{fixture.make_scheduler()};
            ::verilator_utils::event event{};

            const auto make_waiter{[&](this auto, int id) -> ::verilator_utils::task<void> {
                const frame_destruction_counter counter{&destroyed};
                co_await event;
                wake_order.push_back(id);
            }};
            scheduler.add_task(make_waiter(1));
            scheduler.add_task(make_waiter(2));
            scheduler.loop_once();
            CHECK(wake_order.empty());

            event.notify_one();
            scheduler.loop_once();
            CHECK_EQ(wake_order, ::std::vector<int>{1});
            scheduler.finish();
        }
        // 第二个等待者仍由挂起队列跟踪，留待析构调度器时回收（恰好一次）
        CHECK_EQ(destroyed, 2zu);
    }

    TEST_CASE("repeated event suspension and notification keeps suspend tracking balanced")
    {
        ::std::size_t destroyed{};
        {
            scheduler_fixture fixture{};
            auto scheduler{fixture.make_scheduler()};
            ::verilator_utils::event event{};
            ::std::size_t wake_count{};

            const auto waiter{[&](this auto) -> ::verilator_utils::task<void> {
                const frame_destruction_counter counter{&destroyed};
                for(::std::size_t i{}; i != 3; ++i)
                {
                    co_await event;
                    ++wake_count;
                }
            }};
            scheduler.add_task(waiter());
            scheduler.loop_once();
            CHECK_EQ(wake_count, 0u);

            for(::std::size_t i{1}; i != 4; ++i)
            {
                event.notify_all();
                scheduler.loop_once();
                CHECK_EQ(wake_count, i);
            }
            scheduler.finish();
        }
        // 3次挂起对应3次通知，挂起条目均被清除，析构调度器时不会二次销毁
        CHECK_EQ(destroyed, 1zu);
    }

    TEST_CASE("suspend tracking entry kept after partial removal is reclaimed at destruction")
    {
        ::std::size_t destroyed{};
        {
            scheduler_fixture fixture{};
            auto scheduler{fixture.make_scheduler()};
            ::verilator_utils::event event{};
            const auto waiter_lambda{[&] -> ::verilator_utils::task<void> {
                const frame_destruction_counter counter{&destroyed};
                co_await event;
            }};
            auto task{waiter_lambda()};
            scheduler.add_task(::std::move(task));
            scheduler.initial_eval();
            scheduler.finish();
        }
        // 条目保留 → 析构调度器时帧被回收恰好一次
        CHECK_EQ(destroyed, 1zu);
    }

    TEST_CASE("suspending on an event requires a scheduler-bound task")
    {
        ::verilator_utils::event event{};
        const auto waiter_lambda{[&] -> ::verilator_utils::task<void> { co_await event; }};
        const auto task{waiter_lambda()};
        const auto handle{task.get_handle()};

        // 任务未绑定调度器时挂起失败：可等待体在挂起前检查调度器并抛出断言异常
        auto awaiter{task.get_promise().await_transform(event)};
        CHECK_THROWS_AS(awaiter.await_suspend(handle), ::verilator_utils::assertion_error);
    }

    TEST_CASE("event, mailbox and semaphore are immobile synchronization primitives")
    {
        static_assert(!::std::is_copy_constructible_v<::verilator_utils::event>);
        static_assert(!::std::is_copy_assignable_v<::verilator_utils::event>);
        static_assert(!::std::is_move_constructible_v<::verilator_utils::event>);
        static_assert(!::std::is_move_assignable_v<::verilator_utils::event>);
        static_assert(!::std::is_copy_constructible_v<::verilator_utils::semaphore>);
        static_assert(!::std::is_copy_assignable_v<::verilator_utils::semaphore>);
        static_assert(!::std::is_move_constructible_v<::verilator_utils::semaphore>);
        static_assert(!::std::is_move_assignable_v<::verilator_utils::semaphore>);
        static_assert(!::std::is_copy_constructible_v<::verilator_utils::mailbox<int>>);
        static_assert(!::std::is_copy_assignable_v<::verilator_utils::mailbox<int>>);
        static_assert(!::std::is_move_constructible_v<::verilator_utils::mailbox<int>>);
        static_assert(!::std::is_move_assignable_v<::verilator_utils::mailbox<int>>);
        // 事件不可复制/移动，保证挂起时持有的等待队列引用不会悬垂
    }

    // --- 任务取消 (cancel) ---

    TEST_CASE("cancellation request is accepted before the task starts running")
    {
        const auto never_started{[] -> ::verilator_utils::task<void> { co_return; }()};

        // 尚未执行的任务处于initial_suspend，属于可取消状态
        CHECK(never_started.cancel_possible());
        CHECK_FALSE(never_started.cancel_requested());

        never_started.cancel();
        CHECK(never_started.cancel_requested());
        CHECK_EQ(never_started.get_promise().status, ::verilator_utils::task<void>::status_enum::cancel_requested);
        // 已收到取消请求的任务不在可取消状态，重复请求触发断言
        CHECK_FALSE(never_started.cancel_possible());
        CHECK_THROWS_WITH_AS(never_started.cancel(), ::doctest::Contains{"不可取消"}, ::verilator_utils::assertion_error);
    }

    TEST_CASE("cancellation request is rejected while the task runs or after it finished")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool running_rejected{};

        const auto running_lambda{
            [&] -> ::verilator_utils::task<void> {
                const auto handle{co_await ::verilator_utils::get_handle<::verilator_utils::task<void>::promise_type>()};
                CHECK_EQ(handle.promise().status, ::verilator_utils::task<void>::status_enum::running);
                CHECK_FALSE(handle.promise().cancel_possible());
                CHECK_THROWS_WITH_AS(handle.promise().cancel(),
                                     ::doctest::Contains{"不可取消"},
                                     ::verilator_utils::assertion_error);
                running_rejected = true;
            },
        };
        scheduler.add_task(running_lambda());
        CHECK_NOTHROW(scheduler.loop_until_finish());
        CHECK(running_rejected);

        // 已执行完的任务同样不可取消
        auto finished_task{[] -> ::verilator_utils::task<void> { co_await ::verilator_utils::wait_time(1_ps); }()};
        const auto finished_parent{[&] -> ::verilator_utils::task<void> { co_await finished_task; }};
        scheduler.add_task(finished_parent());
        scheduler.loop_until_finish();

        REQUIRE(finished_task.done());
        CHECK_FALSE(finished_task.cancel_possible());
        CHECK_FALSE(finished_task.cancel_requested());
        CHECK_THROWS_WITH_AS(finished_task.cancel(), ::doctest::Contains{"不可取消"}, ::verilator_utils::assertion_error);
    }

    TEST_CASE("canceling a suspended subtask reports cancellation to its parent")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        signal_state signal{};
        bool resumed_after_wait{};
        bool parent_caught{};
        bool parent_finished{};

        // 子任务由外部持有，从而可以在其挂起期间发起取消请求
        const auto child_lambda{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_event([&] { return signal.value != 0; });
                resumed_after_wait = true;
            },
        };
        auto child_task{child_lambda()};
        const auto parent_lambda{
            [&] -> ::verilator_utils::task<void> {
                try
                {
                    co_await child_task;
                }
                catch(const ::verilator_utils::subtask_cancel_exception&)
                {
                    parent_caught = true;
                }
                parent_finished = true;
            },
        };

        scheduler.add_task(parent_lambda());
        scheduler.loop_once();
        // 父任务等待子任务，子任务挂起在事件上：此时子任务处于可取消状态
        REQUIRE_FALSE(child_task.done());
        CHECK_EQ(child_task.get_promise().status, ::verilator_utils::task<void>::status_enum::suspended);
        CHECK(child_task.cancel_possible());

        child_task.cancel();
        CHECK(child_task.cancel_requested());
        CHECK_EQ(child_task.get_promise().status, ::verilator_utils::task<void>::status_enum::cancel_requested);
        CHECK_FALSE(child_task.cancel_possible());

        signal.value = 1;
        scheduler.loop_until_finish();

        // 取消请求在恢复点生效：子任务不再继续执行，父任务收到子任务取消异常而不是子任务的结果
        CHECK_FALSE(resumed_after_wait);
        CHECK(parent_caught);
        CHECK(parent_finished);
        REQUIRE(child_task.done());
        CHECK_EQ(child_task.get_promise().status, ::verilator_utils::task<void>::status_enum::canceled);
        CHECK(child_task.get_promise().is_coroutine_exited());
        CHECK_FALSE(child_task.get_promise().is_coroutine_finished());
        CHECK_FALSE(child_task.get_promise().cancel_requested());
        // 取消不是异常退出：协程中没有未处理的异常，重新抛出时也不产生异常
        CHECK_FALSE(child_task.get_promise().with_unhandled_exception());
        CHECK_NOTHROW(child_task.rethrow_exception());
        // 被取消的子任务没有结果可消费，父任务只能通过subtask_cancel_exception感知取消
        CHECK_THROWS_WITH_AS(static_cast<void>(child_task.get_promise().get_result()),
                             ::doctest::Contains{"不能获取结果"},
                             ::verilator_utils::assertion_error);
    }

    TEST_CASE("canceling a subtask before it starts skips its coroutine body")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool body_ran{};
        bool parent_caught{};
        bool parent_finished{};

        const auto child_lambda{
            [&] -> ::verilator_utils::task<void> {
                body_ran = true;
                co_return;
            },
        };
        auto child_task{child_lambda()};
        CHECK(child_task.cancel_possible());
        child_task.cancel();

        const auto parent_lambda{
            [&] -> ::verilator_utils::task<void> {
                try
                {
                    co_await child_task;
                }
                catch(const ::verilator_utils::subtask_cancel_exception&)
                {
                    parent_caught = true;
                }
                parent_finished = true;
            },
        };
        scheduler.add_task(parent_lambda());
        CHECK_NOTHROW(scheduler.loop_until_finish());

        // 从未开始执行的协程体一次都不会运行
        CHECK_FALSE(body_ran);
        CHECK(parent_caught);
        CHECK(parent_finished);
        REQUIRE(child_task.done());
        CHECK_EQ(child_task.get_promise().status, ::verilator_utils::task<void>::status_enum::canceled);
    }

    TEST_CASE("canceling a root task waiting on time reclaims its frame")
    {
        ::std::size_t destroyed{};
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool resumed_after_wait{};

        const auto waiter_lambda{
            [&] -> ::verilator_utils::task<void> {
                const frame_destruction_counter counter{&destroyed};
                co_await ::verilator_utils::wait_time(1_ns);
                resumed_after_wait = true;
            },
        };
        auto root{waiter_lambda()};
        const auto handle{root.get_handle()};
        scheduler.add_task(::std::move(root));
        // 只执行就绪队列，让根任务挂起在等待队列上而不推进仿真时间
        scheduler.initial_eval();
        REQUIRE_FALSE(handle.done());
        CHECK_EQ(handle.promise().status, ::verilator_utils::task<void>::status_enum::suspended);
        CHECK(handle.promise().cancel_possible());

        handle.promise().cancel();
        CHECK(handle.promise().cancel_requested());

        CHECK_NOTHROW(scheduler.loop_until_finish());
        CHECK_FALSE(resumed_after_wait);
        // 等待时间到达后任务被恢复，取消请求在恢复点生效；根任务由调度器回收协程帧
        CHECK_EQ(destroyed, 1zu);
        // 取消是正常退出，不标记仿真错误
        CHECK_FALSE(scheduler.is_error());
    }

    TEST_CASE("uncaught subtask cancellation propagates out of the scheduler")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        signal_state signal{};
        bool resumed_after_wait{};

        const auto child_lambda{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_event([&] { return signal.value != 0; });
                resumed_after_wait = true;
            },
        };
        auto child_task{child_lambda()};
        // 父任务不捕获子任务取消异常，异常应沿调用链向上传播
        const auto parent_lambda{[&] -> ::verilator_utils::task<void> { co_await child_task; }};

        scheduler.add_task(parent_lambda());
        scheduler.loop_once();
        REQUIRE(child_task.cancel_possible());

        child_task.cancel();
        signal.value = 1;
        CHECK_THROWS_WITH_AS(scheduler.loop_until_finish(),
                             ::doctest::Contains{"子任务取消"},
                             ::verilator_utils::subtask_cancel_exception);
        CHECK_FALSE(resumed_after_wait);
        REQUIRE(child_task.done());
        CHECK_EQ(child_task.get_promise().status, ::verilator_utils::task<void>::status_enum::canceled);
    }

    TEST_CASE("cancellation exceptions are distinct from the simulation finish exception")
    {
        static_assert(::std::derived_from<::verilator_utils::task_cancel_exception, ::std::runtime_error>);
        static_assert(::std::derived_from<::verilator_utils::subtask_cancel_exception, ::std::runtime_error>);
        // 两种取消异常互不派生：父任务捕获的subtask_cancel_exception不会吞掉自身的取消
        static_assert(
            !::std::derived_from<::verilator_utils::subtask_cancel_exception, ::verilator_utils::task_cancel_exception>);
        static_assert(
            !::std::derived_from<::verilator_utils::task_cancel_exception, ::verilator_utils::subtask_cancel_exception>);
        // 取消异常与仿真结束异常互不派生
        static_assert(!::std::derived_from<::verilator_utils::task_cancel_exception, ::verilator_utils::eval_finish_exception>);
        static_assert(
            !::std::derived_from<::verilator_utils::subtask_cancel_exception, ::verilator_utils::eval_finish_exception>);

        CHECK_EQ(::std::string_view{::verilator_utils::task_cancel_exception{}.what()}, "任务取消"sv);
        CHECK_EQ(::std::string_view{::verilator_utils::subtask_cancel_exception{}.what()}, "子任务取消"sv);
    }

    TEST_CASE("coroutine_pair compares by coroutine identity")
    {
        const auto first_lambda{[] -> ::verilator_utils::task<void> { co_return; }};
        const auto second_lambda{[] -> ::verilator_utils::task<void> { co_return; }};
        const auto first{first_lambda()};
        const auto second{second_lambda()};
        const ::verilator_utils::detail::coroutine_pair first_pair{first.get_handle()};
        ::verilator_utils::detail::coroutine_pair first_pair_duplicate{first.get_handle()};
        ::verilator_utils::detail::coroutine_pair second_pair{second.get_handle()};

        // 同一协程柄构造的状态对相等，不同协程柄不相等
        CHECK(first_pair == first_pair_duplicate);
        CHECK(first_pair != second_pair);
        CHECK_EQ(first_pair <=> first_pair_duplicate, ::std::strong_ordering::equal);
        // 全序关系与协程柄地址序一致
        const auto expected_order{first.get_handle().address() < second.get_handle().address()};
        CHECK_EQ(first_pair < second_pair, expected_order);
    }
}
