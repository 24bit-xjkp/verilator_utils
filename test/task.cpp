#include <doctest_macros.hpp>
import verilator_utils.full;

namespace
{
    using namespace ::verilator_utils::verilator;
    using namespace ::verilator_utils::literals;

    auto to_vector(::std::size_t n) noexcept { return ::std::views::take(n) | ::std::ranges::to<::std::vector<bool>>(); }

    struct signal_state
    { ::CData value{}; };

    /// 带静态存活计数的move-only类型，用于验证mailbox对元素的析构平衡
    struct lifecycle_counter
    {
        inline static ::std::size_t live_count{};
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

        scheduler_fixture()
        {
            context.timeunit(-9);
            context.timeprecision(-12);
        }

        [[nodiscard]] ::verilator_utils::eval_scheduler make_scheduler() noexcept
        { return ::verilator_utils::eval_scheduler{dut}; }
    };

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
        auto fibonacci{::verilator_utils::fibonacci_lfsr_generator(3)};
        auto fibonacci_iter{fibonacci.begin()};
        for(const bool expected: {true, false, false, true, true, true, false, true})
        {
            REQUIRE_NE(fibonacci_iter, fibonacci.end());
            CHECK_EQ(*fibonacci_iter, expected);
            ++fibonacci_iter;
        }

        auto galois{::verilator_utils::galois_lfsr_generator(3)};
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
        scheduler_fixture fixture{};
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
        scheduler_fixture fixture{};
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
        scheduler_fixture fixture{};
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

        ::VlWide<2> first_value{0x89ab'cdefu, 0x0000'0123u};
        CHECK_FALSE(delay_line.update(first_value).has_value());

        // disable 的输入不进入寄存器链
        ::VlWide<2> disabled_value{0xdead'beefu, 0x0000'0deau};
        CHECK_FALSE(delay_line.update(disabled_value, false).has_value());

        ::VlWide<2> second_value{0x1122'3344u, 0x0000'0001u};
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

        ::VlWide<2> first_value{0x89ab'cdefu, 0x0000'0123u};
        CHECK_FALSE(delay_line.update(first_value).has_value());

        ::VlWide<2> second_value{0x1122'3344u, 0x0000'0001u};
        CHECK_FALSE(delay_line.update(second_value).has_value());

