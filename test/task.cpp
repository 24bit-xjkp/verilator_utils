#include <doctest_macros.hpp>
import unit_test;

namespace
{
    auto to_vector(::std::size_t n) noexcept { return ::std::views::take(n) | ::std::ranges::to<::std::vector<bool>>(); }

    /// 带静态存活计数的move-only类型，用于验证mailbox对元素的析构平衡
    struct lifecycle_counter
    {
        static ::std::size_t live_count;
        int value{};

        explicit lifecycle_counter(int value) : value{value} { ++live_count; }

        lifecycle_counter(const lifecycle_counter&) = delete;
        lifecycle_counter& operator= (const lifecycle_counter&) = delete;

        lifecycle_counter(lifecycle_counter&& other) noexcept : value{::std::exchange(other.value, -1)} { ++live_count; }

        lifecycle_counter& operator= (lifecycle_counter&& other) noexcept
        {
            value = ::std::exchange(other.value, -1);
            return *this;
        }

        ~lifecycle_counter() { --live_count; }
    };

    constinit ::std::size_t lifecycle_counter::live_count{};

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
    [[nodiscard]] ::std::string describe_exception(const ::std::exception_ptr& exception_ptr)
    {
        try
        {
            ::std::rethrow_exception(exception_ptr);
        }
        catch(const ::std::runtime_error& exception)
        {
            return ::std::format("runtime_error: {}", exception.what());
        }
        catch(const ::std::logic_error& exception)
        {
            return ::std::format("logic_error: {}", exception.what());
        }
        catch(const ::std::exception& exception)
        {
            return ::std::format("exception: {}", exception.what());
        }
        catch(...)
        {
            return "non-standard";
        }
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
        self_handle = co_await ::verilator_utils::get_handle<::verilator_utils::task<void>::promise_type>();
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
        self_handle = co_await ::verilator_utils::get_handle<::verilator_utils::task<void>::promise_type>();
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
        self_handle = co_await ::verilator_utils::get_handle<::verilator_utils::task<void>::promise_type>();
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

}  // namespace

TEST_SUITE("verilator_utils/task")
{
    using namespace ::std::string_view_literals;

    TEST_CASE("fibonacci LFSR generates the expected maximal-length sequence")
    {
        const auto bits{::verilator_utils::fibonacci_lfsr_generator(3) | ::to_vector(14)};
        constexpr static ::std::array expected_period{true, false, false, true, true, true, false};

        REQUIRE_EQ(bits.size(), 14u);
        CHECK(::std::ranges::equal(bits | ::std::views::take(7), expected_period));
        CHECK(::std::ranges::equal(bits | ::std::views::drop(7), expected_period));
    }

    TEST_CASE("galois LFSR generates the expected maximal-length sequence")
    {
        const auto bits{::verilator_utils::galois_lfsr_generator(3) | ::to_vector(14)};
        constexpr static ::std::array expected_period{true, false, true, true, true, false, false};

        REQUIRE_EQ(bits.size(), 14u);
        CHECK(::std::ranges::equal(bits | ::std::views::take(7), expected_period));
        CHECK(::std::ranges::equal(bits | ::std::views::drop(7), expected_period));
    }

    TEST_CASE("LFSR generators honor custom feedback initial value and repeat count")
    {
        constexpr static ::std::array expected_sequence1{true, true, false, true, false, true, true, false};
        CHECK(::std::ranges::equal(::verilator_utils::fibonacci_lfsr_generator(4, 0b1'001, 0b1'011) | ::to_vector(8),
                                   expected_sequence1));
        constexpr static ::std::array expected_sequence2{true, true, false, false, false, true, false, false};
        CHECK(::std::ranges::equal(::verilator_utils::galois_lfsr_generator(4, 0b1'001, 0b1'011) | ::to_vector(8),
                                   expected_sequence2));
        constexpr static ::std::array expected_sequence3{false};
        CHECK(::std::ranges::equal(::verilator_utils::fibonacci_lfsr_generator(64, 1, 1zu << 63zu) | ::to_vector(1),
                                   expected_sequence3));
        CHECK(::std::ranges::equal(::verilator_utils::galois_lfsr_generator(64, 1, 1zu << 63zu) | ::to_vector(1),
                                   expected_sequence3));
    }

    TEST_CASE("zero repeat count leaves LFSR generators unbounded")
    {
        const auto fibonacci{::verilator_utils::fibonacci_lfsr_generator(3)};
        auto fibonacci_iter{fibonacci.begin()};
        for(const bool expected: {true, false, false, true, true, true, false, true})
        {
            REQUIRE_NE(fibonacci_iter, fibonacci.end());
            CHECK_EQ(*fibonacci_iter, expected);
            ++fibonacci_iter;
        }

        const auto galois{::verilator_utils::galois_lfsr_generator(3)};
        auto galois_iter{galois.begin()};
        for(const bool expected: {true, false, true, true, true, false, false, true})
        {
            REQUIRE_NE(galois_iter, galois.end());
            CHECK_EQ(*galois_iter, expected);
            ++galois_iter;
        }
    }

    // NOLINTBEGIN(bugprone-unchecked-optional-access)

    TEST_CASE("mailbox nonblocking operations preserve FIFO order and capacity")
    {
        const scheduler_fixture fixture{};
        ::verilator_utils::mailbox<int> mailbox{2};

        bool put1{};
        bool put2{};
        bool put3{};
        ::std::optional<int> empty_get{};
        bool empty_peek{};
        int peeked_value{};
        ::std::optional<int> first{};
        ::std::optional<int> second{};

        empty_get = mailbox.try_get();
        empty_peek = !mailbox.try_peek().has_value();
        put1 = mailbox.try_put(10);
        put2 = mailbox.try_put(20);
        put3 = mailbox.try_put(30);
        // try_peek 返回只读引用，且不会删除队首元素
        if(auto peeked{mailbox.try_peek()}) { peeked_value = *peeked; }
        first = mailbox.try_get();
        second = mailbox.try_get();

        CHECK(put1);
        CHECK(put2);
        CHECK_FALSE(put3);
        CHECK_FALSE(empty_get.has_value());
        CHECK(empty_peek);
        CHECK_EQ(peeked_value, 10);
        CHECK_EQ(mailbox.num(), 0u);
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        CHECK_EQ(*first, 10);
        CHECK_EQ(*second, 20);
    }

    TEST_CASE("unbounded mailbox accepts move-only values")
    {
        const scheduler_fixture fixture{};
        ::verilator_utils::mailbox<::std::unique_ptr<int>> mailbox{};

        bool put_succeeded{};
        put_succeeded = mailbox.try_put(::std::make_unique<int>(42));
        auto value{mailbox.try_get()};

        CHECK(put_succeeded);
        CHECK_EQ(mailbox.num(), 0u);
        REQUIRE(value.has_value());
        REQUIRE(*value);
        CHECK_EQ(**value, 42);
    }

    TEST_CASE("mailbox keeps element construction and destruction balanced")
    {
        CHECK_EQ(lifecycle_counter::live_count, 0u);

        // 有限容量环形缓冲多次回绕后，槽位中的元素被正确析构
        {
            ::verilator_utils::mailbox<lifecycle_counter> mailbox{3};
            CHECK(mailbox.try_put(1));
            CHECK(mailbox.try_get());
            CHECK(mailbox.try_put(2));
            CHECK(mailbox.try_put(3));
            CHECK(mailbox.try_get());
            CHECK(mailbox.try_put(4));
            CHECK_EQ(mailbox.num(), 2u);
        }
        CHECK_EQ(lifecycle_counter::live_count, 0u);

        // 无限容量：消费部分元素后继续放入触发vector扩容，不得泄漏已销毁槽位的元素
        {
            ::verilator_utils::mailbox<lifecycle_counter> mailbox{};
            for(int i{}; i != 5; ++i) { CHECK(mailbox.try_put(i)); }
            CHECK(mailbox.try_get());
            for(int i{}; i != 100; ++i) { CHECK(mailbox.try_put(100 + i)); }
            while(mailbox.try_get()) {}
        }
        CHECK_EQ(lifecycle_counter::live_count, 0u);

        // 无限容量：消费超过水印数量触发前缀擦除，析构计数保持平衡
        {
            ::verilator_utils::mailbox<lifecycle_counter> mailbox{};
            constexpr ::std::size_t count{2048};
            for(::std::size_t i{}; i != count; ++i) { CHECK(mailbox.try_put(static_cast<int>(i))); }
            for(::std::size_t i{}; i != count; ++i) { CHECK(mailbox.try_get()); }
        }
        CHECK_EQ(lifecycle_counter::live_count, 0u);
    }

    TEST_CASE("mailbox formatter renders values and detailed state")
    {
        const scheduler_fixture fixture{};
        using mailbox_t = ::verilator_utils::mailbox<int>;
        static_assert(::std::formattable<mailbox_t, char>);

        mailbox_t bounded_mailbox{3};
        CHECK(bounded_mailbox.try_put(10));
        CHECK(bounded_mailbox.try_put(20));

        CHECK_EQ(::std::format("{}"sv, bounded_mailbox), "[10, 20]"sv);
        CHECK_EQ(::std::format("{:#}"sv, bounded_mailbox), "{max_count: 3, value: [10, 20]}"sv);
        CHECK_EQ(::doctest::StringMaker<mailbox_t>::convert(bounded_mailbox), "{max_count: 3, value: [10, 20]}");

        mailbox_t empty_mailbox{};
        CHECK_EQ(::std::format("{:#}"sv, empty_mailbox), "{max_count: 0, value: []}"sv);
    }

    TEST_CASE("mailbox formatter rejects unsupported format specifiers")
    {

        ::verilator_utils::mailbox<int> mailbox{};

        CHECK_THROWS_AS(static_cast<void>(::std::vformat("{:x}"sv, ::std::make_format_args(mailbox))), ::std::format_error);
    }

    TEST_CASE("shift_register delays values by the configured depth")
    {
        ::verilator_utils::shift_register<::std::uint64_t> delay_line{3, 8, ::verilator_utils::data_format::hex};

        CHECK_FALSE(delay_line.update(0x01u).has_value());
        CHECK_FALSE(delay_line.update(0x02u).has_value());
        CHECK_FALSE(delay_line.update(0x03u).has_value());

        auto first{delay_line.update(0x04u)};
        REQUIRE(first.has_value());
        CHECK_EQ(first->value(), 0x01u);
        CHECK_EQ(first->width(), 8u);
        CHECK(::std::holds_alternative<::verilator_utils::data_format::hex_t>(first->format()));
        CHECK_EQ(first->to_string(), "0x01"sv);

        auto second{delay_line.update(0x05u)};
        REQUIRE(second.has_value());
        CHECK_EQ(second->value(), 0x02u);
        CHECK_EQ(second->to_string(), "0x02"sv);

        auto third{delay_line.update(0x06u)};
        REQUIRE(third.has_value());
        CHECK_EQ(third->value(), 0x03u);
        CHECK_EQ(third->to_string(), "0x03"sv);
    }

    TEST_CASE("shift_register with depth one echoes the previous value")
    {
        ::verilator_utils::shift_register<::std::uint64_t> delay_line{1, 8, ::verilator_utils::data_format::hex};

        CHECK_FALSE(delay_line.update(0x11u).has_value());

        auto echoed{delay_line.update(0x22u)};
        REQUIRE(echoed.has_value());
        CHECK_EQ(echoed->value(), 0x11u);
        CHECK_EQ(echoed->to_string(), "0x11"sv);

        auto echoed_again{delay_line.update(0x33u)};
        REQUIRE(echoed_again.has_value());
        CHECK_EQ(echoed_again->value(), 0x22u);
    }

    TEST_CASE("shift_register reset clears the delayed values")
    {
        ::verilator_utils::shift_register<::std::uint64_t> delay_line{2, 4, ::verilator_utils::data_format::hex};

        delay_line.update(0x1u);
        delay_line.update(0x2u);
        delay_line.reset();

        CHECK_FALSE(delay_line.update(0x3u).has_value());
        CHECK_FALSE(delay_line.update(0x4u).has_value());
        auto delayed{delay_line.update(0x5u)};
        REQUIRE(delayed.has_value());
        CHECK_EQ(delayed->value(), 0x3u);
    }

    TEST_CASE("shift_register with enable false holds the chain and the output")
    {
        ::verilator_utils::shift_register<::std::uint64_t> delay_line{2, 8, ::verilator_utils::data_format::hex};

        CHECK_FALSE(delay_line.update(0x01u).has_value());
        CHECK_FALSE(delay_line.update(0x02u).has_value());

        // 链未满时，disable 的 update 不产生输出，也不进入寄存器链
        CHECK_FALSE(delay_line.update(0x03u, false).has_value());

        auto first{delay_line.update(0x04u)};
        REQUIRE(first.has_value());
        CHECK_EQ(first->value(), 0x01u);

        // disable 时输出保持上一个移出的值，寄存器链内容不变
        auto held_first{delay_line.update(0x05u, false)};
        REQUIRE(held_first.has_value());
        CHECK_EQ(held_first->value(), 0x01u);
        CHECK_EQ(held_first->width(), 8u);
        CHECK(::std::holds_alternative<::verilator_utils::data_format::hex_t>(held_first->format()));
        CHECK_EQ(held_first->to_string(), "0x01"sv);

        auto held_again{delay_line.update(0x06u, false)};
        REQUIRE(held_again.has_value());
        CHECK_EQ(held_again->value(), 0x01u);

        // 重新使能后，寄存器链未被 disable 期间的值污染
        auto second{delay_line.update(0x07u)};
        REQUIRE(second.has_value());
        CHECK_EQ(second->value(), 0x02u);
    }

    TEST_CASE("shift_register with depth zero outputs and holds the current value")
    {
        ::verilator_utils::shift_register<::std::uint64_t> delay_line{0, 8, ::verilator_utils::data_format::hex};

        auto first{delay_line.update(0x11u)};
        REQUIRE(first.has_value());
        CHECK_EQ(first->value(), 0x11u);

        // disable 时保持最近一次使能输入的值
        auto held{delay_line.update(0x22u, false)};
        REQUIRE(held.has_value());
        CHECK_EQ(held->value(), 0x11u);

        auto second{delay_line.update(0x33u)};
        REQUIRE(second.has_value());
        CHECK_EQ(second->value(), 0x33u);

        delay_line.reset();
        CHECK_FALSE(delay_line.update(0x44u, false).has_value());
        auto third{delay_line.update(0x55u)};
        REQUIRE(third.has_value());
        CHECK_EQ(third->value(), 0x55u);
    }

    TEST_CASE("shift_register reset clears the held output")
    {
        ::verilator_utils::shift_register<::std::uint64_t> delay_line{2, 8, ::verilator_utils::data_format::hex};

        delay_line.update(0x01u);
        delay_line.update(0x02u);
        auto first{delay_line.update(0x03u)};
        REQUIRE(first.has_value());
        CHECK_EQ(first->value(), 0x01u);

        delay_line.reset();

        // reset 后 disable 的 update 不再返回之前保持的输出
        CHECK_FALSE(delay_line.update(0x04u, false).has_value());
        CHECK_FALSE(delay_line.update(0x05u).has_value());
        CHECK_FALSE(delay_line.update(0x06u).has_value());
        auto delayed{delay_line.update(0x07u)};
        REQUIRE(delayed.has_value());
        CHECK_EQ(delayed->value(), 0x05u);
    }

    TEST_CASE("shift_register with enable supports Verilator wide data")
    {
        ::verilator_utils::shift_register<::VlWide<2>> delay_line{1, 48, ::verilator_utils::data_format::hex};

        const ::VlWide<2> first_value{0x89ab'cdefu, 0x0000'0123u};
        CHECK_FALSE(delay_line.update(first_value).has_value());

        // disable 的输入不进入寄存器链
        const ::VlWide<2> disabled_value{0xdead'beefu, 0x0000'0deau};
        CHECK_FALSE(delay_line.update(disabled_value, false).has_value());

        const ::VlWide<2> second_value{0x1122'3344u, 0x0000'0001u};
        auto delayed{delay_line.update(second_value)};
        REQUIRE(delayed.has_value());
        CHECK_EQ(delayed->value().at(0), 0x89ab'cdefu);
        CHECK_EQ(delayed->value().at(1), 0x0000'0123u);

        // disable 时输出保持上一个移出的宽数据
        auto held{delay_line.update(second_value, false)};
        REQUIRE(held.has_value());
        CHECK_EQ(held->value().at(0), 0x89ab'cdefu);
        CHECK_EQ(held->value().at(1), 0x0000'0123u);
        CHECK_EQ(held->to_string(), "0x012389abcdef"sv);
    }

    TEST_CASE("shift_register preserves the configured data format")
    {
        ::verilator_utils::shift_register<::std::uint64_t> binary_delay_line{
            2,
            {4, ::verilator_utils::data_format::bin}
        };
        binary_delay_line.update(0xbu);
        binary_delay_line.update(0xcu);
        auto binary_value{binary_delay_line.update(0xdu)};
        REQUIRE(binary_value.has_value());
        CHECK_EQ(binary_value->value(), 0xbu);
        CHECK_EQ(binary_value->width(), 4u);
        CHECK(::std::holds_alternative<::verilator_utils::data_format::bin_t>(binary_value->format()));
        CHECK_EQ(binary_value->to_string(), "0b1011"sv);

        ::verilator_utils::shift_register<::std::uint64_t> decimal_delay_line{2, 8, ::verilator_utils::data_format::dec_unsigned};
        decimal_delay_line.update(42u);
        decimal_delay_line.update(43u);
        auto decimal_value{decimal_delay_line.update(44u)};
        REQUIRE(decimal_value.has_value());
        CHECK_EQ(decimal_value->value(), 42u);
        CHECK(::std::holds_alternative<::verilator_utils::data_format::dec_unsigned_t>(decimal_value->format()));
        CHECK_EQ(decimal_value->to_string(), "42"sv);
    }

    TEST_CASE("shift_register supports Verilator wide data")
    {
        ::verilator_utils::shift_register<::VlWide<2>> delay_line{2, 48, ::verilator_utils::data_format::hex};

        const ::VlWide<2> first_value{0x89ab'cdefu, 0x0000'0123u};
        CHECK_FALSE(delay_line.update(first_value).has_value());

        const ::VlWide<2> second_value{0x1122'3344u, 0x0000'0001u};
        CHECK_FALSE(delay_line.update(second_value).has_value());

        const ::VlWide<2> third_value{0x5566'7788u, 0x0000'0002u};
        auto delayed{delay_line.update(third_value)};
        REQUIRE(delayed.has_value());
        CHECK_EQ(delayed->value().at(0), 0x89ab'cdefu);
        CHECK_EQ(delayed->value().at(1), 0x0000'0123u);
        CHECK_EQ(delayed->width(), 48u);
        CHECK(::std::holds_alternative<::verilator_utils::data_format::hex_t>(delayed->format()));
        CHECK_EQ(delayed->to_string(), "0x012389abcdef"sv);

        const ::VlWide<2> fourth_value{0xaabb'ccddu, 0x0000'0003u};
        auto delayed_again{delay_line.update(fourth_value)};
        REQUIRE(delayed_again.has_value());
        CHECK_EQ(delayed_again->value().at(0), 0x1122'3344u);
        CHECK_EQ(delayed_again->value().at(1), 0x0000'0001u);
    }

    TEST_CASE("shift_register formatter renders contents and detailed state")
    {

        using shift_register_t = ::verilator_utils::shift_register<::std::uint64_t>;
        static_assert(::std::formattable<shift_register_t, char>);

        shift_register_t delay_line{3, 8, ::verilator_utils::data_format::hex};
        delay_line.update(0x01u);
        delay_line.update(0x02u);
        delay_line.update(0x03u);

        CHECK_EQ(::std::format("{}"sv, delay_line), "[1, 2, 3]"sv);
        CHECK_EQ(::std::format("{:#}"sv, delay_line), "{depth: 3, reg: [1, 2, 3]}"sv);
        CHECK_EQ(::doctest::StringMaker<shift_register_t>::convert(delay_line), "{depth: 3, reg: [1, 2, 3]}");
    }

    TEST_CASE("shift_register formatter rejects unsupported format specifiers")
    {

        ::verilator_utils::shift_register<::std::uint64_t> delay_line{1, 8, ::verilator_utils::data_format::hex};

        CHECK_THROWS_AS(static_cast<void>(::std::vformat("{:x}"sv, ::std::make_format_args(delay_line))), ::std::format_error);
    }

    TEST_CASE("stacktrace_frame formatter renders coroutine role, function and location")
    {
        using frame_t = ::verilator_utils::coroutine_stacktrace::stacktrace_frame;
        static_assert(::std::formattable<frame_t, char>);

        const auto location{::std::source_location::current()};
        const frame_t frame{nullptr, location, ::verilator_utils::detail::promise_base::coroutine_type_enum::sub_coroutine};

        const auto plain{::std::format("{}"sv, frame)};
        CHECK(plain.contains("子协程"sv));
        CHECK(plain.contains(location.function_name()));
        CHECK(plain.contains(location.file_name()));
        CHECK(plain.ends_with(::std::format(":{}:{}"sv, location.line(), location.column())));

        const auto colored{::std::format("{:#}"sv, frame)};
        CHECK(colored.contains("\033[36m"sv));
        CHECK(colored.contains("\033[33m"sv));
        CHECK(colored.ends_with("\033[0m"sv));
        CHECK(colored.contains(location.function_name()));
        CHECK(colored.contains(location.file_name()));
    }

    TEST_CASE("stacktrace_frame formatter renders a default frame deterministically")
    {
        using frame_t = ::verilator_utils::coroutine_stacktrace::stacktrace_frame;

        const frame_t frame{};
        CHECK_EQ(::std::format("{}"sv, frame), "0x0(根协程):  at :0:0"sv);

        // doctest的StringMaker会根据全局颜色配置决定是否输出ANSI转义序列，因此只校验内容而非精确字符串
        const auto converted{::doctest::StringMaker<frame_t>::convert(frame)};
        CHECK_NE(converted.size(), 0u);
        CHECK(::std::string_view{converted.c_str()}.contains("根协程"sv));
    }

    TEST_CASE("stacktrace_frame formatter rejects unsupported format specifiers")
    {
        using frame_t = ::verilator_utils::coroutine_stacktrace::stacktrace_frame;

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
        CHECK_EQ(::std::format("{}"sv, frame.type), "根协程"sv);
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
        const auto root_handle{root_task.get_handle()};
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
        CHECK(rendered.starts_with("Coroutine Stacktrace:\n[0] "sv));
        CHECK(rendered.contains("根协程"sv));
        CHECK(rendered.contains("stacktrace_root_only"sv));
        CHECK(rendered.ends_with('\n'));
        CHECK_FALSE(rendered.contains("\033["sv));

        const auto colored{::std::format("{:#}"sv, *captured)};
        CHECK(colored.contains("\033[36m"sv));
        CHECK(colored.contains("\033[33m"sv));
        CHECK(colored.contains("stacktrace_root_only"sv));
    }

    TEST_CASE("coroutine_type_enum formatter rejects unsupported format specifiers")
    {
        const auto type{::verilator_utils::detail::promise_base::coroutine_type_enum::sub_coroutine};
        CHECK_EQ(::std::format("{}"sv, type), "子协程"sv);
        CHECK_EQ(::std::format("{}"sv, ::verilator_utils::detail::promise_base::coroutine_type_enum::async_coroutine),
                 "异步协程"sv);
        CHECK_THROWS_AS(static_cast<void>(::std::vformat("{:x}"sv, ::std::make_format_args(type))), ::std::format_error);
    }

    TEST_CASE("mailbox get and peek wait until a value is available")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::mailbox<int> mailbox{};
        const int* peeked{};
        int received{};

        auto consumer_task{[&](this auto) -> ::verilator_utils::task<void> {
            const int& reference{co_await mailbox.peek()};
            peeked = ::std::addressof(reference);
            received = co_await mailbox.get();
        }()};
        scheduler.add_task(::std::move(consumer_task));

        scheduler.loop_once();
        CHECK_FALSE(peeked);
        CHECK_EQ(mailbox.num(), 0u);

        bool put_succeeded{};
        put_succeeded = mailbox.try_put(17);
        scheduler.loop_once();
        CHECK(put_succeeded);

        CHECK(peeked);
        CHECK_EQ(received, 17);
        CHECK_EQ(mailbox.num(), 0u);
    }

    TEST_CASE("const mailbox peek operations preserve const reference identity")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::mailbox<int> mailbox{};
        const auto& const_mailbox{mailbox};
        int peeked{};

        // 阻塞式peek需要修改事件等待队列，仅支持非const邮箱；
        static_assert(::std::same_as<decltype(const_mailbox.try_peek()), ::std::optional<int>>);
        CHECK_FALSE(const_mailbox.try_peek().has_value());
        CHECK_EQ(const_mailbox.num(), 0u);

        bool put_succeeded{};
        put_succeeded = mailbox.try_put(29);
        CHECK(put_succeeded);

        // 通过非const阻塞式peek获取队首元素，用于验证元素的身份
        auto peeker_task{[&](this auto) -> ::verilator_utils::task<void> { peeked = co_await mailbox.peek(); }()};
        scheduler.add_task(::std::move(peeker_task));
        scheduler.loop_once();
        CHECK_EQ(peeked, 29);

        // const mailbox的try_peek返回同一元素，且不删除元素
        auto nonblocking_peek{const_mailbox.try_peek()};
        REQUIRE(nonblocking_peek.has_value());
        CHECK_EQ(*nonblocking_peek, 29);
        CHECK_EQ(peeked, *nonblocking_peek);
        CHECK_EQ(const_mailbox.num(), 1u);
    }

    TEST_CASE("bounded mailbox put waits for available capacity")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::mailbox<int> mailbox{1};
        bool producer_completed{};

        bool initial_put{};
        initial_put = mailbox.try_put(1);
        CHECK(initial_put);

        auto producer_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await mailbox.put(2);
            producer_completed = true;
        }()};
        scheduler.add_task(::std::move(producer_task));

        scheduler.loop_once();
        CHECK_FALSE(producer_completed);
        CHECK_EQ(mailbox.num(), 1u);

        ::std::optional<int> first{};
        first = mailbox.try_get();
        scheduler.loop_once();
        REQUIRE(first.has_value());
        CHECK_EQ(*first, 1);
        CHECK(producer_completed);
        CHECK_EQ(mailbox.num(), 1u);

        ::std::optional<int> second{};
        second = mailbox.try_get();
        scheduler.loop_once();
        REQUIRE(second.has_value());
        CHECK_EQ(*second, 2);
        CHECK_EQ(mailbox.num(), 0u);
    }

    TEST_CASE("semaphore nonblocking operations update the available count")
    {
        ::verilator_utils::semaphore semaphore{3};

        CHECK(semaphore.try_get(2));
        CHECK_FALSE(semaphore.try_get(2));
        CHECK(semaphore.try_get());
        CHECK_FALSE(semaphore.try_get());
        semaphore.put(4);
        CHECK(semaphore.try_get(4));
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore get waits for enough keys")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{1};
        bool acquired{};

        auto waiter_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await semaphore.get(2);
            acquired = true;
        }()};
        scheduler.add_task(::std::move(waiter_task));

        scheduler.loop_once();
        CHECK_FALSE(acquired);
        semaphore.put();
        scheduler.loop_once();

        CHECK(acquired);
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore grants blocked waiters in ticket order")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        ::std::vector<int> acquisition_order;

        const auto make_waiter{[&](this auto, int id) -> ::verilator_utils::task<void> {
            co_await semaphore.get();
            acquisition_order.push_back(id);
        }};
        auto first_task{make_waiter(1)};
        auto second_task{make_waiter(2)};
        scheduler.add_task(::std::move(first_task));
        scheduler.add_task(::std::move(second_task));

        scheduler.loop_once();
        CHECK(acquisition_order.empty());

        semaphore.put();
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, ::std::vector<int>{1});

        semaphore.put();
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, (::std::vector<int>{1, 2}));
    }

