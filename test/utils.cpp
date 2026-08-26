#include <doctest_macros.hpp>
import verilator_utils.full;
using namespace ::verilator_utils::verilator;

TEST_SUITE("verilator_utils/utils")
{
    using namespace ::verilator_utils;
    using namespace ::verilator_utils::verilator;
    using namespace ::std::string_view_literals;

    TEST_CASE("femtosecond literals convert to femtoseconds")
    {
        CHECK_EQ(static_cast<::std::uint64_t>(0_fs), 0u);
        CHECK_EQ(static_cast<::std::uint64_t>(1_fs), 1u);
        CHECK_EQ(static_cast<::std::uint64_t>(1.5_fs), 2u);
        CHECK_EQ(static_cast<::std::uint64_t>(2_ps), 2'000u);
        CHECK_EQ(static_cast<::std::uint64_t>(2.5_ps), 2'500u);
        CHECK_EQ(static_cast<::std::uint64_t>(3_ns), 3'000'000u);
        CHECK_EQ(static_cast<::std::uint64_t>(3.5_ns), 3'500'000u);
        CHECK_EQ(static_cast<::std::uint64_t>(0.5_ps), 500u);
        CHECK_EQ(static_cast<::std::uint64_t>(0.000'001_ns), 1u);
    }

    TEST_CASE("microsecond literals convert to femtoseconds")
    {
        CHECK_EQ(static_cast<::std::uint64_t>(0_us), 0u);
        CHECK_EQ(static_cast<::std::uint64_t>(1_us), 1'000'000'000ull);
        CHECK_EQ(static_cast<::std::uint64_t>(2_us), 2'000'000'000ull);
        CHECK_EQ(static_cast<::std::uint64_t>(1.5_us), 1'500'000'000ull);
        CHECK_EQ(static_cast<::std::uint64_t>(0.5_us), 500'000'000ull);
        CHECK_EQ(static_cast<::std::uint64_t>(0.000'001_us), 1'000u);
        CHECK_EQ(static_cast<::std::uint64_t>(1.000'000'001_us), 1'000'000'001ull);
    }

    TEST_CASE("millisecond literals convert to femtoseconds")
    {
        CHECK_EQ(static_cast<::std::uint64_t>(0_ms), 0u);
        CHECK_EQ(static_cast<::std::uint64_t>(1_ms), 1'000'000'000'000ull);
        CHECK_EQ(static_cast<::std::uint64_t>(2_ms), 2'000'000'000'000ull);
        CHECK_EQ(static_cast<::std::uint64_t>(1.5_ms), 1'500'000'000'000ull);
        CHECK_EQ(static_cast<::std::uint64_t>(0.5_ms), 500'000'000'000ull);
        CHECK_EQ(static_cast<::std::uint64_t>(0.000'001_ms), 1'000'000ull);
    }

    TEST_CASE("femtosecond arithmetic and comparison")
    {
        constexpr static auto duration{2_ns + 500_ps};

        CHECK_EQ(static_cast<::std::uint64_t>(duration), 2'500'000u);
        CHECK_EQ(static_cast<::std::uint64_t>(duration - 1_ns), 1'500'000u);
        CHECK_EQ(static_cast<::std::uint64_t>(1_ns - 1_ns), 0u);
        CHECK_EQ(static_cast<::std::uint64_t>(2_ps * static_cast<::std::uint64_t>(0)), 0u);
        CHECK_EQ(static_cast<::std::uint64_t>(2_ps * static_cast<::std::uint64_t>(3)), 6'000u);
        CHECK_EQ(static_cast<::std::uint64_t>(2_ps * 1.5), 3'000u);
        CHECK_EQ(static_cast<::std::uint64_t>(1_fs * 1.5), 2u);
        CHECK_EQ(static_cast<::std::uint64_t>(6_ps / static_cast<::std::uint64_t>(3)), 2'000u);
        CHECK_EQ(static_cast<::std::uint64_t>(3_ps / 1.5), 2'000u);
        CHECK_EQ(static_cast<::std::uint64_t>(1_ps / 3.0), 333u);
        CHECK_LT(1_ps, 2_ps);
        CHECK_EQ(1_ns, 1'000_ps);
        CHECK_EQ(1_us, 1'000_ns);
        CHECK_EQ(1_ms, 1'000'000_ns);
        CHECK_EQ(1_us, 1'000_ps * static_cast<::std::uint64_t>(1'000));
        CHECK_EQ(1_ms, 1'000_us);
        CHECK_EQ(1_ms, 1'000'000_ps * static_cast<::std::uint64_t>(1'000));
    }

    TEST_CASE("second literals convert to femtoseconds")
    {
        CHECK_EQ(static_cast<::std::uint64_t>(0_s), 0u);
        CHECK_EQ(static_cast<::std::uint64_t>(1_s), 1'000'000'000'000'000ull);
        CHECK_EQ(static_cast<::std::uint64_t>(2_s), 2'000'000'000'000'000ull);
        CHECK_EQ(static_cast<::std::uint64_t>(1.5_s), 1'500'000'000'000'000ull);
        CHECK_EQ(static_cast<::std::uint64_t>(0.5_s), 500'000'000'000'000ull);
        CHECK_EQ(static_cast<::std::uint64_t>(0.000'001_s), 1'000'000'000ull);
        CHECK_EQ(static_cast<::std::uint64_t>(18'000_s), 18'000'000'000'000'000'000ull);
        CHECK_EQ(1_s, 1'000_ms);
        CHECK_EQ(1_s, 1'000'000_us);
        CHECK_EQ(1_s, 1'000'000'000_ns);
        CHECK_EQ(1_s, 1'000'000'000'000_ps);
        CHECK_EQ(1_s, 1'000'000'000'000'000_fs);
    }

    TEST_CASE("floating point femtosecond values round to nearest integer")
    {
        CHECK_EQ(static_cast<::std::uint64_t>(0.4_fs), 0u);
        CHECK_EQ(static_cast<::std::uint64_t>(0.5_fs), 1u);
        CHECK_EQ(static_cast<::std::uint64_t>(1.5_fs), 2u);
        CHECK_EQ(static_cast<::std::uint64_t>(2.4_fs), 2u);
        CHECK_EQ(static_cast<::std::uint64_t>(2.5_fs), 3u);
        CHECK_EQ(static_cast<::std::uint64_t>(2.6_fs), 3u);
        CHECK_EQ(static_cast<::std::uint64_t>(1_fs * 2.4), 2u);
        CHECK_EQ(static_cast<::std::uint64_t>(1_fs * 2.5), 3u);
        CHECK_EQ(static_cast<::std::uint64_t>(1_fs / 2.0), 1u);
        CHECK_EQ(static_cast<::std::uint64_t>(1_fs / 3.0), 0u);
        CHECK_EQ(static_cast<::std::uint64_t>(1_ps / 3.0), 333u);
        CHECK_EQ(static_cast<::std::uint64_t>(1_ps / 3.5), 286u);
        CHECK_EQ(static_cast<::std::uint64_t>(1_ps / 4.0), 250u);
    }

    TEST_CASE("femtosecond addition throws on overflow")
    {
        constexpr static auto max{::std::numeric_limits<::std::uint64_t>::max()};
        CHECK_THROWS_AS(femtosecond_t{max} + 1_fs, ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(femtosecond_t{max} + femtosecond_t{max}, ::verilator_utils::assertion_error);
        CHECK_EQ(static_cast<::std::uint64_t>(femtosecond_t{max} + 0_fs), max);
    }

    TEST_CASE("femtosecond subtraction throws on underflow")
    {
        constexpr static auto max{::std::numeric_limits<::std::uint64_t>::max()};
        CHECK_THROWS_AS(1_fs - 2_fs, ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(0_fs - 1_fs, ::verilator_utils::assertion_error);
        CHECK_EQ(static_cast<::std::uint64_t>(femtosecond_t{max} - 0_fs), max);
        CHECK_EQ(static_cast<::std::uint64_t>(femtosecond_t{max} - femtosecond_t{max}), 0u);
    }

    TEST_CASE("femtosecond multiplication throws on overflow")
    {
        constexpr static auto max{::std::numeric_limits<::std::uint64_t>::max()};
        CHECK_THROWS_AS(femtosecond_t{max} * static_cast<::std::uint64_t>(2), ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(femtosecond_t{max / 2 + 1} * static_cast<::std::uint64_t>(2), ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(femtosecond_t{max} * 2.0, ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(femtosecond_t{max} * 1e19, ::verilator_utils::assertion_error);
        CHECK_EQ(static_cast<::std::uint64_t>(femtosecond_t{max} * static_cast<::std::uint64_t>(1)), max);
        CHECK_EQ(static_cast<::std::uint64_t>(femtosecond_t{max / 2} * static_cast<::std::uint64_t>(2)), max - 1);
    }

    TEST_CASE("femtosecond multiplication rejects negative multiplier")
    {
        CHECK_THROWS_AS(1_fs * -1.0, ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(1_fs * -0.5, ::verilator_utils::assertion_error);
    }

    TEST_CASE("femtosecond division rejects invalid divisors and overflow")
    {
        constexpr static auto max{::std::numeric_limits<::std::uint64_t>::max()};
        CHECK_THROWS_AS(1_fs / static_cast<::std::uint64_t>(0), ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(1_fs / 0.0, ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(1_fs / -1.0, ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(1_fs / 1e-20, ::verilator_utils::assertion_error);
        CHECK_EQ(static_cast<::std::uint64_t>(femtosecond_t{max} / static_cast<::std::uint64_t>(1)), max);
    }

    TEST_CASE("femtosecond double constructor validates range")
    {
        CHECK_THROWS_AS(femtosecond_t{-1.0}, ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(femtosecond_t{-0.5}, ::verilator_utils::assertion_error);
        CHECK_EQ(static_cast<::std::uint64_t>(femtosecond_t{0.0}), 0u);
        CHECK_EQ(static_cast<::std::uint64_t>(femtosecond_t{0.5}), 1u);
        CHECK_EQ(static_cast<::std::uint64_t>(femtosecond_t{1.5}), 2u);
    }

    TEST_CASE("femtosecond operations are usable in constant evaluation")
    {
        static_assert(static_cast<::std::uint64_t>(1_s) == 1'000'000'000'000'000ull);
        // NOLINTBEGIN(google-runtime-float)
        static_assert(static_cast<::std::uint64_t>(1.5_s) == 1'500'000'000'000'000ull);
        static_assert(static_cast<::std::uint64_t>(1.5_fs) == 2u);
        // NOLINTEND(google-runtime-float)
        static_assert(1_s == 1'000_ms);
        static_assert(1_s == 1'000'000'000'000_ps);
        constexpr auto sum{1_s + 2_s};
        static_assert(static_cast<::std::uint64_t>(sum) == 3'000'000'000'000'000ull);
        constexpr auto product{1_ms * static_cast<::std::uint64_t>(3)};
        static_assert(static_cast<::std::uint64_t>(product) == 3'000'000'000'000ull);
        constexpr auto quotient{1_ms / static_cast<::std::uint64_t>(2)};
        static_assert(static_cast<::std::uint64_t>(quotient) == 500'000'000'000ull);
        constexpr auto scaled{1_ns * 1.5};
        static_assert(static_cast<::std::uint64_t>(scaled) == 1'500'000u);
    }

    TEST_CASE("femtosecond overflow errors carry descriptive messages")
    {
        constexpr static auto max{::std::numeric_limits<::std::uint64_t>::max()};
        try
        {
            auto result{femtosecond_t{max} + 1_fs};
            (void)result;
            FAIL("expected assertion_error for addition overflow"sv);
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK_EQ(error.message(), "发生上溢"sv);
        }
        try
        {
            auto result{1_fs - 2_fs};
            (void)result;
            FAIL("expected assertion_error for subtraction underflow"sv);
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK_EQ(error.message(), "发生下溢"sv);
        }
        try
        {
            auto result{1_fs / static_cast<::std::uint64_t>(0)};
            (void)result;
            FAIL("expected assertion_error for division by zero"sv);
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK_EQ(error.message(), "发生除0"sv);
        }
        try
        {
            auto result{1_fs * -1.0};
            (void)result;
            FAIL("expected assertion_error for negative multiplier"sv);
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK_EQ(error.message(), "非法乘数: -1"sv);
        }
        try
        {
            auto result{1_fs / -1.0};
            (void)result;
            FAIL("expected assertion_error for negative divisor"sv);
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK_EQ(error.message(), "非法除数: -1"sv);
        }
    }

    TEST_CASE("verilator data type traits identify supported types")
    {
        static_assert(::verilator_utils::is_verilator_data_type<::CData>);
        static_assert(::verilator_utils::is_verilator_data_type<::SData>);
        static_assert(::verilator_utils::is_verilator_data_type<::IData>);
        static_assert(::verilator_utils::is_verilator_data_type<::QData>);
        static_assert(::verilator_utils::is_verilator_data_type<::VlWide<2>>);
        static_assert(::verilator_utils::is_verilator_data_type<::VlWide<1>>);
        static_assert(!::verilator_utils::is_verilator_data_type<int>);
        static_assert(!::verilator_utils::is_verilator_data_type<const ::CData>);
    }

    TEST_CASE("verilator unpacked array type traits identify supported types")
    {
        static_assert(::verilator_utils::is_vl_unpacked_type<::VlUnpacked<::CData, 1>>);
        static_assert(::verilator_utils::is_vl_unpacked_type<::VlUnpacked<::SData, 1>>);
        static_assert(::verilator_utils::is_vl_unpacked_type<::VlUnpacked<::IData, 1>>);
        static_assert(::verilator_utils::is_vl_unpacked_type<::VlUnpacked<::QData, 1>>);
        static_assert(::verilator_utils::is_vl_unpacked_type<::VlUnpacked<::VlWide<1>, 1>>);

        static_assert(::verilator_utils::is_vl_unpacked_type<::VlUnpacked<::VlUnpacked<::CData, 1>, 1>>);
        static_assert(::verilator_utils::is_vl_unpacked_type<::VlUnpacked<::VlUnpacked<::SData, 1>, 1>>);
        static_assert(::verilator_utils::is_vl_unpacked_type<::VlUnpacked<::VlUnpacked<::IData, 1>, 1>>);
        static_assert(::verilator_utils::is_vl_unpacked_type<::VlUnpacked<::VlUnpacked<::QData, 1>, 1>>);
        static_assert(::verilator_utils::is_vl_unpacked_type<::VlUnpacked<::VlUnpacked<::VlWide<1>, 1>, 1>>);

        {
            using type_traits = ::verilator_utils::verilator_unpacked_array_type_traits<::VlUnpacked<::CData, 1>>;
            static_assert(::std::same_as<type_traits::value_type, ::CData>);
            static_assert(type_traits::n == 1);
        }
        {
            using type_traits = ::verilator_utils::verilator_unpacked_array_type_traits<::VlUnpacked<::VlWide<1>, 1>>;
            static_assert(::std::same_as<type_traits::value_type, ::VlWide<1>>);
            static_assert(type_traits::n == 1);
        }
        {
            using type_traits =
                ::verilator_utils::verilator_unpacked_array_type_traits<::VlUnpacked<::VlUnpacked<::CData, 1>, 1>>;
            static_assert(::std::same_as<type_traits::value_type, ::VlUnpacked<::CData, 1>>);
            static_assert(type_traits::n == 1);
        }
    }
}