        ::VlWide<2> third_value{0x5566'7788u, 0x0000'0002u};
        auto delayed{delay_line.update(third_value)};
        REQUIRE(delayed.has_value());
        CHECK_EQ(delayed->value().at(0), 0x89ab'cdefu);
        CHECK_EQ(delayed->value().at(1), 0x0000'0123u);
        CHECK_EQ(delayed->width(), 48u);
        CHECK(::std::holds_alternative<::verilator_utils::data_format::hex_t>(delayed->format()));
        CHECK_EQ(delayed->to_string(), "0x012389abcdef"sv);

        ::VlWide<2> fourth_value{0xaabb'ccddu, 0x0000'0003u};
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
        CHECK(plain.contains("sub_coroutine"sv));
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
        CHECK_EQ(::std::format("{}"sv, frame), "0x0(root_coroutine):  at :0:0"sv);

        // doctest的StringMaker会根据全局颜色配置决定是否输出ANSI转义序列，因此只校验内容而非精确字符串
        const auto converted{::doctest::StringMaker<frame_t>::convert(frame)};
        CHECK_NE(converted.size(), 0u);
        CHECK(::std::string_view{converted.c_str()}.contains("root_coroutine"sv));
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
        CHECK_EQ(::std::format("{}"sv, frame.type), "root_coroutine"sv);
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
        auto root_handle{root_task.get_handle()};
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
            auto&& scheduler_ref{co_await ::verilator_utils::get_scheduler()};
            ::verilator_utils::async_task child{scheduler_ref,
                                                stacktrace_async_child(captured, async_child_handle, expected_line)};
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

        ::verilator_utils::async_task child{scheduler, stacktrace_orphan_async(captured, expected_line)};
        scheduler.loop_until_finish();

        REQUIRE(captured);
        REQUIRE_EQ(captured->frames.size(), 1u);
        // 未被等待的异步协程没有父协程，单独构成一帧
        CHECK_EQ(captured->frames[0].type, ::verilator_utils::detail::promise_base::coroutine_type_enum::async_coroutine);
        CHECK_EQ(static_cast<int>(captured->frames[0].location.line()), expected_line);
        CHECK(::std::string_view{captured->frames[0].location.function_name()}.contains("stacktrace_orphan_async"sv));
        CHECK(child.done());
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
        CHECK(rendered.contains("root_coroutine"sv));
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
        CHECK_EQ(::std::format("{}"sv, type), "sub_coroutine"sv);
        CHECK_EQ(::std::format("{}"sv, ::verilator_utils::detail::promise_base::coroutine_type_enum::async_coroutine),
                 "async_coroutine"sv);
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
        ::verilator_utils::async_task consumer{scheduler, ::std::move(consumer_task)};

        scheduler.loop_once();
        CHECK_FALSE(consumer.done());
        CHECK_EQ(mailbox.num(), 0u);

        bool put_succeeded{};
        put_succeeded = mailbox.try_put(17);
        scheduler.loop_once();
        CHECK(put_succeeded);

        CHECK(consumer.done());
        CHECK(peeked);
        CHECK_EQ(received, 17);
        CHECK_EQ(mailbox.num(), 0u);
        consumer.get_promise().rethrow_exception();
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
        ::verilator_utils::async_task peeker{scheduler, ::std::move(peeker_task)};
        scheduler.loop_once();
        CHECK(peeker.done());

        // const mailbox的try_peek返回同一元素，且不删除元素
        auto nonblocking_peek{const_mailbox.try_peek()};
        REQUIRE(nonblocking_peek.has_value());
        CHECK_EQ(*nonblocking_peek, 29);
        CHECK_EQ(peeked, *nonblocking_peek);
        CHECK_EQ(const_mailbox.num(), 1u);
        peeker.get_promise().rethrow_exception();
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
        ::verilator_utils::async_task producer{scheduler, ::std::move(producer_task)};

        scheduler.loop_once();
        CHECK_FALSE(producer.done());
        CHECK_FALSE(producer_completed);
        CHECK_EQ(mailbox.num(), 1u);

        ::std::optional<int> first{};
        first = mailbox.try_get();
        scheduler.loop_once();
        REQUIRE(first.has_value());
        CHECK_EQ(*first, 1);
        CHECK(producer.done());
        CHECK(producer_completed);
        CHECK_EQ(mailbox.num(), 1u);

        ::std::optional<int> second{};
        second = mailbox.try_get();
        scheduler.loop_once();
        REQUIRE(second.has_value());
        CHECK_EQ(*second, 2);
        CHECK_EQ(mailbox.num(), 0u);
        producer.get_promise().rethrow_exception();
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
        ::verilator_utils::async_task waiter{scheduler, ::std::move(waiter_task)};

        scheduler.loop_once();
        CHECK_FALSE(waiter.done());
        CHECK_FALSE(acquired);
        semaphore.put();
        scheduler.loop_once();

        CHECK(waiter.done());
        CHECK(acquired);
        CHECK_FALSE(semaphore.try_get());
        waiter.get_promise().rethrow_exception();
    }

    TEST_CASE("semaphore grants blocked waiters in ticket order")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        ::std::vector<int> acquisition_order;

        auto make_waiter{[&](this auto, int id) -> ::verilator_utils::task<void> {
            co_await semaphore.get();
            acquisition_order.push_back(id);
        }};
        auto first_task{make_waiter(1)};
        auto second_task{make_waiter(2)};
        ::verilator_utils::async_task first{scheduler, ::std::move(first_task)};
        ::verilator_utils::async_task second{scheduler, ::std::move(second_task)};

        scheduler.loop_once();
        CHECK_FALSE(first.done());
        CHECK_FALSE(second.done());

        semaphore.put();
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, ::std::vector<int>{1});
        CHECK(first.done());
        CHECK_FALSE(second.done());