    TEST_CASE("semaphore get returns immediately when the count already suffices")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{3};
        bool acquired{};

        auto waiter_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await semaphore.get(2);
            acquired = true;
        }()};
        scheduler.add_task(::std::move(waiter_task));

        scheduler.loop_once();
        CHECK(acquired);
        CHECK(semaphore.try_get());
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore reserves permits for queued waiters before waking them")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        ::std::vector<int> acquisition_order;

        const auto make_waiter{[&](this auto, int id) -> ::verilator_utils::task<void> {
            co_await semaphore.get();
            acquisition_order.push_back(id);
        }};
        auto first_task{make_waiter(1)};
        auto second_task{make_waiter(2)};
        scheduler.add_task(::std::move(first_task));
        scheduler.add_task(::std::move(second_task));

        scheduler.loop_once();
        CHECK(acquisition_order.empty());

        // 放入的许可立即预留给队首等待者：外部观察者（如同伴协程）在等待者恢复前无法抢走，
        // 因此这里 try_get 必须失败（旧实现中许可尚未预留，try_get 会成功）
        semaphore.put();
        CHECK_FALSE(semaphore.try_get());
        CHECK(acquisition_order.empty());

        scheduler.loop_once();
        CHECK_EQ(acquisition_order, ::std::vector<int>{1});
        CHECK_FALSE(semaphore.try_get());

        // 下一个许可同样预留给新的队首等待者
        semaphore.put();
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, (::std::vector<int>{1, 2}));
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore grants multiple queued waiters from a single put in FIFO order")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        ::std::vector<int> acquisition_order;

        const auto make_waiter{[&](this auto, int id, ::std::size_t update) -> ::verilator_utils::task<void> {
            co_await semaphore.get(update);
            acquisition_order.push_back(id);
        }};
        auto first_task{make_waiter(1, 1)};
        auto second_task{make_waiter(2, 2)};
        auto third_task{make_waiter(3, 3)};
        scheduler.add_task(::std::move(first_task));
        scheduler.add_task(::std::move(second_task));
        scheduler.add_task(::std::move(third_task));

        scheduler.loop_once();
        CHECK(acquisition_order.empty());

        // 一次放入5个许可：按FIFO满足前两个等待者（1+2），剩余2个不足以满足第三个
        semaphore.put(5);
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, ::std::vector<int>{1, 2});
        // 严格FIFO：队列非空时，即使计数足以满足获取，try_get也必须失败
        CHECK_FALSE(semaphore.try_get(3));
        CHECK_FALSE(semaphore.try_get(2));

        // 补足最后一个等待者所需的许可
        semaphore.put(1);
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, (::std::vector<int>{1, 2, 3}));
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore keeps FIFO order when the head waiter needs more permits than are available")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        ::std::vector<int> acquisition_order;

        const auto make_waiter{[&](this auto, int id, ::std::size_t update) -> ::verilator_utils::task<void> {
            co_await semaphore.get(update);
            acquisition_order.push_back(id);
        }};
        auto first_task{make_waiter(1, 3)};
        auto second_task{make_waiter(2, 1)};
        scheduler.add_task(::std::move(first_task));
        scheduler.add_task(::std::move(second_task));

        scheduler.loop_once();
        CHECK(acquisition_order.empty());

        // 队首等待者需要3个许可：放入2个后仍不足，排在后面的等待者不能插队
        semaphore.put(2);
        scheduler.loop_once();
        CHECK(acquisition_order.empty());
        // 严格FIFO：等待队列非空时，任何非阻塞获取都不得拿走许可，即使计数足以满足
        CHECK_FALSE(semaphore.try_get(3));
        CHECK_FALSE(semaphore.try_get(2));

        // 补足队首等待者所需的许可（仅满足队首），后续等待者仍然等待
        semaphore.put(1);
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, ::std::vector<int>{1});
        CHECK_FALSE(semaphore.try_get());

        // 队首许可全部发放后，后续等待者才获得许可
        semaphore.put();
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, (::std::vector<int>{1, 2}));
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore get enqueues behind queued waiters even when the count suffices")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        ::std::vector<int> acquisition_order;

        const auto make_waiter{[&](this auto, int id, ::std::size_t update) -> ::verilator_utils::task<void> {
            co_await semaphore.get(update);
            acquisition_order.push_back(id);
        }};
        auto first_task{make_waiter(1, 5)};
        auto second_task{make_waiter(2, 1)};
        scheduler.add_task(::std::move(first_task));
        scheduler.add_task(::std::move(second_task));

        scheduler.loop_once();
        CHECK(acquisition_order.empty());

        // 队首需要5个许可：放入4个后队首仍不满足，此时新来的get(1)计数已足够，
        // 但严格FIFO要求它排到队尾而不是立即获得许可
        semaphore.put(4);
        scheduler.loop_once();
        CHECK(acquisition_order.empty());
        CHECK_FALSE(semaphore.try_get());

        // 补足队首的许可：队首获得许可后计数耗尽，排队的get(1)继续等待
        semaphore.put(1);
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, ::std::vector<int>{1});
        CHECK_FALSE(semaphore.try_get());

        // 新的许可到达后才轮到排队的get(1)
        semaphore.put();
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, (::std::vector<int>{1, 2}));
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore restores nonblocking acquisition once the queue drains")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};

        bool acquired{};
        auto waiter_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await semaphore.get();
            acquired = true;
        }()};
        scheduler.add_task(::std::move(waiter_task));

        scheduler.loop_once();
        CHECK_FALSE(acquired);

        // 放入2个许可：1个发放给队首等待者，剩余1个保留在计数中，
        // 队列已逻辑清空（物理条目要等到回收水位才被擦除），非阻塞获取立即恢复
        semaphore.put(2);
        CHECK(semaphore.try_get());
        CHECK_FALSE(semaphore.try_get());
        scheduler.loop_once();
        CHECK(acquired);

        // 队列清空后，计数充足的get走快速路径，一次调度即完成
        bool fast_acquired{};
        auto fast_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await semaphore.get();
            fast_acquired = true;
        }()};
        semaphore.put();
        scheduler.add_task(::std::move(fast_task));
        scheduler.loop_once();
        CHECK(fast_acquired);
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore waiters accumulate permits across multiple puts")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        bool acquired{};

        auto waiter_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await semaphore.get(5);
            acquired = true;
        }()};
        scheduler.add_task(::std::move(waiter_task));

        scheduler.loop_once();
        CHECK_FALSE(acquired);

        // 放入0个许可不得发放任何等待者
        semaphore.put(0);
        scheduler.loop_once();
        CHECK_FALSE(acquired);

        semaphore.put(2);
        scheduler.loop_once();
        CHECK_FALSE(acquired);

        semaphore.put(2);
        scheduler.loop_once();
        CHECK_FALSE(acquired);

        // 累计许可达到请求量后一次性发放
        semaphore.put();
        scheduler.loop_once();
        CHECK(acquired);
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore grants a large queue of waiters without losing or duplicating permits")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        // 超过semaphore内部suspend_queue的回收水位线，强制触发前缀回收路径
        constexpr static ::std::size_t waiter_count{4096zu / sizeof(::std::size_t) + 8zu};
        constexpr static auto iota_range{::std::views::iota(0zu, waiter_count)};
        ::std::vector<::std::size_t> acquisition_order;
        acquisition_order.reserve(waiter_count);

        const auto waiter{[&](this auto, ::std::size_t id) -> ::verilator_utils::task<void> {
            co_await semaphore.get();
            acquisition_order.push_back(id);
        }};

        for(const auto i: iota_range) { scheduler.add_task(waiter(i)); }

        scheduler.loop_once();
        CHECK_EQ(acquisition_order.size(), 0u);

        // 一次发放所有许可，跨过回收边界后仍按原FIFO顺序完成
        semaphore.put(waiter_count);
        scheduler.loop_once();

        REQUIRE_EQ(acquisition_order.size(), waiter_count);
        const auto expected{iota_range | ::std::ranges::to<::std::vector>()};
        CHECK_EQ(acquisition_order, expected);
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore reclaims queue storage for very large waiter queues")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        // 等待者数量足够多，使前缀回收后的空闲容量达到收缩阈值，触发shrink_to_fit
        constexpr static ::std::size_t waiter_count{6000};
        ::std::size_t completed{};

        const auto waiter{[&](this auto, ::std::size_t update) -> ::verilator_utils::task<void> {
            co_await semaphore.get(update);
            ++completed;
        }};

        scheduler.add_task(waiter(2));
        for(::std::size_t i{1}; i != waiter_count; ++i) { scheduler.add_task(waiter(1)); }

        scheduler.loop_once();
        CHECK_EQ(completed, 0u);

        // 总需求量恰好为waiter_count+1，发放后不留余量，验证计数守恒
        semaphore.put(waiter_count + 1);
        scheduler.loop_once();

        CHECK_EQ(completed, waiter_count);
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("mailbox put rechecks capacity when a peer producer fills the freed slot")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::mailbox<int> mailbox{2};
        ::std::size_t completed_count{};
        ::std::size_t max_observed_size{};
        bool capacity_violation{};

        // 填满邮箱，使所有生产者都阻塞
        bool initial_put1{};
        bool initial_put2{};
        initial_put1 = mailbox.try_put(97);
        initial_put2 = mailbox.try_put(98);
        REQUIRE(initial_put1);
        REQUIRE(initial_put2);

        const auto make_producer{[&](this auto, int value) -> ::verilator_utils::task<void> {
            co_await mailbox.put(value);
            ++completed_count;
            max_observed_size = ::std::max(max_observed_size, mailbox.num());
            capacity_violation = capacity_violation || mailbox.num() > 2;
        }};
        auto first_task{make_producer(10)};
        auto second_task{make_producer(20)};
        auto third_task{make_producer(30)};
        scheduler.add_task(::std::move(first_task));
        scheduler.add_task(::std::move(second_task));
        scheduler.add_task(::std::move(third_task));

        scheduler.loop_once();
        CHECK_EQ(completed_count, 0u);
        CHECK_EQ(mailbox.num(), 2u);

        // 消费一个元素释放一个空位：阻塞的生产者被唤醒并放入，其余继续等待
        ::std::optional<int> first_item{};
        first_item = mailbox.try_get();
        scheduler.loop_once();
        REQUIRE(first_item.has_value());
        CHECK_EQ(*first_item, 97);
        CHECK_EQ(completed_count, 1u);
        CHECK_EQ(mailbox.num(), 2u);

        // 再次释放空位：又一个生产者放入，其余继续等待
        ::std::optional<int> second_item{};
        second_item = mailbox.try_get();
        scheduler.loop_once();
        REQUIRE(second_item.has_value());
        CHECK_EQ(*second_item, 98);
        CHECK_EQ(completed_count, 2u);
        CHECK_EQ(mailbox.num(), 2u);

        // 最后一个生产者最终放入
        ::std::optional<int> third_item{};
        third_item = mailbox.try_get();
        scheduler.loop_once();
        REQUIRE(third_item.has_value());
        CHECK_EQ(completed_count, 3u);
        CHECK_EQ(mailbox.num(), 2u);

        // 所有元素均按FIFO顺序取出，容量从未被突破
        ::std::vector<int> drained{*first_item, *second_item, *third_item};
        while(auto item{mailbox.try_get()}) { drained.push_back(*item); }
        ::std::ranges::sort(drained);
        CHECK_EQ(drained, (::std::vector<int>{10, 20, 30, 97, 98}));
        CHECK_FALSE(capacity_violation);
        CHECK_LE(max_observed_size, 2u);
    }

    TEST_CASE("mailbox get rechecks emptiness when a peer consumer takes the only item")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::mailbox<int> mailbox{};
        int first_received{};
        int second_received{};

        const auto make_consumer{
            [&](this auto, int& received) -> ::verilator_utils::task<void> { received = co_await mailbox.get(); }};
        auto first_task{make_consumer(first_received)};
        auto second_task{make_consumer(second_received)};
        scheduler.add_task(::std::move(first_task));
        scheduler.add_task(::std::move(second_task));

        scheduler.loop_once();
        CHECK_EQ(first_received, 0);
        CHECK_EQ(second_received, 0);

        // 只放入一个元素：仅一个消费者被唤醒并取出，
        // 另一个消费者必须重新检查空态而不是对空邮箱取值
        bool put1{};
        put1 = mailbox.try_put(5);
        scheduler.loop_once();
        REQUIRE(put1);
        CHECK_EQ(first_received, 5);
        CHECK_EQ(second_received, 0);
        CHECK_EQ(mailbox.num(), 0u);

        // 第二个消费者重新等待后获得新元素
        bool put2{};
        put2 = mailbox.try_put(6);
        scheduler.loop_once();
        REQUIRE(put2);
        CHECK_EQ(second_received, 6);
        CHECK_EQ(mailbox.num(), 0u);
    }

    TEST_CASE("mailbox peek rechecks emptiness when a consumer removes the only item")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::mailbox<int> mailbox{};
        int received{};
        int peeked{};

        auto getter_task{[&](this auto) -> ::verilator_utils::task<void> { received = co_await mailbox.get(); }()};
        auto peeker_task{[&](this auto) -> ::verilator_utils::task<void> { peeked = co_await mailbox.peek(); }()};
        scheduler.add_task(::std::move(getter_task));
        scheduler.add_task(::std::move(peeker_task));

        scheduler.loop_once();
        CHECK_EQ(received, 0);
        CHECK_EQ(peeked, 0);

        // 只放入一个元素：getter取出后邮箱变空，peeker必须重新等待
        bool put1{};
        put1 = mailbox.try_put(7);
        scheduler.loop_once();
        REQUIRE(put1);
        CHECK_EQ(received, 7);
        CHECK_EQ(peeked, 0);
        CHECK_EQ(mailbox.num(), 0u);

        // peeker重新等待后观察到新元素，且不删除它
        bool put2{};
        put2 = mailbox.try_put(8);
        scheduler.loop_once();
        REQUIRE(put2);
        CHECK_EQ(peeked, 8);
        CHECK_EQ(mailbox.num(), 1u);

        ::std::optional<int> item{};
        item = mailbox.try_get();
        REQUIRE(item.has_value());
        CHECK_EQ(*item, 8);
        CHECK_EQ(mailbox.num(), 0u);
    }

    TEST_CASE("concurrent mailbox producers and consumers converge without exceeding capacity")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::mailbox<int> mailbox{3};
        constexpr static ::std::size_t producer_count{4};
        constexpr static ::std::size_t consumer_count{4};
        constexpr static ::std::size_t items_per_task{5};
        ::std::vector<int> received;
        ::std::size_t max_observed_size{};
        bool capacity_violation{};

        const auto producer{[&](this auto, int id) -> ::verilator_utils::task<void> {
            for(::std::size_t i{}; i != items_per_task; ++i)
            {
                co_await mailbox.put((id * 100 + static_cast<int>(i)));
                max_observed_size = ::std::max(max_observed_size, mailbox.num());
                capacity_violation = capacity_violation || mailbox.num() > 3;
            }
        }};
        const auto consumer{[&](this auto) -> ::verilator_utils::task<void> {
            for(::std::size_t i{}; i != items_per_task; ++i) { received.push_back(co_await mailbox.get()); }
        }};

        for(::std::size_t i{}; i != producer_count; ++i) { scheduler.add_task(producer(static_cast<int>(i))); }
        for(::std::size_t i{}; i != consumer_count; ++i) { scheduler.add_task(consumer()); }

        scheduler.loop_until_finish();

        CHECK_FALSE(capacity_violation);
        CHECK_LE(max_observed_size, 3u);
        CHECK_EQ(mailbox.num(), 0u);

        ::std::vector<int> expected;
        expected.reserve(producer_count * items_per_task);
        for(::std::size_t id{}; id != producer_count; ++id)
        {
            for(::std::size_t i{}; i != items_per_task; ++i) { expected.push_back(static_cast<int>(id * 100 + i)); }
        }
        ::std::ranges::sort(received);
        CHECK_EQ(received, expected);
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
            for(::std::size_t i{}; i != 3; ++i)
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
            for(::std::size_t i{}; i != 2; ++i)
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

    // --- 异步任务与任务池 (async_task / spawn_pool) ---

    TEST_CASE("async_task created inside a coroutine resumes its parent and propagates exceptions")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool completed{};
        bool caught{};

        const auto successful_child{[] -> ::verilator_utils::task<void> { co_await ::verilator_utils::wait_time(1_ps); }};
        const auto failing_child{
            [] -> ::verilator_utils::task<void> {
                co_await ::verilator_utils::wait_time(1_ps);
                throw ::std::runtime_error{"async child failure"};
            },
        };

        const auto& parent{
            [&] -> ::verilator_utils::task<void> {
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
                    caught = exception.what() == "async child failure"sv;
                }
            },
        };

        scheduler.add_task(parent());
        scheduler.loop_until_finish();
        CHECK(completed);
        CHECK(caught);
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
                CHECK_EQ(child.get_promise().is_async, true);

                // 让出执行权，使异步子任务运行到完成
                co_await ::verilator_utils::wait_time(1_ps);
                CHECK(child.done());
                CHECK_EQ(child.get_promise().status, ::verilator_utils::task<void>::status_enum::finished);

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
                CHECK_THROWS_WITH_AS(static_cast<void>(child.get_promise()),
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
                CHECK_THROWS_WITH_AS(static_cast<void>(owner.get_promise()),
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

    TEST_CASE("canceling an async task before it starts skips its coroutine body")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool child_body_ran{};
        bool child_awaited{};

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
                CHECK_EQ(child.get_promise().status, ::verilator_utils::task<void>::status_enum::cancel_requested);
                CHECK_FALSE(child.cancel_possible());

                // 异步任务的取消不通过异常传播：等待被取消的任务不会抛出异常
                co_await child;
                child_awaited = true;
            },
        };

        scheduler.add_task(parent());
        CHECK_NOTHROW(scheduler.loop_until_finish());
        // 取消请求在恢复点生效：协程体一次都不会运行
        CHECK_FALSE(child_body_ran);
        CHECK(child_awaited);
    }

    TEST_CASE("canceling a suspended async task drops the remainder of its body")
    {
        ::std::size_t destroyed{};
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        bool child_started{};
        bool resumed_after_wait{};
        bool child_awaited{};

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
                CHECK_EQ(child.get_promise().status, ::verilator_utils::task<void>::status_enum::suspended);
                CHECK(child.cancel_possible());

                child.cancel();
                CHECK(child.cancel_requested());
                CHECK_EQ(child.get_promise().status, ::verilator_utils::task<void>::status_enum::cancel_requested);
                // 已收到取消请求的任务不在可取消状态，重复请求触发断言
                CHECK_FALSE(child.cancel_possible());
                CHECK_THROWS_WITH_AS(child.cancel(), ::doctest::Contains{"不可取消"}, ::verilator_utils::assertion_error);

                // 取消不产生未处理异常，等待被取消的任务同样不会抛出异常
                co_await child;
                child_awaited = true;
            },
        };

        scheduler.add_task(parent());
        CHECK_NOTHROW(scheduler.loop_until_finish());
        // 取消请求在恢复点生效：子任务不再继续执行
        CHECK(child_started);
        CHECK_FALSE(resumed_after_wait);
        CHECK(child_awaited);
        // 被取消的异步任务在等待完成后回收协程帧
        CHECK_EQ(destroyed, 1zu);
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
            scheduler.loop_until_finish();
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
                    co_await ::verilator_utils::get_handle<::verilator_utils::task<void>::promise_type>()};
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
        const auto& promise{parent_task.get_promise()};
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
                auto pool{co_await ::verilator_utils::get_spawn_pool()};
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
                auto pool{co_await ::verilator_utils::get_spawn_pool()};
                pool.add_task(successful_child());
                pool.add_task(first_failing_child());
                pool.add_task(second_failing_child());
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
        CHECK_EQ(descriptions, (::std::vector<::std::string>{"runtime_error: first failure", "logic_error: second failure"}));
        // what()按"序号: 消息"逐行列出所有异常
        CHECK_EQ(message, "1: first failure\n2: second failure\n"sv);
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
                auto pool{co_await ::verilator_utils::get_spawn_pool()};
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
                auto pool{co_await ::verilator_utils::get_spawn_pool()};
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
        CHECK_EQ(descriptions, (::std::vector<::std::string>{"non-standard"}));
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

    TEST_CASE("scheduler reclaims a pool of subtasks suspended on a never-fired event")
    {
        ::std::size_t destroyed{};
        {
            scheduler_fixture fixture{};
            auto scheduler{fixture.make_scheduler()};
            const auto parent_lambda{[&] -> ::verilator_utils::task<void> {
                const frame_destruction_counter pool_counter{&destroyed};
                auto pool{co_await ::verilator_utils::get_spawn_pool()};
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
                auto pool{co_await ::verilator_utils::get_spawn_pool()};
                pool.add_task(just_wait());
                pool.add_task(just_wait());
                co_await pool.join_none();
                co_await ::verilator_utils::wait_time(1_ns);
            },
        };
        // join_all：父协程等待所有子任务完成
        const auto join_all_parent{
            [&] -> ::verilator_utils::task<void> {
                auto pool{co_await ::verilator_utils::get_spawn_pool()};
                pool.add_task(just_wait());
                pool.add_task(just_wait());
                co_await pool.join_all();
            },
        };
        // join_any：父协程等待任一子任务完成
        const auto join_any_parent{
            [&] -> ::verilator_utils::task<void> {
                auto pool{co_await ::verilator_utils::get_spawn_pool()};
                pool.add_task(just_wait());
                pool.add_task(just_wait());
                co_await pool.join_any();
            },
        };
        // 不join：父协程析构任务池，子任务被分离给调度器
        const auto detached_parent{
            [&] -> ::verilator_utils::task<void> {
                auto pool{co_await ::verilator_utils::get_spawn_pool()};
                pool.add_task(just_wait());
                pool.add_task(just_wait());
                co_await ::verilator_utils::wait_time(1_ns);
            },
        };
        // 子任务挂起在永不触发的事件上，由调度器通过挂起队列兜底回收
        const auto event_parent{
            [&] -> ::verilator_utils::task<void> {
                auto pool{co_await ::verilator_utils::get_spawn_pool()};
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

    TEST_CASE("event suspends waiters until notify_all wakes every waiter in order")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::event event{};
        ::std::vector<int> wake_order;

        const auto make_waiter{[&](this auto, int id) -> ::verilator_utils::task<void> {
            co_await event;
            wake_order.push_back(id);
        }};
        auto first_task{make_waiter(1)};
        auto second_task{make_waiter(2)};
        auto third_task{make_waiter(3)};
        scheduler.add_task(::std::move(first_task));
        scheduler.add_task(::std::move(second_task));
        scheduler.add_task(::std::move(third_task));

        scheduler.loop_once();
        CHECK(wake_order.empty());

        // notify_all 为同步调用，直接唤醒所有等待者
        event.notify_all();
        scheduler.loop_once();
        CHECK_EQ(wake_order, (::std::vector<int>{1, 2, 3}));
    }

    TEST_CASE("event notify_one wakes the oldest waiter and preserves FIFO order")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::event event{};
        ::std::vector<int> wake_order;

        const auto make_waiter{[&](this auto, int id) -> ::verilator_utils::task<void> {
            co_await event;
            wake_order.push_back(id);
        }};
        auto first_task{make_waiter(1)};
        auto second_task{make_waiter(2)};
        auto third_task{make_waiter(3)};
        scheduler.add_task(::std::move(first_task));
        scheduler.add_task(::std::move(second_task));
        scheduler.add_task(::std::move(third_task));

        scheduler.loop_once();
        CHECK(wake_order.empty());

        event.notify_one();
        scheduler.loop_once();
        CHECK_EQ(wake_order, ::std::vector<int>{1});

        event.notify_one();
        scheduler.loop_once();
        CHECK_EQ(wake_order, (::std::vector<int>{1, 2}));

        event.notify_one();
        scheduler.loop_once();
        CHECK_EQ(wake_order, (::std::vector<int>{1, 2, 3}));
    }

    TEST_CASE("event is edge-triggered and does not latch missed notifications")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::event event{};

        // 在没有任何等待者时通知：通知被丢弃，随后到达的等待者仍会挂起
        event.notify_all();

        bool woke{};
        auto waiter_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await event;
            woke = true;
        }()};
        scheduler.add_task(::std::move(waiter_task));

        scheduler.loop_once();
        CHECK_FALSE(woke);

        // 再次通知后等待者才被唤醒
        event.notify_all();
        scheduler.loop_once();
        CHECK(woke);
    }

    TEST_CASE("event notify_one on an empty queue is a safe no-op")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::event event{};

        // 在没有任何等待者时通知单个协程：不崩溃、不做任何事，随后到达的等待者仍会挂起
        event.notify_one();

        bool woke{};
        auto waiter_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await event;
            woke = true;
        }()};
        scheduler.add_task(::std::move(waiter_task));
        scheduler.loop_once();
        CHECK_FALSE(woke);

        // 再次通知后等待者才被唤醒
        event.notify_one();
        scheduler.loop_once();
        CHECK(woke);
    }

    TEST_CASE("event notify_all on an empty queue is a safe no-op")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::event event{};

        // 在没有任何等待者时通知所有协程：不崩溃、不做任何事，随后到达的等待者仍会挂起
        event.notify_all();

        bool woke{};
        auto waiter_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await event;
            woke = true;
        }()};
        scheduler.add_task(::std::move(waiter_task));
        scheduler.loop_once();
        CHECK_FALSE(woke);

        // 再次通知后等待者才被唤醒
        event.notify_all();
        scheduler.loop_once();
        CHECK(woke);
    }

    TEST_CASE("event can be awaited again after each notification")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::event event{};
        ::std::size_t wake_count{};

        auto waiter_task{[&](this auto) -> ::verilator_utils::task<void> {
            for(::std::size_t i{}; i != 3; ++i)
            {
                co_await event;
                ++wake_count;
            }
        }()};
        scheduler.add_task(::std::move(waiter_task));

        scheduler.loop_once();
        CHECK_EQ(wake_count, 0u);

        for(::std::size_t i{1}; i != 4; ++i)
        {
            event.notify_all();
            scheduler.loop_once();
            CHECK_EQ(wake_count, i);
        }
    }

    TEST_CASE("event handshake between producer and consumer converges")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        constexpr static ::std::size_t item_count{5};
        ::verilator_utils::event data_ready{};
        ::verilator_utils::event data_consumed{};
        ::std::vector<int> received;

        // 消费者先等待数据，生产者发送数据后等待消费者确认
        const auto consumer{[&](this auto) -> ::verilator_utils::task<void> {
            for(::std::size_t i{}; i != item_count; ++i)
            {
                co_await data_ready;
                received.push_back(static_cast<int>(i));
                data_consumed.notify_all();
            }
        }};
        const auto producer{[&](this auto) -> ::verilator_utils::task<void> {
            for(::std::size_t i{}; i != item_count; ++i)
            {
                data_ready.notify_all();
                co_await data_consumed;
            }
        }};

        auto consumer_task{consumer()};
        scheduler.add_task(::std::move(consumer_task));

        // 消费者先就绪等待数据
        scheduler.loop_once();
        CHECK(received.empty());

        auto producer_task{producer()};
        scheduler.add_task(::std::move(producer_task));
        scheduler.loop_until_finish();

        ::std::vector<int> expected;
        expected.reserve(item_count);
        for(::std::size_t i{}; i != item_count; ++i) { expected.push_back(static_cast<int>(i)); }
        CHECK_EQ(received, expected);
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
                        verify_stage = scheduler.get_eval_stage();
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

    // NOLINTEND(bugprone-unchecked-optional-access)
}
