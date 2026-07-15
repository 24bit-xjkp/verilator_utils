module;
#include <doctest_fwd.hpp>
module unit_test;

extern "C++"
{
#include <doctest.h>
}

namespace
{
    struct fake_dut final : ::VerilatedModel
    {
        explicit fake_dut(::VerilatedContext& context) : ::VerilatedModel{context} {}

        void eval() {}

        const char* hierName() const override final { return "fake_dut"; }

        const char* modelName() const override final { return "fake_dut"; }

        unsigned threads() const override final { return 1; }

        void prepareClone() const { contextp()->prepareClone(); }

        void atClone() const { contextp()->threadPoolpOnClone(); }
    };

    struct scheduler_fixture
    {
        ::VerilatedContext context{};
        fake_dut dut{context};

        scheduler_fixture(::std::int32_t time_unit = -9, ::std::int32_t time_precision = -12)
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
        auto child{[] -> ::verilator_utils::task { co_return; }()};

        ::verilator_utils::task::handle_t parent_handle;
        auto parent{
            [](::verilator_utils::task& child, ::verilator_utils::task::handle_t& parent_handle) -> ::verilator_utils::task
            {
                auto handle{co_await ::verilator_utils::get_handle()};
                CHECK(handle);
                parent_handle = handle;
                CHECK_EQ(handle.promise().status, ::verilator_utils::task::status_enum::running);
                co_await child;
            }(child, parent_handle)};
        auto expected_parent_handle{parent.get_handle()};

        CHECK(parent);
        CHECK(child);
        CHECK_EQ(parent.get_promise().status, ::verilator_utils::task::status_enum::initial_suspend);
        CHECK_EQ(child.get_promise().status, ::verilator_utils::task::status_enum::initial_suspend);
        ::verilator_utils::async_task runner{scheduler, ::std::move(parent)};
        scheduler.loop_until_finish();
        CHECK_EQ(parent_handle, expected_parent_handle);
        CHECK(runner.done());
        CHECK(child.done());
        runner.get_promise().rethrow_exception();
    }

    TEST_CASE("task supports move construction assignment detach and destroy")
    {
        auto task{[] -> ::verilator_utils::task { co_return; }()};
        auto original_handle{task.get_handle()};

        ::verilator_utils::task moved{::std::move(task)};
        CHECK_FALSE(task);
        CHECK(moved);
        CHECK_EQ(moved.get_handle(), original_handle);

        ::verilator_utils::task assigned{[] -> ::verilator_utils::task { co_return; }()};
        original_handle = assigned.get_handle();
        auto detached_handle{assigned.detach()};
        CHECK_FALSE(assigned);
        CHECK_EQ(detached_handle, original_handle);
        detached_handle.destroy();

        auto destroy_task{[] -> ::verilator_utils::task { co_return; }()};
        destroy_task.destroy();
        CHECK_FALSE(destroy_task);
    }

    TEST_CASE("task records regular exceptions and ignores finish exceptions when rethrowing")
    {
        auto failing_task{[] -> ::verilator_utils::task
                          {
                              throw ::std::runtime_error{"regular failure"};
                              co_return;
                          }()};
        failing_task.resume();
        CHECK(failing_task.done());
        CHECK(failing_task.get_promise().with_unhandled_exception());
        CHECK_THROWS_AS(failing_task.rethrow_exception(), ::std::runtime_error);

        auto finish_task{[] -> ::verilator_utils::task
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

        signal.value = true;
        CHECK(rising_detector());
        CHECK_FALSE(rising_detector());
        CHECK_FALSE(falling_detector());
        CHECK(both_detector());

        signal.value = false;
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

        signal.value = true;
        CHECK_FALSE(detector());
        signal.value = false;
        CHECK(detector());
    }

    TEST_CASE("time waits advance the simulated time and format correctly")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};

        CHECK_EQ(scheduler.time_in_time_precision(), 0u);
        CHECK_EQ(scheduler.time_in_time_unit(), 0.0);
        CHECK_EQ(scheduler.time_in_string(), "0ps");

