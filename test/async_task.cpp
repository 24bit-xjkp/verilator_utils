#include <doctest_macros.hpp>
import unit_test;

namespace
{
    /// 非std::exception派生的异常类型，用于验证join_all_exception对未知异常的描述
    struct non_standard_error
    {
    };

    /**
     * @brief 将异常指针转换为"类型: 消息"形式的字符串
     *
     * 用于校验join_all收集的异常是否保持了原始类型与消息
     *
     * @param exception_ptr 异常指针
     * @return "类型: 消息"形式的字符串
     */
    [[nodiscard]] ::std::string describe_exception(const ::std::exception_ptr& exception_ptr)  // NOLINT(misc-no-recursion)
    {
        try
        {
            ::std::rethrow_exception(exception_ptr);
        }
        catch(const ::verilator_utils::coroutine_exception& exception)
        {
            return ::describe_exception(exception.exception());
        }
        catch(const ::std::runtime_error& exception)
        {
            return ::std::format("runtime_error: {}"sv, exception.what());
        }
        catch(const ::std::logic_error& exception)
        {
            return ::std::format("logic_error: {}"sv, exception.what());
        }
        catch(const ::std::exception& exception)
        {
            return ::std::format("exception: {}"sv, exception.what());
        }
        catch(...)
        {
            return "non-standard";
        }
    }
}  // namespace

