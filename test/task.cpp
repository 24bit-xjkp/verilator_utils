#include "doctest_macros.hpp"
import verilator_utils.full;

namespace
{
    using namespace ::verilator_utils::verilator;

    auto to_vector(::std::size_t n) noexcept { return ::std::views::take(n) | ::std::ranges::to<::std::vector<bool>>(); }

    struct signal_state
    { ::CData value{}; };

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
}  // namespace

TEST_SUITE("verilator_utils/task")
{
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
        ::verilator_utils::mailbox<int> mailbox{2};

        CHECK_EQ(mailbox.num(), 0u);
        CHECK_FALSE(mailbox.try_get().has_value());
        CHECK_FALSE(mailbox.try_peek().has_value());
        CHECK(mailbox.try_put(10));
        CHECK(mailbox.try_put(20));
        CHECK_FALSE(mailbox.try_put(30));
        CHECK_EQ(mailbox.num(), 2u);

        auto first_peek{mailbox.try_peek()};
        REQUIRE(first_peek.has_value());
        CHECK_EQ(*first_peek, 10);
        *first_peek = 11;
        CHECK_EQ(mailbox.num(), 2u);

        auto first{mailbox.try_get()};
        REQUIRE(first.has_value());
        CHECK_EQ(*first, 11);
        auto second{mailbox.try_get()};
        REQUIRE(second.has_value());
        CHECK_EQ(*second, 20);
        CHECK_EQ(mailbox.num(), 0u);
    }

    TEST_CASE("unbounded mailbox accepts move-only values")
    {
        ::verilator_utils::mailbox<::std::unique_ptr<int>> mailbox{};

        CHECK(mailbox.try_put(::std::make_unique<int>(42)));
        auto value{mailbox.try_get()};

        REQUIRE(value.has_value());
        REQUIRE(*value);
        CHECK_EQ(**value, 42);
        CHECK_EQ(mailbox.num(), 0u);
    }

    TEST_CASE("mailbox formatter renders values and detailed state")
    {
        using mailbox_t = ::verilator_utils::mailbox<int>;
        static_assert(::std::formattable<mailbox_t, char>);

        mailbox_t bounded_mailbox{3};
        CHECK(bounded_mailbox.try_put(10));
        CHECK(bounded_mailbox.try_put(20));

        CHECK_EQ(::std::format("{}", bounded_mailbox), "[10, 20]");
        CHECK_EQ(::std::format("{:#}", bounded_mailbox), "{max_count: 3, value: [10, 20]}");
        CHECK_EQ(::doctest::StringMaker<mailbox_t>::convert(bounded_mailbox), "{max_count: 3, value: [10, 20]}");

        mailbox_t empty_mailbox{};
        CHECK_EQ(::std::format("{:#}", empty_mailbox), "{max_count: 0, value: []}");
    }

    TEST_CASE("mailbox formatter rejects unsupported format specifiers")
    {
        ::verilator_utils::mailbox<int> mailbox{};

        CHECK_THROWS_AS(static_cast<void>(::std::vformat("{:x}", ::std::make_format_args(mailbox))), ::std::format_error);
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
        CHECK_EQ(first->to_string(), "0x01");

        auto second{delay_line.update(0x05u)};
        REQUIRE(second.has_value());
        CHECK_EQ(second->value(), 0x02u);
        CHECK_EQ(second->to_string(), "0x02");

        auto third{delay_line.update(0x06u)};
        REQUIRE(third.has_value());
        CHECK_EQ(third->value(), 0x03u);
        CHECK_EQ(third->to_string(), "0x03");
    }

    TEST_CASE("shift_register with depth one echoes the previous value")
    {
        ::verilator_utils::shift_register<::std::uint64_t> delay_line{1, 8, ::verilator_utils::data_format::hex};

        CHECK_FALSE(delay_line.update(0x11u).has_value());

        auto echoed{delay_line.update(0x22u)};
        REQUIRE(echoed.has_value());
        CHECK_EQ(echoed->value(), 0x11u);
        CHECK_EQ(echoed->to_string(), "0x11");

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
        CHECK_EQ(held_first->to_string(), "0x01");

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
        CHECK_EQ(held->to_string(), "0x012389abcdef");
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
        CHECK_EQ(binary_value->to_string(), "0b1011");

        ::verilator_utils::shift_register<::std::uint64_t> decimal_delay_line{2, 8, ::verilator_utils::data_format::dec_unsigned};
        decimal_delay_line.update(42u);
        decimal_delay_line.update(43u);
        auto decimal_value{decimal_delay_line.update(44u)};
        REQUIRE(decimal_value.has_value());
        CHECK_EQ(decimal_value->value(), 42u);
        CHECK(::std::holds_alternative<::verilator_utils::data_format::dec_unsigned_t>(decimal_value->format()));
        CHECK_EQ(decimal_value->to_string(), "42");
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
        CHECK_EQ(delayed->to_string(), "0x012389abcdef");

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

        CHECK_EQ(::std::format("{}", delay_line), "[1, 2, 3]");
        CHECK_EQ(::std::format("{:#}", delay_line), "{depth: 3, reg: [1, 2, 3]}");
        CHECK_EQ(::doctest::StringMaker<shift_register_t>::convert(delay_line), "{depth: 3, reg: [1, 2, 3]}");
    }

    TEST_CASE("shift_register formatter rejects unsupported format specifiers")
    {
        ::verilator_utils::shift_register<::std::uint64_t> delay_line{1, 8, ::verilator_utils::data_format::hex};

        CHECK_THROWS_AS(static_cast<void>(::std::vformat("{:x}", ::std::make_format_args(delay_line))), ::std::format_error);
    }

    TEST_CASE("mailbox get and peek wait until a value is available")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::mailbox<int> mailbox{};
        int* peeked{};
        int received{};

        auto consumer_task{[&](this auto) -> ::verilator_utils::task<void> {
            int& reference{co_await mailbox.peek()};
            peeked = ::std::addressof(reference);
            reference = 18;
            received = co_await mailbox.get();
        }()};
        ::verilator_utils::async_task consumer{scheduler, ::std::move(consumer_task)};

        scheduler.loop_once();
        CHECK_FALSE(consumer.done());
        CHECK_EQ(mailbox.num(), 0u);

        CHECK(mailbox.try_put(17));
        scheduler.loop_once();

        CHECK(consumer.done());
        CHECK(peeked);
        CHECK_EQ(received, 18);
        CHECK_EQ(mailbox.num(), 0u);
        consumer.get_promise().rethrow_exception();
    }

    TEST_CASE("const mailbox peek operations preserve const reference identity")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::mailbox<int> mailbox{};
        const auto& const_mailbox{mailbox};
        const int* peeked{};
        int observed{};

        static_assert(::std::same_as<decltype(const_mailbox.peek()), ::verilator_utils::task<const int&>>);
        static_assert(::std::same_as<decltype(const_mailbox.try_peek()), ::std::optional<const int&>>);
        CHECK_FALSE(const_mailbox.try_peek().has_value());

        auto consumer_task{[&](this auto) -> ::verilator_utils::task<void> {
            const int& reference{co_await const_mailbox.peek()};
            peeked = ::std::addressof(reference);
            observed = reference;
        }()};
        ::verilator_utils::async_task consumer{scheduler, ::std::move(consumer_task)};

        scheduler.loop_once();
        CHECK_FALSE(consumer.done());
        CHECK(mailbox.try_put(29));
        scheduler.loop_once();

        auto nonblocking_peek{const_mailbox.try_peek()};
        REQUIRE(nonblocking_peek.has_value());
        CHECK(consumer.done());
        CHECK_EQ(observed, 29);
        CHECK_EQ(peeked, ::std::addressof(*nonblocking_peek));
        CHECK_EQ(const_mailbox.num(), 1u);
        consumer.get_promise().rethrow_exception();
    }

    TEST_CASE("bounded mailbox put waits for available capacity")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::mailbox<int> mailbox{1};
        bool producer_completed{};

        CHECK(mailbox.try_put(1));
        auto producer_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await mailbox.put(2);
            producer_completed = true;
        }()};
        ::verilator_utils::async_task producer{scheduler, ::std::move(producer_task)};

        scheduler.loop_once();
        CHECK_FALSE(producer.done());
        CHECK_FALSE(producer_completed);
        CHECK_EQ(mailbox.num(), 1u);

        auto first{mailbox.try_get()};
        REQUIRE(first.has_value());
        CHECK_EQ(*first, 1);
        scheduler.loop_once();

        CHECK(producer.done());
        CHECK(producer_completed);
        CHECK_EQ(mailbox.num(), 1u);
        auto second{mailbox.try_get()};
        REQUIRE(second.has_value());
        CHECK_EQ(*second, 2);
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

    // NOLINTEND(bugprone-unchecked-optional-access)
}
