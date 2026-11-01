#include <doctest_macros.hpp>
import unit_test;

namespace
{
    auto to_vector(::std::size_t n) noexcept { return ::std::views::take(n) | ::std::ranges::to<::std::vector<bool>>(); }
}  // namespace

TEST_SUITE("verilator_utils/lfsr")
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

    TEST_CASE("generator iterators support post-increment")
    {
        const auto fibonacci{::verilator_utils::fibonacci_lfsr_generator(3)};
        auto iter{fibonacci.begin()};
        const auto expected{*iter};

        // 后置自增返回void，只推进迭代器
        iter++;
        CHECK_EQ(*iter, false);
        CHECK_NE(*iter, expected);
    }

    TEST_CASE("LFSR generator validates its arguments when the coroutine first runs")
    {
        // 参数校验位于协程体内，异常在生成器首次恢复时被捕获并在迭代时重新抛出
        const auto width_too_small{::verilator_utils::fibonacci_lfsr_generator(2)};
        CHECK_THROWS_WITH_AS(static_cast<void>(width_too_small.begin()),
                             ::doctest::Contains{"LFSR宽度2超出范围[3, 64]"},
                             ::verilator_utils::assertion_error);

        const auto zero_initial_value{::verilator_utils::galois_lfsr_generator(4, 1, 0)};
        CHECK_THROWS_WITH_AS(static_cast<void>(zero_initial_value.begin()),
                             ::doctest::Contains{"初始值为0时LFSR输出恒为0"},
                             ::verilator_utils::assertion_error);

        // 宽度上界同样受检查，0同样不满足下界
        const auto width_too_large{::verilator_utils::galois_lfsr_generator(65)};
        CHECK_THROWS_WITH_AS(static_cast<void>(width_too_large.begin()),
                             ::doctest::Contains{"LFSR宽度65超出范围[3, 64]"},
                             ::verilator_utils::assertion_error);
        const auto width_zero{::verilator_utils::fibonacci_lfsr_generator(0)};
        CHECK_THROWS_WITH_AS(static_cast<void>(width_zero.begin()),
                             ::doctest::Contains{"LFSR宽度0超出范围[3, 64]"},
                             ::verilator_utils::assertion_error);
    }
}
