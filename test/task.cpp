#include <doctest_fwd.hpp>
import std;
import verilator_utils;
#include <doctest.h>

namespace
{
    using namespace ::verilator_utils::verilator;

    auto to_vector(::std::size_t n) noexcept { return ::std::views::take(n) | ::std::ranges::to<::std::vector<bool>>(); }

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

    TEST_CASE("mailbox get and peek wait until a value is available")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::mailbox<int> mailbox{};
        int* peeked{};
        int received{};

        auto consumer_task{[&](this auto) -> ::verilator_utils::task<void>
                           {
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

        auto consumer_task{[&](this auto) -> ::verilator_utils::task<void>
                           {
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
        auto producer_task{[&](this auto) -> ::verilator_utils::task<void>
                           {
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

        auto waiter_task{[&](this auto) -> ::verilator_utils::task<void>
                         {
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

        auto make_waiter{[&](this auto, int id) -> ::verilator_utils::task<void>
                         {
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

    // NOLINTEND(bugprone-unchecked-optional-access)
}