        semaphore.put();
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, (::std::vector<int>{1, 2}));
        CHECK(second.done());
        first.get_promise().rethrow_exception();
        second.get_promise().rethrow_exception();
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

        auto make_producer{[&](this auto, int value) -> ::verilator_utils::task<void> {
            co_await mailbox.put(value);
            ++completed_count;
            max_observed_size = ::std::max(max_observed_size, mailbox.num());
            capacity_violation = capacity_violation || mailbox.num() > 2;
        }};
        auto first_task{make_producer(10)};
        auto second_task{make_producer(20)};
        auto third_task{make_producer(30)};
        ::verilator_utils::async_task first{scheduler, ::std::move(first_task)};
        ::verilator_utils::async_task second{scheduler, ::std::move(second_task)};
        ::verilator_utils::async_task third{scheduler, ::std::move(third_task)};

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
        first.get_promise().rethrow_exception();
        second.get_promise().rethrow_exception();
        third.get_promise().rethrow_exception();
    }

    TEST_CASE("mailbox get rechecks emptiness when a peer consumer takes the only item")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::mailbox<int> mailbox{};
        int first_received{};
        int second_received{};

        auto make_consumer{[&](this auto, int& received) -> ::verilator_utils::task<void> { received = co_await mailbox.get(); }};
        auto first_task{make_consumer(first_received)};
        auto second_task{make_consumer(second_received)};
        ::verilator_utils::async_task first{scheduler, ::std::move(first_task)};
        ::verilator_utils::async_task second{scheduler, ::std::move(second_task)};

        scheduler.loop_once();
        CHECK_FALSE(first.done());
        CHECK_FALSE(second.done());

        // 只放入一个元素：仅一个消费者被唤醒并取出，
        // 另一个消费者必须重新检查空态而不是对空邮箱取值
        bool put1{};
        put1 = mailbox.try_put(5);
        scheduler.loop_once();
        REQUIRE(put1);
        CHECK(first.done());
        CHECK_FALSE(second.done());
        CHECK_EQ(first_received, 5);
        CHECK_EQ(mailbox.num(), 0u);

        // 第二个消费者重新等待后获得新元素
        bool put2{};
        put2 = mailbox.try_put(6);
        scheduler.loop_once();
        REQUIRE(put2);
        CHECK(second.done());
        CHECK_EQ(second_received, 6);
        CHECK_EQ(mailbox.num(), 0u);
        first.get_promise().rethrow_exception();
        second.get_promise().rethrow_exception();
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
        ::verilator_utils::async_task getter{scheduler, ::std::move(getter_task)};
        ::verilator_utils::async_task peeker{scheduler, ::std::move(peeker_task)};

        scheduler.loop_once();
        CHECK_FALSE(getter.done());
        CHECK_FALSE(peeker.done());

        // 只放入一个元素：getter取出后邮箱变空，peeker必须重新等待
        bool put1{};
        put1 = mailbox.try_put(7);
        scheduler.loop_once();
        REQUIRE(put1);
        CHECK(getter.done());
        CHECK_FALSE(peeker.done());
        CHECK_EQ(received, 7);
        CHECK_EQ(mailbox.num(), 0u);

        // peeker重新等待后观察到新元素，且不删除它
        bool put2{};
        put2 = mailbox.try_put(8);
        scheduler.loop_once();
        REQUIRE(put2);
        CHECK(peeker.done());
        CHECK_EQ(peeked, 8);
        CHECK_EQ(mailbox.num(), 1u);

        ::std::optional<int> item{};
        item = mailbox.try_get();
        REQUIRE(item.has_value());
        CHECK_EQ(*item, 8);
        CHECK_EQ(mailbox.num(), 0u);
        getter.get_promise().rethrow_exception();
        peeker.get_promise().rethrow_exception();
    }

    TEST_CASE("semaphore get rechecks availability when a peer consumes the permits first")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        ::std::vector<int> acquisition_order;
        bool drained{};

        auto first_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await semaphore.get();
            acquisition_order.push_back(1);
        }()};
        ::verilator_utils::async_task first{scheduler, ::std::move(first_task)};

        auto second_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await semaphore.get();
            acquisition_order.push_back(2);
        }()};
        ::verilator_utils::async_task second{scheduler, ::std::move(second_task)};

        scheduler.loop_once();
        CHECK_FALSE(first.done());
        CHECK_FALSE(second.done());

        // 放入许可后在同一个协程内先消耗掉它：被唤醒的第一个等待者在恢复执行前
        // 发现许可已被同伴消耗，必须重新检查可用性而不是透支计数器
        semaphore.put();
        drained = semaphore.try_get();
        scheduler.loop_once();
        CHECK(drained);
        CHECK_FALSE(first.done());
        CHECK_FALSE(second.done());
        CHECK(acquisition_order.empty());
        CHECK_FALSE(semaphore.try_get());

        // 下一个许可唤醒排在队首的等待者，但其票号尚未轮到自己，必须继续等待
        semaphore.put();
        scheduler.loop_once();
        CHECK_FALSE(first.done());
        CHECK_FALSE(second.done());

        // 票号轮到的等待者获得许可，保持先来先得顺序
        semaphore.put();
        scheduler.loop_once();
        CHECK(first.done());
        CHECK_FALSE(second.done());
        CHECK_EQ(acquisition_order, ::std::vector<int>{1});

        // 最后一个等待者在票号轮到时获得许可
        semaphore.put();
        scheduler.loop_once();
        CHECK(second.done());
        CHECK_EQ(acquisition_order, (::std::vector<int>{1, 2}));
        // 统计：共放入4个许可，1个被排水者消耗、2个被等待者获得，恰好剩余1个
        CHECK(semaphore.try_get(1));
        CHECK_FALSE(semaphore.try_get());
        first.get_promise().rethrow_exception();
        second.get_promise().rethrow_exception();
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

        auto producer{[&](this auto, int id) -> ::verilator_utils::task<void> {
            for(::std::size_t i{}; i != items_per_task; ++i)
            {
                co_await mailbox.put((id * 100 + static_cast<int>(i)));
                max_observed_size = ::std::max(max_observed_size, mailbox.num());
                capacity_violation = capacity_violation || mailbox.num() > 3;
            }
        }};
        auto consumer{[&](this auto) -> ::verilator_utils::task<void> {
            for(::std::size_t i{}; i != items_per_task; ++i) { received.push_back(co_await mailbox.get()); }
        }};

        ::std::vector<::verilator_utils::async_task> tasks;
        tasks.reserve(producer_count + consumer_count);
        for(::std::size_t i{}; i != producer_count; ++i) { tasks.emplace_back(scheduler, producer(static_cast<int>(i))); }
        for(::std::size_t i{}; i != consumer_count; ++i) { tasks.emplace_back(scheduler, consumer()); }

        scheduler.loop_until_finish();

        for(auto& task: tasks) { task.get_promise().rethrow_exception(); }
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
        ::verilator_utils::async_task task{scheduler, ::std::move(selector_task)};

        scheduler.loop_once();
        CHECK_FALSE(task.done());
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
        CHECK_FALSE(task.done());
        CHECK_EQ(results.size(), 1u);

        // 再次上升沿触发
        clk.value = 1u;
        scheduler.loop_once();
        REQUIRE_EQ(results.size(), 2u);
        REQUIRE_EQ(results[1].size(), 1u);
        CHECK_EQ(results[1][0], true);

        // 信号保持不变不会触发
        scheduler.loop_once();
        CHECK_FALSE(task.done());
        CHECK_EQ(results.size(), 2u);

        // 第三次上升沿触发，任务结束
        clk.value = 0u;
        scheduler.loop_once();
        CHECK_FALSE(task.done());
        clk.value = 1u;
        scheduler.loop_once();
        CHECK(task.done());
        REQUIRE_EQ(results.size(), 3u);
        REQUIRE_EQ(results[2].size(), 1u);
        CHECK_EQ(results[2][0], true);
        task.get_promise().rethrow_exception();
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
        ::verilator_utils::async_task task{scheduler, ::std::move(selector_task)};

        scheduler.loop_once();
        CHECK_FALSE(task.done());
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
        CHECK_FALSE(task.done());
        CHECK_EQ(results.size(), 1u);

        // clk_b 下降沿触发，clk_a 无上升沿
        clk_b.value = 0u;
        scheduler.loop_once();
        CHECK(task.done());
        REQUIRE_EQ(results.size(), 2u);
        REQUIRE_EQ(results[1].size(), 2u);
        CHECK_EQ(results[1][0], false);
        CHECK_EQ(results[1][1], true);
        task.get_promise().rethrow_exception();
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
        ::verilator_utils::async_task task{scheduler, ::std::move(selector_task)};

        scheduler.loop_once();
        CHECK_FALSE(task.done());
        CHECK(results.empty());

        clk_a.value = 1u;
        clk_b.value = 1u;
        scheduler.loop_once();
        CHECK(task.done());
        REQUIRE_EQ(results.size(), 2u);
        CHECK_EQ(results[0], true);
        CHECK_EQ(results[1], true);
        task.get_promise().rethrow_exception();
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
        ::verilator_utils::async_task task{scheduler, ::std::move(selector_task)};

        scheduler.loop_once();
        CHECK(task.done());
        REQUIRE_EQ(results.size(), 1u);
        CHECK_EQ(results[0], true);
        task.get_promise().rethrow_exception();
    }

    TEST_CASE("event suspends waiters until notify_all wakes every waiter in order")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::event event{};
        ::std::vector<int> wake_order;

        auto make_waiter{[&](this auto, int id) -> ::verilator_utils::task<void> {
            co_await event;
            wake_order.push_back(id);
        }};
        auto first_task{make_waiter(1)};
        auto second_task{make_waiter(2)};
        auto third_task{make_waiter(3)};
        ::verilator_utils::async_task first{scheduler, ::std::move(first_task)};
        ::verilator_utils::async_task second{scheduler, ::std::move(second_task)};
        ::verilator_utils::async_task third{scheduler, ::std::move(third_task)};

        scheduler.loop_once();
        CHECK_FALSE(first.done());
        CHECK_FALSE(second.done());
        CHECK_FALSE(third.done());
        CHECK(wake_order.empty());

        // notify_all 为同步调用，直接唤醒所有等待者
        event.notify_all();
        scheduler.loop_once();
        CHECK(first.done());
        CHECK(second.done());
        CHECK(third.done());
        CHECK_EQ(wake_order, (::std::vector<int>{1, 2, 3}));
        first.get_promise().rethrow_exception();
        second.get_promise().rethrow_exception();
        third.get_promise().rethrow_exception();
    }

    TEST_CASE("event notify_one wakes the oldest waiter and preserves FIFO order")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::event event{};
        ::std::vector<int> wake_order;

        auto make_waiter{[&](this auto, int id) -> ::verilator_utils::task<void> {
            co_await event;
            wake_order.push_back(id);
        }};
        auto first_task{make_waiter(1)};
        auto second_task{make_waiter(2)};
        auto third_task{make_waiter(3)};
        ::verilator_utils::async_task first{scheduler, ::std::move(first_task)};
        ::verilator_utils::async_task second{scheduler, ::std::move(second_task)};
        ::verilator_utils::async_task third{scheduler, ::std::move(third_task)};

        scheduler.loop_once();
        CHECK_FALSE(first.done());
        CHECK_FALSE(second.done());
        CHECK_FALSE(third.done());

        event.notify_one();
        scheduler.loop_once();
        CHECK(first.done());
        CHECK_FALSE(second.done());
        CHECK_FALSE(third.done());
        CHECK_EQ(wake_order, ::std::vector<int>{1});

        event.notify_one();
        scheduler.loop_once();
        CHECK(second.done());
        CHECK_FALSE(third.done());
        CHECK_EQ(wake_order, (::std::vector<int>{1, 2}));

        event.notify_one();
        scheduler.loop_once();
        CHECK(third.done());
        CHECK_EQ(wake_order, (::std::vector<int>{1, 2, 3}));

        first.get_promise().rethrow_exception();
        second.get_promise().rethrow_exception();
        third.get_promise().rethrow_exception();
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
        ::verilator_utils::async_task waiter{scheduler, ::std::move(waiter_task)};

        scheduler.loop_once();
        CHECK_FALSE(waiter.done());
        CHECK_FALSE(woke);

        // 再次通知后等待者才被唤醒
        event.notify_all();
        scheduler.loop_once();
        CHECK(waiter.done());
        CHECK(woke);

        waiter.get_promise().rethrow_exception();
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
        ::verilator_utils::async_task waiter{scheduler, ::std::move(waiter_task)};
        scheduler.loop_once();
        CHECK_FALSE(waiter.done());
        CHECK_FALSE(woke);

        // 再次通知后等待者才被唤醒
        event.notify_one();
        scheduler.loop_once();
        CHECK(waiter.done());
        CHECK(woke);

        waiter.get_promise().rethrow_exception();
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
        ::verilator_utils::async_task waiter{scheduler, ::std::move(waiter_task)};
        scheduler.loop_once();
        CHECK_FALSE(waiter.done());
        CHECK_FALSE(woke);

        // 再次通知后等待者才被唤醒
        event.notify_all();
        scheduler.loop_once();
        CHECK(waiter.done());
        CHECK(woke);

        waiter.get_promise().rethrow_exception();
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
        ::verilator_utils::async_task waiter{scheduler, ::std::move(waiter_task)};

        scheduler.loop_once();
        CHECK_FALSE(waiter.done());
        CHECK_EQ(wake_count, 0u);

        for(::std::size_t i{1}; i != 4; ++i)
        {
            event.notify_all();
            scheduler.loop_once();
            CHECK_EQ(wake_count, i);
        }
        CHECK(waiter.done());

        waiter.get_promise().rethrow_exception();
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
        auto consumer{[&](this auto) -> ::verilator_utils::task<void> {
            for(::std::size_t i{}; i != item_count; ++i)
            {
                co_await data_ready;
                received.push_back(static_cast<int>(i));
                data_consumed.notify_all();
            }
        }};
        auto producer{[&](this auto) -> ::verilator_utils::task<void> {
            for(::std::size_t i{}; i != item_count; ++i)
            {
                data_ready.notify_all();
                co_await data_consumed;
            }
        }};

        auto consumer_task{consumer()};
        ::verilator_utils::async_task consumer_async{scheduler, ::std::move(consumer_task)};

        // 消费者先就绪等待数据
        scheduler.loop_once();
        CHECK_FALSE(consumer_async.done());
        CHECK(received.empty());

        auto producer_task{producer()};
        ::verilator_utils::async_task producer_async{scheduler, ::std::move(producer_task)};
        scheduler.loop_until_finish();

        CHECK(consumer_async.done());
        CHECK(producer_async.done());
        ::std::vector<int> expected;
        expected.reserve(item_count);
        for(::std::size_t i{}; i != item_count; ++i) { expected.push_back(static_cast<int>(i)); }
        CHECK_EQ(received, expected);

        consumer_async.get_promise().rethrow_exception();
        producer_async.get_promise().rethrow_exception();
    }

    // NOLINTEND(bugprone-unchecked-optional-access)
}