TEST_SUITE("verilator_utils/async_task")
{
    using namespace ::std::string_view_literals;

    TEST_CASE("async_task created inside a coroutine resumes its parent and propagates exceptions")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool completed{};
        bool caught_original{};
        bool caught_wrapped{};

        const auto successful_child{[] -> ::verilator_utils::task<void> { co_await ::verilator_utils::wait_time(1_ps); }};
        const auto failing_child{
            [] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_time(1_ps);
                throw ::std::runtime_error{"async child failure"};
            },
        };

        const auto& parent{
            [&] -> ::verilator_utils::task<void> {
                auto pool{co_await ::verilator_utils::spawn_pool()};
                pool.add_task(successful_child());
                co_await pool.join_any();
                completed = true;

                pool.add_task(failing_child());
                try
                {
                    co_await pool.join_any();
                }
                catch(const ::verilator_utils::coroutine_exception&)
                {
                    // 未回溯到根协程，父协程不应收到带栈回溯的包装异常
                    caught_wrapped = true;
                }
                catch(const ::std::runtime_error& exception)
                {
                    caught_original = ::std::string_view{exception.what()} == "async child failure"sv;
                }
            },
        };

        scheduler.add_task(parent());
        CHECK_NOTHROW(scheduler.loop_until_finish());
        CHECK(completed);
        CHECK(caught_original);
        CHECK_FALSE(caught_wrapped);
    }

    TEST_CASE("uncaught async task exception is wrapped with the coroutine stack trace at the root")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::task<void>::handle_t child_handle{};
        bool wrapped{};
        bool recovered_original{};
        ::std::vector<::verilator_utils::coroutine_stacktrace::frame> frames{};

        const auto child_lambda{
            [] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_time(1_ps);
                throw ::std::runtime_error{"async task failure"};
            },
        };

        // 父协程不捕获异步子任务的异常，异常应沿调用链向上传播
        const auto parent{
            [&](this auto) -> ::verilator_utils::task<void> {
                auto child{co_await ::verilator_utils::to_async(child_lambda())};
                child_handle = child.handle();
                co_await child;
            },
        };

        scheduler.add_task(parent());
        try
        {
            scheduler.loop_until_finish();
        }
        catch(const ::verilator_utils::coroutine_exception& exception)
        {
            wrapped = true;
            frames = exception.stacktrace().frames;
            // 原始异常类型可通过as<>取回
            const auto* original{exception.as<::std::runtime_error>()};
            REQUIRE_NE(original, nullptr);
            recovered_original = ::std::string_view{original->what()} == "async task failure"sv;
        }

        CHECK(wrapped);
        CHECK(recovered_original);
        // 异常在根协程注入栈回溯：抛出异常的异步子协程与等待它的根协程
        REQUIRE_EQ(frames.size(), 2u);
        CHECK_EQ(frames[0].coroutine_frame_ptr, child_handle.address());
        CHECK_EQ(frames[1].type, ::verilator_utils::detail::promise_base::coroutine_type_enum::root_coroutine);
    }

    TEST_CASE("async_task reports coroutine ownership and completion state")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool parent_finished{};

        // 子任务立即执行完毕，父任务恢复时即可观察到完成状态
        const auto child_lambda{[] -> ::verilator_utils::task<void> { co_return; }};

        const auto parent{
            [&] -> ::verilator_utils::task<void> {
                auto child{co_await ::verilator_utils::to_async(child_lambda())};
                CHECK(child.joinable());
                CHECK(static_cast<bool>(child));
                CHECK_FALSE(child.done());
                CHECK_EQ(child.promise().is_async, true);

                // 让出执行权，使异步子任务运行到完成
                co_await ::verilator_utils::wait_time(1_ps);
                CHECK(child.done());
                CHECK_EQ(child.promise().status, ::verilator_utils::task<void>::status_enum::finished);

                // 已完成的异步任务立即就绪，等待后对象不再持有协程
                co_await child;
                CHECK_FALSE(child.joinable());
                parent_finished = true;
            },
        };

        scheduler.add_task(parent());
        CHECK_NOTHROW(scheduler.loop_until_finish());
        CHECK(parent_finished);
    }

    TEST_CASE("async_task accessors reject a task that no longer owns a coroutine")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool move_checked{};
        bool await_checked{};

        const auto child_lambda{[] -> ::verilator_utils::task<void> { co_await ::verilator_utils::wait_time(1_ps); }};

        const auto parent{
            [&] -> ::verilator_utils::task<void> {
                auto child{co_await ::verilator_utils::to_async(child_lambda())};
                auto owner{::std::move(child)};
                CHECK(owner.joinable());
                // 移动后源对象不再绑定协程，需要协程的访问器都触发断言而不是解引用空句柄
                // NOLINTBEGIN(bugprone-use-after-move)
                CHECK_FALSE(child.joinable());
                CHECK_FALSE(static_cast<bool>(child));
                CHECK_THROWS_WITH_AS(static_cast<void>(child.promise()),
                                     ::doctest::Contains{"不能获取承诺体"},
                                     ::verilator_utils::assertion_error);
                CHECK_THROWS_WITH_AS(static_cast<void>(child.done()),
                                     ::doctest::Contains{"不能检查是否完成"},
                                     ::verilator_utils::assertion_error);
                CHECK_THROWS_WITH_AS(static_cast<void>(child.cancel_possible()),
                                     ::doctest::Contains{"不能检查取消状态"},
                                     ::verilator_utils::assertion_error);
                CHECK_THROWS_WITH_AS(static_cast<void>(child.cancel_requested()),
                                     ::doctest::Contains{"不能检查取消状态"},
                                     ::verilator_utils::assertion_error);
                CHECK_THROWS_WITH_AS(child.cancel(), ::doctest::Contains{"不能取消"}, ::verilator_utils::assertion_error);
                // 不可等待：co_await未绑定协程的任务在operator co_await处触发断言
                CHECK_THROWS_WITH_AS(child.operator co_await(),
                                     ::doctest::Contains{"不能等待"},
                                     ::verilator_utils::assertion_error);
                // NOLINTEND(bugprone-use-after-move)
                move_checked = true;

                // 等待后异步任务把协程交给可等待体，对象同样不再绑定协程
                co_await owner;
                CHECK_FALSE(owner.joinable());
                CHECK_THROWS_WITH_AS(static_cast<void>(owner.promise()),
                                     ::doctest::Contains{"不能获取承诺体"},
                                     ::verilator_utils::assertion_error);
                await_checked = true;
            },
        };

        scheduler.add_task(parent());
        CHECK_NOTHROW(scheduler.loop_until_finish());
        CHECK(move_checked);
        CHECK(await_checked);
    }

    TEST_CASE("async_task rejects a task bound to a different scheduler")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        scheduler_fixture other_fixture{};
        auto other_scheduler{other_fixture.make_scheduler()};

        const auto parent{[] -> ::verilator_utils::task<void> { co_return; }()};
        parent.promise().scheduler = ::std::addressof(scheduler);
        auto child{[] -> ::verilator_utils::task<void> { co_return; }()};
        child.promise().scheduler = ::std::addressof(other_scheduler);

        CHECK_THROWS_WITH_AS((::verilator_utils::async_task{parent.promise(), ::std::move(child)}),
                             ::doctest::Contains{"调度器"},
                             ::verilator_utils::assertion_error);
    }

    TEST_CASE("async_task accepts a task with no scheduler or the parent scheduler")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};

        const auto parent{[] -> ::verilator_utils::task<void> { co_return; }()};
        parent.promise().scheduler = ::std::addressof(scheduler);
        auto unbound_child{[] -> ::verilator_utils::task<void> { co_return; }()};
        auto bound_child{[] -> ::verilator_utils::task<void> { co_return; }()};
        bound_child.promise().scheduler = ::std::addressof(scheduler);

        CHECK_NOTHROW((::verilator_utils::async_task{parent.promise(), ::std::move(unbound_child)}));
        CHECK_NOTHROW((::verilator_utils::async_task{parent.promise(), ::std::move(bound_child)}));
        scheduler.loop_until_finish();
    }

    TEST_CASE("canceling an async task before it starts skips its coroutine body")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool child_body_ran{};
        bool parent_caught{};
        bool parent_finished{};

        const auto child_lambda{
            [&] -> ::verilator_utils::task<void> {
                child_body_ran = true;
                co_return;
            },
        };

        const auto parent{
            [&] -> ::verilator_utils::task<void> {
                auto child{co_await ::verilator_utils::to_async(child_lambda())};
                // 异步任务创建后立即进入调度器就绪队列，此时处于从未执行的initial_suspend状态
                CHECK(child.cancel_possible());
                CHECK_FALSE(child.cancel_requested());

                child.cancel();
                CHECK(child.cancel_requested());
                CHECK_EQ(child.promise().status, ::verilator_utils::task<void>::status_enum::cancel_requested);
                CHECK_FALSE(child.cancel_possible());

                // 与同步任务一致：等待被取消的异步任务抛出子任务取消异常，而不是静默返回
                try
                {
                    co_await child;
                }
                catch(const ::verilator_utils::subtask_cancel_exception&)
                {
                    parent_caught = true;
                }
                parent_finished = true;
            },
        };

        scheduler.add_task(parent());
        CHECK_NOTHROW(scheduler.loop_until_finish());
        // 取消请求在恢复点生效：协程体一次都不会运行
        CHECK_FALSE(child_body_ran);
        CHECK(parent_caught);
        CHECK(parent_finished);
    }

    TEST_CASE("canceling a suspended async task drops the remainder of its body")
    {
        ::std::size_t destroyed{};
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool child_started{};
        bool resumed_after_wait{};
        bool parent_caught{};
        bool parent_finished{};

        const auto child_lambda{
            [&] -> ::verilator_utils::task<void> {
                const frame_destruction_counter counter{&destroyed};
                child_started = true;
                co_await ::verilator_utils::wait_time(5_ps);
                resumed_after_wait = true;
            },
        };

        const auto parent{
            [&] -> ::verilator_utils::task<void> {
                auto child{co_await ::verilator_utils::to_async(child_lambda())};
                CHECK(child.cancel_possible());
                CHECK_FALSE(child.cancel_requested());

                // 让出执行权，使异步子任务开始执行并挂起在等待队列上
                co_await ::verilator_utils::wait_time(1_ps);
                CHECK(child_started);
                CHECK_FALSE(child.done());
                CHECK_EQ(child.promise().status, ::verilator_utils::task<void>::status_enum::suspended);
                CHECK(child.cancel_possible());

                child.cancel();
                CHECK(child.cancel_requested());
                CHECK_EQ(child.promise().status, ::verilator_utils::task<void>::status_enum::cancel_requested);
                // 已收到取消请求的任务不在可取消状态，重复请求触发断言
                CHECK_FALSE(child.cancel_possible());
                CHECK_THROWS_WITH_AS(child.cancel(), ::doctest::Contains{"不可取消"}, ::verilator_utils::assertion_error);

                // 取消自身不产生未处理异常，但等待被取消的异步任务会收到子任务取消异常
                try
                {
                    co_await child;
                }
                catch(const ::verilator_utils::subtask_cancel_exception&)
                {
                    parent_caught = true;
                }
                parent_finished = true;
            },
        };

        scheduler.add_task(parent());
        CHECK_NOTHROW(scheduler.loop_until_finish());
        // 取消请求在恢复点生效：子任务不再继续执行
        CHECK(child_started);
        CHECK_FALSE(resumed_after_wait);
        CHECK(parent_caught);
        CHECK(parent_finished);
        // 被取消的异步任务在等待完成后回收协程帧
        CHECK_EQ(destroyed, 1zu);
    }

    TEST_CASE("uncaught async task cancellation propagates out of the scheduler")
    {
        ::std::size_t destroyed{};
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool resumed_after_wait{};
        ::verilator_utils::task<void>::promise_type* child_promise{};

        const auto child_lambda{
            [&] -> ::verilator_utils::task<void> {
                const frame_destruction_counter counter{&destroyed};
                child_promise = ::std::addressof(
                    (co_await ::verilator_utils::handle<::verilator_utils::task<void>::promise_type>()).promise());
                co_await ::verilator_utils::wait_time(5_ps);
                resumed_after_wait = true;
            },
        };

        // 父任务不捕获异步子任务的取消异常，异常应沿调用链向上传播
        const auto parent{
            [&] -> ::verilator_utils::task<void> {
                auto child{co_await ::verilator_utils::to_async(child_lambda())};
                co_await child;
            },
        };

        scheduler.add_task(parent());
        // 只执行就绪队列：父任务挂起在异步子任务上，异步子任务挂起在等待队列上
        scheduler.initial_eval();

        REQUIRE(child_promise != nullptr);
        CHECK_EQ(child_promise->status, ::verilator_utils::task<void>::status_enum::suspended);
        CHECK(child_promise->cancel_possible());
        child_promise->cancel();
        CHECK_EQ(child_promise->status, ::verilator_utils::task<void>::status_enum::cancel_requested);

        CHECK_THROWS_WITH_AS(scheduler.loop_until_finish(),
                             ::doctest::Contains{"子任务取消"},
                             ::verilator_utils::coroutine_exception);
        CHECK_FALSE(resumed_after_wait);
        // 被取消的异步子任务不再继续执行，其协程帧在父任务展开时回收
        CHECK_EQ(destroyed, 1zu);
    }

    TEST_CASE("spawn_pool join_all collects the cancellation of a canceled child")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::task<void>::promise_type* canceled_promise{};
        bool survivor_finished{};
        ::std::size_t exception_count{};
        bool cancel_rethrown{};

        const auto canceled_child{
            [&] -> ::verilator_utils::task<void> {
                canceled_promise = ::std::addressof(
                    (co_await ::verilator_utils::handle<::verilator_utils::task<void>::promise_type>()).promise());
                co_await ::verilator_utils::wait_time(5_ps);
            },
        };
        const auto survivor_child{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_time(3_ps);
                survivor_finished = true;
            },
        };

        const auto parent{
            [&] -> ::verilator_utils::task<void> {
                auto pool{co_await ::verilator_utils::spawn_pool()};
                pool.add_task(canceled_child());
                pool.add_task(survivor_child());
                // 让出执行权，使两个子任务都挂起在等待队列上
                co_await ::verilator_utils::wait_time(1_ps);
                CHECK_NE(canceled_promise, nullptr);
                if(canceled_promise != nullptr) { canceled_promise->cancel(); }

                try
                {
                    co_await pool.join_all();
                }
                catch(const ::verilator_utils::spawn_pool::join_all_exception& join_all_exception)
                {
                    const auto& exceptions{join_all_exception.exceptions()};
                    exception_count = exceptions.size();
                    CHECK_FALSE(exceptions.empty());
                    if(!exceptions.empty())
                    {
                        try
                        {
                            ::std::rethrow_exception(exceptions.front());
                        }
                        catch(const ::verilator_utils::subtask_cancel_exception&)
                        {
                            cancel_rethrown = true;
                        }
                    }
                }
            },
        };

        scheduler.add_task(parent());
        CHECK_NOTHROW(scheduler.loop_until_finish());
        // join_all等待所有子任务：被取消的子任务与其他子任务的异常一样被汇总
        CHECK(survivor_finished);
        CHECK_EQ(exception_count, 1zu);
        CHECK(cancel_rethrown);
    }

    TEST_CASE("scheduler reclaims detached async tasks suspended on a never-fired event")
    {
        ::std::size_t destroyed{};
        {
            scheduler_fixture fixture{};
            auto scheduler{fixture.make_scheduler()};
            ::verilator_utils::event event{};
            const auto make_waiter{[&] -> ::verilator_utils::task<void> {
                const frame_destruction_counter counter{&destroyed};
                co_await event;
            }};

            // 异步任务必须在协程上下文中创建：父协程结束后async_task对象析构，
            // 挂起在调度器外的异步子协程被分离并托管给调度器，由调度器兜底回收
            scheduler.add_task([&](this auto) -> ::verilator_utils::task<void> {
                [[maybe_unused]] const auto first{co_await ::verilator_utils::to_async(make_waiter())};
                [[maybe_unused]] const auto second{co_await ::verilator_utils::to_async(make_waiter())};
                // 先让两个异步子协程挂起到event上，再结束父协程
                co_await ::verilator_utils::wait_time(1_ps);
            }());
            // 两个异步子协程挂起在永不触发的事件上：死锁诊断抛出runtime_error
            CHECK_THROWS_AS(scheduler.loop_until_finish(), ::std::runtime_error);
            scheduler.finish();
        }
        CHECK_EQ(destroyed, 2zu);
    }

    TEST_CASE("awaiting an async child records the parent suspend point")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        signal_state signal{};

        const auto child_lambda{
            [&] -> ::verilator_utils::task<void> {
                [[maybe_unused]] const auto handle{
                    co_await ::verilator_utils::handle<::verilator_utils::task<void>::promise_type>()};
                co_await ::verilator_utils::wait_event([&] { return signal.value != 0; });
            },
        };
        ::std::uint32_t recorded_line{};
        const auto parent{
            [&] -> ::verilator_utils::task<void> {
                auto async_child{co_await ::verilator_utils::to_async(child_lambda())};
                recorded_line = ::std::source_location::current().line();
                co_await async_child;
            },
        };
        auto parent_task{parent()};
        const auto& promise{parent_task.promise()};
        scheduler.add_task(::std::move(parent_task));

        scheduler.loop_once();
        CHECK_EQ(promise.status, ::verilator_utils::task<void>::status_enum::suspended);
        REQUIRE_NE(promise.suspend_location.line(), 0u);
        CHECK_EQ(promise.suspend_location.line(), recorded_line + 1);

        signal.value = 1;
        scheduler.loop_once();
    }

    TEST_CASE("spawn_pool rejects a task that does not carry a coroutine")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool rejected{};
        bool joined{};

        const auto child_lambda{[] -> ::verilator_utils::task<void> { co_await ::verilator_utils::wait_time(1_ps); }};

        const auto parent{
            [&] -> ::verilator_utils::task<void> {
                auto pool{co_await ::verilator_utils::spawn_pool()};
                CHECK(pool.empty());
                CHECK_FALSE(pool.joinable());

                auto source{child_lambda()};
                auto owner{::std::move(source)};
                // NOLINTBEGIN(bugprone-use-after-move)
                CHECK_THROWS_WITH_AS(pool.add_task(::std::move(source)),
                                     ::doctest::Contains{"任务未绑定协程，不能转化为异步任务"},
                                     ::verilator_utils::assertion_error);
                // NOLINTEND(bugprone-use-after-move)
                rejected = true;
                // 被拒绝的任务不会进入任务池
                CHECK(pool.empty());
                CHECK_FALSE(pool.joinable());

                // 检查失败后任务池仍然可用
                pool.add_task(::std::move(owner));
                CHECK(pool.joinable());
                co_await pool.join_all();
                CHECK(pool.empty());
                joined = true;
            },
        };

        scheduler.add_task(parent());
        CHECK_NOTHROW(scheduler.loop_until_finish());
        CHECK(rejected);
        CHECK(joined);
    }

    TEST_CASE("spawn_pool join_all waits for every child and collects exceptions")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool joined{};
        bool successful_child_completed{};
        ::std::size_t exception_count{};
        ::std::vector<::std::string> descriptions;
        ::std::string message;

        // 成功任务最后完成，用于验证join_all会等待所有子任务而不只是失败的任务
        const auto successful_child{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_time(3_ps);
                successful_child_completed = true;
            },
        };
        const auto first_failing_child{
            [] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_time(2_ps);
                throw ::std::runtime_error{"first failure"};
            },
        };
        const auto second_failing_child{
            [] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_time(1_ps);
                throw ::std::logic_error{"second failure"};
            },
        };

        const auto parent{
            [&] -> ::verilator_utils::task<void> {
                auto pool{co_await ::verilator_utils::spawn_pool()};
                pool.add_task(successful_child());
                pool.add_task(first_failing_child());
                pool.add_task(second_failing_child());
                try
                {
                    co_await pool.join_all();
                }
                catch(const ::verilator_utils::spawn_pool::join_all_exception& join_all_exception)
                {
                    message = join_all_exception.what();
                    exception_count = join_all_exception.exceptions().size();
                    for(const auto& exception_ptr: join_all_exception.exceptions())
                    {
                        descriptions.emplace_back(::describe_exception(exception_ptr));
                    }
                }
                joined = pool.empty();
            },
        };
        scheduler.add_task(parent());
        scheduler.loop_until_finish();
        CHECK(successful_child_completed);
        CHECK(joined);
        // 异常按子任务加入任务池的顺序收集，与子任务的完成顺序无关
        CHECK_EQ(exception_count, 2u);
        CHECK_EQ(descriptions[0], ::doctest::Contains{"runtime_error: first failure"});
        CHECK_EQ(descriptions[1], ::doctest::Contains{"logic_error: second failure"});
        // 收集的是原始异常，因此what()只按"序号: 消息"逐行列出所有异常
        CHECK_EQ(message, "1: first failure\n2: second failure\n"sv);
    }

    TEST_CASE("spawn_pool join_all reports an exception message longer than the format buffer")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        // 消息长度跨越格式化缓冲区的容量，用于回归长消息的拼接路径
        const ::std::string failure(300, 'x');
        ::std::string message;
        bool caught{};

        const auto failing_child{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_time(1_ps);
                throw ::std::runtime_error{failure};
            },
        };
        const auto parent{
            [&] -> ::verilator_utils::task<void> {
                auto pool{co_await ::verilator_utils::spawn_pool()};
                pool.add_task(failing_child());
                try
                {
                    co_await pool.join_all();
                }
                catch(const ::verilator_utils::spawn_pool::join_all_exception& join_all_exception)
                {
                    message = join_all_exception.what();
                    caught = true;
                }
            },
        };
        scheduler.add_task(parent());
        scheduler.loop_until_finish();
        CHECK(caught);
        CHECK_EQ(message, ::std::format("1: {}\n", failure));
    }

    TEST_CASE("spawn_pool join_all exception is catchable as std::exception")
    {
        static_assert(::std::derived_from<::verilator_utils::spawn_pool::join_all_exception, ::std::exception>);

        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool caught_as_join_all{};
        ::std::size_t exception_count{};
        ::std::string message;

        const auto failing_child{
            [] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_time(1_ps);
                throw ::std::runtime_error{"single failure"};
            },
        };

        const auto parent{
            [&] -> ::verilator_utils::task<void> {
                auto pool{co_await ::verilator_utils::spawn_pool()};
                pool.add_task(failing_child());
                try
                {
                    co_await pool.join_all();
                }
                catch(const ::std::exception& exception)
                {
                    message = exception.what();
                    try
                    {
                        // 重新抛出以验证通过基类捕获不会切掉派生类型
                        throw;
                    }
                    catch(const ::verilator_utils::spawn_pool::join_all_exception& join_all_exception)
                    {
                        caught_as_join_all = true;
                        exception_count = join_all_exception.exceptions().size();
                    }
                }
            },
        };
        scheduler.add_task(parent());
        scheduler.loop_until_finish();
        CHECK(caught_as_join_all);
        CHECK_EQ(exception_count, 1u);
        CHECK_EQ(message, "1: single failure\n"sv);
    }

    TEST_CASE("spawn_pool join_all describes non-standard exceptions as unknown")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool non_standard_rethrown{};
        ::std::size_t exception_count{};
        ::std::vector<::std::string> descriptions;
        ::std::string message;

        const auto failing_child{
            [] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_time(1_ps);
                // NOLINTNEXTLINE(bugprone-std-exception-baseclass)
                throw ::non_standard_error{};
            },
        };

        const auto parent{
            [&] -> ::verilator_utils::task<void> {
                auto pool{co_await ::verilator_utils::spawn_pool()};
                pool.add_task(failing_child());
                try
                {
                    co_await pool.join_all();
                }
                catch(const ::verilator_utils::spawn_pool::join_all_exception& exception)
                {
                    message = exception.what();
                    exception_count = exception.exceptions().size();
                    for(const auto& exception_ptr: exception.exceptions())
                    {
                        descriptions.emplace_back(::describe_exception(exception_ptr));
                        try
                        {
                            ::std::rethrow_exception(exception_ptr);
                        }
                        catch(const ::non_standard_error&)
                        {
                            non_standard_rethrown = true;
                        }
                    }
                }
            },
        };
        scheduler.add_task(parent());
        scheduler.loop_until_finish();
        // 非std::exception异常仍保留原始类型，但无法提取消息
        CHECK(non_standard_rethrown);
        CHECK_EQ(exception_count, 1u);
        CHECK_EQ(descriptions[0], ::doctest::Contains{"non-standard"});
        CHECK_EQ(message, "1: unknown\n"sv);
    }

    TEST_CASE("spawn_pool join_any removes the completed child regardless of position")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::std::vector<int> completed;
        bool joined{};

        const auto first_child{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_time(2_ps);
                completed.emplace_back(1);
            },
        };
        const auto second_child{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_time(1_ps);
                completed.emplace_back(2);
            },
        };

        const auto parent{
            [&] -> ::verilator_utils::task<void> {
                auto pool{co_await ::verilator_utils::spawn_pool()};
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

    TEST_CASE("spawn_pool join_any reports the cancellation of a canceled child")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::task<void>::promise_type* canceled_promise{};
        bool cancel_caught{};
        bool survivor_finished{};
        bool drained{};

        const auto canceled_child{
            [&] -> ::verilator_utils::task<void> {
                canceled_promise = ::std::addressof(
                    (co_await ::verilator_utils::handle<::verilator_utils::task<void>::promise_type>()).promise());
                co_await ::verilator_utils::wait_time(2_ps);
            },
        };
        const auto survivor_child{
            [&] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_time(5_ps);
                survivor_finished = true;
            },
        };

        const auto parent{
            [&] -> ::verilator_utils::task<void> {
                auto pool{co_await ::verilator_utils::spawn_pool()};
                pool.add_task(canceled_child());
                pool.add_task(survivor_child());
                // 让出执行权，使两个子任务都挂起在等待队列上
                co_await ::verilator_utils::wait_time(1_ps);
                CHECK_NE(canceled_promise, nullptr);
                if(canceled_promise != nullptr) { canceled_promise->cancel(); }

                // 被取消的子任务先到达恢复点：join_any报告取消，并把它移出任务池
                try
                {
                    co_await pool.join_any();
                }
                catch(const ::verilator_utils::subtask_cancel_exception&)
                {
                    cancel_caught = true;
                }
                CHECK_FALSE(pool.empty());

                // 取消只影响被取消的子任务：再次join_any仍然会等待其余子任务正常完成
                co_await pool.join_any();
                drained = pool.empty();
            },
        };

        scheduler.add_task(parent());
        CHECK_NOTHROW(scheduler.loop_until_finish());
        CHECK(cancel_caught);
        CHECK(survivor_finished);
        CHECK(drained);
    }

    TEST_CASE("scheduler reclaims a pool of subtasks suspended on a never-fired event")
    {
        ::std::size_t destroyed{};
        {
            scheduler_fixture fixture{};
            auto scheduler{fixture.make_scheduler()};
            const auto parent_lambda{[&] -> ::verilator_utils::task<void> {
                const frame_destruction_counter pool_counter{&destroyed};
                auto pool{co_await ::verilator_utils::spawn_pool()};
                ::verilator_utils::event event{};
                const auto subtask{[&] -> ::verilator_utils::task<void> {
                    const frame_destruction_counter subtask_counter{&destroyed};
                    co_await event;
                }};
                pool.add_task(subtask());
                pool.add_task(subtask());
                co_await pool.join_all();
            }};
            scheduler.add_task(parent_lambda());
            scheduler.initial_eval();
        }
        // 父任务与两个挂起在事件上的子任务都应由调度器回收
        CHECK_EQ(destroyed, 3zu);
    }

    TEST_CASE("cleanup unfinished async tasks and pools when scheduler destroyed")
    {
        ::std::size_t destroyed{};
        const auto just_wait{
            [&] -> ::verilator_utils::task<void> {
                const frame_destruction_counter counter{&destroyed};
                co_await ::verilator_utils::wait_time(1_ns);
            },
        };
        // join_none：子任务被托管给调度器，父协程继续等待
        const auto join_none_parent{
            [&] -> ::verilator_utils::task<void> {
                auto pool{co_await ::verilator_utils::spawn_pool()};
                pool.add_task(just_wait());
                pool.add_task(just_wait());
                co_await pool.join_none();
                co_await ::verilator_utils::wait_time(1_ns);
            },
        };
        // join_all：父协程等待所有子任务完成
        const auto join_all_parent{
            [&] -> ::verilator_utils::task<void> {
                auto pool{co_await ::verilator_utils::spawn_pool()};
                pool.add_task(just_wait());
                pool.add_task(just_wait());
                co_await pool.join_all();
            },
        };
        // join_any：父协程等待任一子任务完成
        const auto join_any_parent{
            [&] -> ::verilator_utils::task<void> {
                auto pool{co_await ::verilator_utils::spawn_pool()};
                pool.add_task(just_wait());
                pool.add_task(just_wait());
                co_await pool.join_any();
            },
        };
        // 不join：父协程析构任务池，子任务被分离给调度器
        const auto detached_parent{
            [&] -> ::verilator_utils::task<void> {
                auto pool{co_await ::verilator_utils::spawn_pool()};
                pool.add_task(just_wait());
                pool.add_task(just_wait());
                co_await ::verilator_utils::wait_time(1_ns);
            },
        };
        // 子任务挂起在永不触发的事件上，由调度器通过挂起队列兜底回收
        const auto event_parent{
            [&] -> ::verilator_utils::task<void> {
                auto pool{co_await ::verilator_utils::spawn_pool()};
                ::verilator_utils::event event{};
                const auto subtask{[&] -> ::verilator_utils::task<void> {
                    const frame_destruction_counter counter{&destroyed};
                    co_await event;
                }};
                pool.add_task(subtask());
                pool.add_task(subtask());
                co_await pool.join_all();
            },
        };
        {
            scheduler_fixture fixture{};
            auto scheduler{fixture.make_scheduler()};
            scheduler.add_task(join_none_parent());
            scheduler.add_task(join_all_parent());
            scheduler.add_task(join_any_parent());
            scheduler.add_task(detached_parent());
            scheduler.add_task(event_parent());
            // 只执行就绪队列：以上任务都挂起而未完成
            scheduler.initial_eval();
        }
        // 调度器析构时应回收全部未完成的子任务协程帧：5种任务池配置，每组2个子任务
        CHECK_EQ(destroyed, 10zu);
    }

    TEST_CASE("co_await add_task schedules another root task in the same evaluation round")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::std::vector<::std::string> order{};

        const auto child_lambda{
            [&] -> ::verilator_utils::task<void> {
                order.emplace_back("child");
                co_return;
            },
        };
        const auto parent_lambda{
            [&] -> ::verilator_utils::task<void> {
                order.emplace_back("parent");
                co_await ::verilator_utils::add_task(child_lambda());
                order.emplace_back("parent-finished");
            },
        };

        scheduler.add_task(parent_lambda());
        scheduler.loop_until_finish();

        // add_task不挂起父协程：父协程先执行完，新加入的任务在同一轮评估中被执行
        CHECK_EQ(order, (::std::vector<::std::string>{"parent", "parent-finished", "child"}));
    }
}
