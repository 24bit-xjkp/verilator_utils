module;
#include <doctest_fwd.hpp>
module unit_test;

extern "C++"
{
#include <doctest.h>
}

namespace
{
    auto to_vector(::std::size_t n) noexcept { return ::std::views::take(n) | ::std::ranges::to<::std::vector<bool>>(); }
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
        CHECK(::std::ranges::equal(::verilator_utils::fibonacci_lfsr_generator(64, 1, 1ull << 63) | ::to_vector(1),
                                   expected_sequence3));
        CHECK(::std::ranges::equal(::verilator_utils::galois_lfsr_generator(64, 1, 1ull << 63) | ::to_vector(1),
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
}