        auto task{[](::verilator_utils::eval_scheduler& scheduler) -> ::verilator_utils::task
                  {
                      co_await ::verilator_utils::wait_time(5_ps);
                      CHECK_EQ(scheduler.time_in_time_precision(), 5u);
                      CHECK_EQ(scheduler.time_in_string(), "5ps");
                      co_await ::verilator_utils::wait_time(2_ns);
                      CHECK_EQ(scheduler.time_in_time_precision(), 2'005u);
                      CHECK_EQ(scheduler.time_in_string(), "2.005ns");
                  }(scheduler)};

        ::verilator_utils::async_task runner{scheduler, ::std::move(task)};
        scheduler.loop_until_finish();
        CHECK(runner.done());
        runner.get_promise().rethrow_exception();
    }

    TEST_CASE("time formatting selects larger units and keeps compact precision")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};

        auto task{[](::verilator_utils::eval_scheduler& scheduler) -> ::verilator_utils::task
                  {
                      co_await ::verilator_utils::wait_time(999_ps);
                      CHECK_EQ(scheduler.time_in_string(), "999ps");
                      co_await ::verilator_utils::wait_time(1_ps);
                      CHECK_EQ(scheduler.time_in_string(), "1ns");
                      co_await ::verilator_utils::wait_time(999_ns);
                      CHECK_EQ(scheduler.time_in_string(), "1us");
                      co_await ::verilator_utils::wait_time(999'000_ns);
                      CHECK_EQ(scheduler.time_in_string(), "1ms");
                  }(scheduler)};

        ::verilator_utils::async_task runner{scheduler, ::std::move(task)};
        scheduler.loop_until_finish();
        CHECK(runner.done());
        runner.get_promise().rethrow_exception();
    }

    TEST_CASE("scheduler uses configured time unit for normalized time")
    {
        scheduler_fixture fixture{-6, -12};
        auto scheduler{fixture.make_scheduler()};

        auto task{[](::verilator_utils::eval_scheduler& scheduler) -> ::verilator_utils::task
                  {
                      co_await ::verilator_utils::wait_time(1'000_ns);
                      CHECK_EQ(scheduler.time_in_time_precision(), 1'000'000u);
                      CHECK_EQ(scheduler.time_in_time_unit(), doctest::Approx{1.0});
                      CHECK_EQ(scheduler.time_in_string(), "1us");
                  }(scheduler)};

        ::verilator_utils::async_task runner{scheduler, ::std::move(task)};
        scheduler.loop_until_finish();
        CHECK(runner.done());
        runner.get_promise().rethrow_exception();
    }

    TEST_CASE("femtosecond waits accept aligned values")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};

        auto task{[](::verilator_utils::eval_scheduler& scheduler) -> ::verilator_utils::task
                  {
                      co_await ::verilator_utils::wait_time(2_ps);
                      CHECK_EQ(scheduler.time_in_time_precision(), 2u);
                  }(scheduler)};

        ::verilator_utils::async_task runner{scheduler, ::std::move(task)};
        scheduler.loop_until_finish();
        CHECK(runner.done());
    }

    TEST_CASE("wait queue resumes tasks by target time and groups equal deadlines")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::std::vector<::std::uint64_t> observed_times;
        ::std::vector<int> completed_tasks;

        auto task_a{[](::verilator_utils::eval_scheduler& scheduler,
                       ::std::vector<::std::uint64_t>& observed_times,
                       ::std::vector<int>& completed_tasks) -> ::verilator_utils::task
                    {
                        co_await ::verilator_utils::wait_time(3_ps);
                        observed_times.push_back(scheduler.time_in_time_precision());
                        completed_tasks.push_back(1);
                    }(scheduler, observed_times, completed_tasks)};
        auto task_b{[](::verilator_utils::eval_scheduler& scheduler,
                       ::std::vector<::std::uint64_t>& observed_times,
                       ::std::vector<int>& completed_tasks) -> ::verilator_utils::task
                    {
                        co_await ::verilator_utils::wait_time(1_ps);
                        observed_times.push_back(scheduler.time_in_time_precision());
                        completed_tasks.push_back(2);
                    }(scheduler, observed_times, completed_tasks)};
        auto task_c{[](::verilator_utils::eval_scheduler& scheduler,
                       ::std::vector<::std::uint64_t>& observed_times,
                       ::std::vector<int>& completed_tasks) -> ::verilator_utils::task
                    {
                        co_await ::verilator_utils::wait_time(3_ps);
                        observed_times.push_back(scheduler.time_in_time_precision());
                        completed_tasks.push_back(3);
                    }(scheduler, observed_times, completed_tasks)};

        ::std::vector<::verilator_utils::async_task> tasks;
        tasks.reserve(3);
        tasks.emplace_back(scheduler, ::std::move(task_a));
        tasks.emplace_back(scheduler, ::std::move(task_b));
        tasks.emplace_back(scheduler, ::std::move(task_c));

        scheduler.loop_once();
        CHECK_EQ(observed_times, ::std::vector<::std::uint64_t>{1u});
        CHECK_EQ(completed_tasks, ::std::vector<int>{2});
        scheduler.loop_once();
        CHECK_EQ(observed_times, (::std::vector<::std::uint64_t>{1u, 3u, 3u}));
        CHECK_EQ(completed_tasks.size(), 3u);
        CHECK(::std::ranges::contains(completed_tasks, 1));
        CHECK(::std::ranges::contains(completed_tasks, 3));
        CHECK(::std::ranges::all_of(tasks, [](::verilator_utils::async_task& task) { return task.done(); }));
    }

    TEST_CASE("event waits wake when callback becomes ready")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        signal_state signal{};
        bool observed_ready{};

        auto task{[](::verilator_utils::eval_scheduler& scheduler,
                     signal_state& signal,
                     bool& observed_ready) -> ::verilator_utils::task
                  {
                      co_await ::verilator_utils::wait_event(
                          [&]
                          {
                              observed_ready = signal.value != 0;
                              return signal.value != 0;
                          });
                      CHECK(observed_ready);
                      CHECK(signal.value != 0);
                  }(scheduler, signal, observed_ready)};

        ::verilator_utils::async_task runner{scheduler, ::std::move(task)};
        scheduler.loop_once();
        CHECK_FALSE(runner.done());
        signal.value = 1;
        scheduler.loop_once();
        CHECK(runner.done());
        runner.get_promise().rethrow_exception();
    }

    TEST_CASE("event queue wakes multiple ready tasks in one evaluation")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool event_ready{};
        ::std::vector<int> resumed_tasks;

        auto make_task{[](::verilator_utils::eval_scheduler& scheduler,
                          bool& event_ready,
                          ::std::vector<int>& resumed_tasks,
                          int task_id) -> ::verilator_utils::task
                       {
                           co_await ::verilator_utils::wait_event([&event_ready] { return event_ready; });
                           resumed_tasks.push_back(task_id);
                       }};

        auto task_a{make_task(scheduler, event_ready, resumed_tasks, 1)};
        auto task_b{make_task(scheduler, event_ready, resumed_tasks, 2)};
        ::verilator_utils::async_task runner_a{scheduler, ::std::move(task_a)};
        ::verilator_utils::async_task runner_b{scheduler, ::std::move(task_b)};

        scheduler.loop_once();
        CHECK(resumed_tasks.empty());
        event_ready = true;
        scheduler.loop_once();

        CHECK_EQ(resumed_tasks.size(), 2u);
        CHECK(::std::ranges::contains(resumed_tasks, 1));
        CHECK(::std::ranges::contains(resumed_tasks, 2));
        CHECK(runner_a.done());
        CHECK(runner_b.done());
        CHECK(scheduler.empty());
        runner_a.get_promise().rethrow_exception();
        runner_b.get_promise().rethrow_exception();
    }

    TEST_CASE("event waits that are immediately ready do not enter the scheduler queue")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool resumed{};

        auto task{[](::verilator_utils::eval_scheduler& scheduler, bool& resumed) -> ::verilator_utils::task
                  {
                      co_await ::verilator_utils::wait_event([] { return true; });
                      resumed = true;
                  }(scheduler, resumed)};

        ::verilator_utils::async_task runner{scheduler, ::std::move(task)};
        scheduler.loop_once();
        CHECK(resumed);
        CHECK(runner.done());
        CHECK(scheduler.empty());
        runner.get_promise().rethrow_exception();
    }

    TEST_CASE("wait_stimulus waits for the requested falling edges")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::CData clk{};
        ::std::uint64_t resumed_times;

        auto clock_task{::verilator_utils::generate_clock(::verilator_utils::bit_slice<::CData>{clk}, 4_ps)};
        auto stimulus_task{[](::verilator_utils::eval_scheduler& scheduler,
                              ::verilator_utils::bit_slice<::CData> clk,
                              ::std::uint64_t& resumed_times) -> ::verilator_utils::task
                           {
                               co_await ::verilator_utils::wait_stimulus(clk, 2);
                               resumed_times = scheduler.time_in_time_precision();
                               co_await ::verilator_utils::eval_finish();
                           }(scheduler, ::verilator_utils::bit_slice<::CData>{clk}, resumed_times)};

        ::verilator_utils::async_task clock_runner{scheduler, ::std::move(clock_task)};
        ::verilator_utils::async_task stimulus_runner{scheduler, ::std::move(stimulus_task)};

        scheduler.loop_until_finish();

        CHECK(stimulus_runner.done());
        // 一个时钟周期4ps，等待2个时钟周期
        CHECK_EQ(resumed_times, 8);
        stimulus_runner.get_promise().rethrow_exception();
    }

    TEST_CASE("stage waits observe scheduler phases")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool seen_before_eval{};
        bool seen_after_eval{};

        auto task{
            [](::verilator_utils::eval_scheduler& scheduler,
               bool& seen_before_eval,
               bool& seen_after_eval) -> ::verilator_utils::task
            {
                co_await ::verilator_utils::wait_eval_stage(::verilator_utils::eval_scheduler::eval_stage_enum::before_dut_eval);
                seen_before_eval = true;
                co_await ::verilator_utils::wait_eval_stage(::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval);
                seen_after_eval = true;
            }(scheduler, seen_before_eval, seen_after_eval)};

        ::verilator_utils::async_task runner{scheduler, ::std::move(task)};
        scheduler.loop_once();
        CHECK(seen_before_eval);
        CHECK(seen_after_eval);
        CHECK(runner.done());
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

            void eval()
            {
                CHECK_EQ(scheduler->get_eval_stage(), ::verilator_utils::eval_scheduler::eval_stage_enum::on_dut_eval);
                *seen_on_dut_eval = true;
            }

            const char* hierName() const override final { return "observing_dut"; }

            const char* modelName() const override final { return "observing_dut"; }

            unsigned threads() const override final { return 1; }

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

        auto task{
            [](::verilator_utils::eval_scheduler& scheduler,
               bool& seen_before_eval,
               bool& seen_after_eval) -> ::verilator_utils::task
            {
                co_await ::verilator_utils::wait_eval_stage(::verilator_utils::eval_scheduler::eval_stage_enum::before_dut_eval);
                seen_before_eval = true;
                co_await ::verilator_utils::wait_eval_stage(::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval);
                seen_after_eval = true;
            }(scheduler, seen_before_eval, seen_after_eval)};

        ::verilator_utils::async_task runner{scheduler, ::std::move(task)};
        scheduler.loop_once();
        CHECK(seen_before_eval);
        CHECK(seen_after_eval);
        CHECK(runner.done());
    }

    TEST_CASE("clock and reset helpers drive expected signal sequences")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::CData clk{};
        ::CData reset{};
        ::CData reset_n{};

        auto clock_task{::verilator_utils::generate_clock(::verilator_utils::bit_slice<::CData>{clk}, 4_ps)};
        auto reset_task{::verilator_utils::generate_reset(::verilator_utils::bit_slice<::CData>{reset},
                                                          ::verilator_utils::bit_slice<::CData>{clk},
                                                          1,
                                                          true)};
        auto reset_n_task{::verilator_utils::generate_reset(::verilator_utils::bit_slice<::CData>{reset_n},
                                                            ::verilator_utils::bit_slice<::CData>{clk},
                                                            1,
                                                            false)};

        ::verilator_utils::async_task clock_runner{scheduler, ::std::move(clock_task)};
        ::verilator_utils::async_task reset_runner{scheduler, ::std::move(reset_task)};
        ::verilator_utils::async_task reset_n_runner{scheduler, ::std::move(reset_n_task)};

        scheduler.loop_once();
        CHECK_EQ(clk, 1);
        CHECK_EQ(reset, 1);
        CHECK_EQ(reset_n, 0);

        scheduler.loop_once();
        CHECK_EQ(clk, 0);
        CHECK_EQ(reset, 0);
        CHECK_EQ(reset_n, 1);
        CHECK(reset_runner.done());
        CHECK(reset_n_runner.done());

        scheduler.loop_once();
        CHECK_EQ(clk, 1);
        CHECK_EQ(reset, 0);
        CHECK_EQ(reset_n, 1);

        scheduler.finish();
        scheduler.loop_once();
        CHECK(clock_runner.done());
        clock_runner.get_promise().rethrow_exception();
        reset_runner.get_promise().rethrow_exception();
        reset_n_runner.get_promise().rethrow_exception();
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

        auto task{[](::verilator_utils::eval_scheduler& scheduler,
                     signal_state& clk,
                     ::std::size_t& posedge_count,
                     ::std::size_t& negedge_count,
                     ::std::size_t& alledge_count) -> ::verilator_utils::task
                  {
                      co_await ::verilator_utils::wait_posedge(::verilator_utils::bit_slice<::CData>{clk.value}, 2);
                      posedge_count = 2;
                      co_await ::verilator_utils::wait_negedge(::verilator_utils::bit_slice<::CData>{clk.value}, 1);
                      negedge_count = 1;
                      co_await ::verilator_utils::wait_alledge(::verilator_utils::bit_slice<::CData>{clk.value}, 2);
                      alledge_count = 2;
                  }(scheduler, clk, posedge_count, negedge_count, alledge_count)};

        ::verilator_utils::async_task runner{scheduler, ::std::move(task)};
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
        CHECK(runner.done());
    }

    TEST_CASE("async_task joins and propagates child exceptions")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};

        auto ok_task{[](::verilator_utils::eval_scheduler& scheduler) -> ::verilator_utils::task
                     {
                         co_await ::verilator_utils::wait_eval_stage(
                             ::verilator_utils::eval_scheduler::eval_stage_enum::before_dut_eval);
                     }(scheduler)};
        auto failing_task{[](::verilator_utils::eval_scheduler& scheduler) -> ::verilator_utils::task
                          {
                              co_await ::verilator_utils::wait_eval_stage(
                                  ::verilator_utils::eval_scheduler::eval_stage_enum::before_dut_eval);
                              throw ::std::runtime_error{"boom"};
                              co_return;
                          }(scheduler)};

        ::std::vector<::verilator_utils::async_task> tasks;
        tasks.reserve(2);
        tasks.emplace_back(scheduler, ::std::move(ok_task));
        tasks.emplace_back(scheduler, ::std::move(failing_task));

        scheduler.loop_once();
        CHECK(tasks[0].done());
        CHECK(tasks[1].done());

        auto joiner{::verilator_utils::async_task_join_all(tasks)};
        CHECK(joiner.await_ready());
        CHECK_THROWS_AS(joiner.await_resume(),
                        ::verilator_utils::detail::async_task_join_all_awaiter::unhandled_exception_vector);
    }

    TEST_CASE("async_task can be awaited and propagates child exceptions")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool observed_completion{};
        bool observed_exception{};

        auto child_task{[](::verilator_utils::eval_scheduler& scheduler) -> ::verilator_utils::task
                        { co_await ::verilator_utils::wait_time(1_ps); }(scheduler)};
        ::verilator_utils::async_task child{scheduler, ::std::move(child_task)};
        auto watcher_task{[](::verilator_utils::async_task& child, bool& observed_completion) -> ::verilator_utils::task
                          {
                              co_await child;
                              observed_completion = true;
                          }(child, observed_completion)};
        ::verilator_utils::async_task watcher{scheduler, ::std::move(watcher_task)};

        scheduler.loop_until_finish();
        CHECK(observed_completion);
        CHECK(watcher.done());
        watcher.get_promise().rethrow_exception();

        auto failing_task{[](::verilator_utils::eval_scheduler& scheduler) -> ::verilator_utils::task
                          {
                              co_await ::verilator_utils::wait_time(1_ps);
                              throw ::std::runtime_error{"async child failure"};
                          }(scheduler)};
        ::verilator_utils::async_task failing_child{scheduler, ::std::move(failing_task)};
        auto failing_watcher_task{
            [](::verilator_utils::async_task& failing_child, bool& observed_exception) -> ::verilator_utils::task
            {
                try
                {
                    co_await failing_child;
                }
                catch(const ::std::runtime_error&)
                {
                    observed_exception = true;
                }
            }(failing_child, observed_exception)};
        ::verilator_utils::async_task failing_watcher{scheduler, ::std::move(failing_watcher_task)};

        scheduler.loop_until_finish();
        CHECK(observed_exception);
        CHECK(failing_watcher.done());
        failing_watcher.get_promise().rethrow_exception();
    }

    TEST_CASE("task_join waits for all children and succeeds without exceptions")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool joined{};

        auto child_a{[](::verilator_utils::eval_scheduler& scheduler) -> ::verilator_utils::task
                     { co_await ::verilator_utils::wait_time(1_ps); }(scheduler)};
        auto child_b{[](::verilator_utils::eval_scheduler& scheduler) -> ::verilator_utils::task
                     { co_await ::verilator_utils::wait_time(2_ps); }(scheduler)};

        ::std::vector<::verilator_utils::async_task> tasks;
        tasks.reserve(2);
        tasks.emplace_back(scheduler, ::std::move(child_a));
        tasks.emplace_back(scheduler, ::std::move(child_b));

        auto join_task{[](::verilator_utils::eval_scheduler& scheduler,
                          ::std::vector<::verilator_utils::async_task>& tasks,
                          bool& joined) -> ::verilator_utils::task
                       {
                           co_await ::verilator_utils::async_task_join_all(tasks);
                           joined = true;
                       }(scheduler, tasks, joined)};
        ::verilator_utils::async_task join_runner{scheduler, ::std::move(join_task)};

        scheduler.loop_until_finish();
        CHECK(joined);
        CHECK(join_runner.done());
        CHECK(::std::ranges::all_of(tasks, [](::verilator_utils::async_task& task) { return task.done(); }));
        join_runner.get_promise().rethrow_exception();
    }

    TEST_CASE("finish cooperatively cancels waiting tasks")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool resumed{};

        auto task{[](::verilator_utils::eval_scheduler& scheduler, bool& resumed) -> ::verilator_utils::task
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
                  }(scheduler, resumed)};

        ::verilator_utils::async_task runner{scheduler, ::std::move(task)};
        scheduler.finish();
        scheduler.loop_once();
        CHECK(resumed);
        CHECK(runner.done());
        CHECK(runner.get_promise().is_eval_finish_exception);
        CHECK_FALSE(runner.get_promise().with_unhandled_exception());
    }
}
