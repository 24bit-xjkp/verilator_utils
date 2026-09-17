#include <doctest_macros.hpp>
import unit_test;

TEST_SUITE("verilator_utils/utils")
{
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
        static_assert(static_cast<::std::uint64_t>(1.5_s) == 1'500'000'000'000'000ull);
        static_assert(static_cast<::std::uint64_t>(1.5_fs) == 2u);
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

    TEST_CASE("width_cast keeps unsigned values that fit the target width")
    {
        CHECK_EQ(::verilator_utils::width_cast(0u, 8zu), 0u);
        CHECK_EQ(::verilator_utils::width_cast(1u, 1zu), 1u);
        CHECK_EQ(::verilator_utils::width_cast(2u, 2zu), 2u);
        CHECK_EQ(::verilator_utils::width_cast(0x1Fu, 5zu), 0x1Fu);
        CHECK_EQ(::verilator_utils::width_cast(0xABCu, 12zu), 0xABCu);
        CHECK_EQ(::verilator_utils::width_cast(255u, 8zu), 255u);
        CHECK_EQ(::verilator_utils::width_cast(65'535u, 16zu), 65'535u);
        CHECK_EQ(::verilator_utils::width_cast(::std::numeric_limits<::std::uint32_t>::max(), 32zu),
                 static_cast<::std::uint64_t>(::std::numeric_limits<::std::uint32_t>::max()));
        CHECK_THROWS_AS(::verilator_utils::width_cast(0u, 0zu), ::verilator_utils::assertion_error);
    }

    TEST_CASE("width_cast rejects unsigned values that exceed the target width")
    {
        CHECK_THROWS_AS(::verilator_utils::width_cast(1u, 0zu), ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(::verilator_utils::width_cast(2u, 1zu), ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(::verilator_utils::width_cast(0x20u, 5zu), ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(::verilator_utils::width_cast(256u, 8zu), ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(::verilator_utils::width_cast(65'536u, 16zu), ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(::verilator_utils::width_cast(::std::numeric_limits<::std::uint64_t>::max(), 63zu),
                        ::verilator_utils::assertion_error);
    }

    TEST_CASE("width_cast reinterprets signed values as two's complement")
    {
        CHECK_EQ(::verilator_utils::width_cast(0, 8zu), 0u);
        CHECK_EQ(::verilator_utils::width_cast(1, 8zu), 1u);
        CHECK_EQ(::verilator_utils::width_cast(127, 8zu), 127u);
        CHECK_EQ(::verilator_utils::width_cast(-1, 8zu), 0xFFu);
        CHECK_EQ(::verilator_utils::width_cast(-127, 8zu), 0x81u);
        CHECK_EQ(::verilator_utils::width_cast(1, 2zu), 0b01u);
        CHECK_EQ(::verilator_utils::width_cast(-1, 2zu), 0b11u);
        CHECK_EQ(::verilator_utils::width_cast(-1, 5zu), 0x1Fu);
        CHECK_EQ(::verilator_utils::width_cast(-3, 12zu), 0xFFDu);
        CHECK_EQ(::verilator_utils::width_cast(::std::int16_t{-2}, 16zu), 0xFFFEu);
        CHECK_EQ(::verilator_utils::width_cast(::std::int32_t{-2}, 32zu), 0xFFFF'FFFEu);
    }

    TEST_CASE("width_cast accepts the most negative value of the target width")
    {
        CHECK_EQ(::verilator_utils::width_cast(-2, 2zu), 0b10u);
        CHECK_EQ(::verilator_utils::width_cast(-128, 8zu), 0x80u);
        CHECK_EQ(::verilator_utils::width_cast(-32'768, 16zu), 0x8000u);
        CHECK_EQ(::verilator_utils::width_cast(::std::int16_t{-32'768}, 16zu), 0x8000u);
        CHECK_EQ(::verilator_utils::width_cast(::std::numeric_limits<::std::int64_t>::min() / 2, 63zu), 0x4000'0000'0000'0000ull);
    }

    TEST_CASE("width_cast reproduces the two's complement pattern over the whole int16 range")
    {
        ::std::size_t mismatches{};
        for(auto raw: ::std::views::iota(-32'768, 32'768))
        {
            auto expected{static_cast<::std::uint64_t>(static_cast<::std::uint16_t>(raw))};
            if(::verilator_utils::width_cast(static_cast<::std::int16_t>(raw), 16zu) != expected) { ++mismatches; }
        }
        CHECK_EQ(mismatches, 0zu);
    }

    TEST_CASE("width_cast rejects signed values that exceed the target width")
    {
        CHECK_THROWS_AS(::verilator_utils::width_cast(2, 2zu), ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(::verilator_utils::width_cast(-3, 2zu), ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(::verilator_utils::width_cast(128, 8zu), ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(::verilator_utils::width_cast(-129, 8zu), ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(::verilator_utils::width_cast(32'768, 16zu), ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(::verilator_utils::width_cast(-32'769, 16zu), ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(::verilator_utils::width_cast(::std::numeric_limits<::std::int64_t>::min(), 63zu),
                        ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(::verilator_utils::width_cast(::std::numeric_limits<::std::int64_t>::max() / 2 + 1, 63zu),
                        ::verilator_utils::assertion_error);
    }

    TEST_CASE("width_cast requires at least two bits for signed values")
    {
        CHECK_THROWS_AS(::verilator_utils::width_cast(0, 0zu), ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(::verilator_utils::width_cast(0, 1zu), ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(::verilator_utils::width_cast(-1, 1zu), ::verilator_utils::assertion_error);
        CHECK_THROWS_AS(::verilator_utils::width_cast(::std::int16_t{1}, 1zu), ::verilator_utils::assertion_error);
    }

    TEST_CASE("width_cast preserves the full sixty-four bit target width")
    {
        constexpr static auto uint64_max{::std::numeric_limits<::std::uint64_t>::max()};
        constexpr static auto int64_min{::std::numeric_limits<::std::int64_t>::min()};
        constexpr static auto int64_max{::std::numeric_limits<::std::int64_t>::max()};
        CHECK_EQ(::verilator_utils::width_cast(0u, 64zu), 0u);
        CHECK_EQ(::verilator_utils::width_cast(uint64_max, 64zu), uint64_max);
        CHECK_EQ(::verilator_utils::width_cast(0, 64zu), 0u);
        CHECK_EQ(::verilator_utils::width_cast(-1, 64zu), uint64_max);
        CHECK_EQ(::verilator_utils::width_cast(int64_min, 64zu), 0x8000'0000'0000'0000ull);
        CHECK_EQ(::verilator_utils::width_cast(int64_max, 64zu), 0x7FFF'FFFF'FFFF'FFFFull);
    }

    TEST_CASE("width_cast accepts every integral value type")
    {
        CHECK_EQ(::verilator_utils::width_cast(static_cast<::std::uint8_t>(255), 8zu), 255u);
        CHECK_EQ(::verilator_utils::width_cast(static_cast<::std::int8_t>(-1), 8zu), 0xFFu);
        CHECK_EQ(::verilator_utils::width_cast(static_cast<::std::uint16_t>(65'535), 16zu), 65'535u);
        CHECK_EQ(::verilator_utils::width_cast(static_cast<::std::int16_t>(-1), 16zu), 0xFFFFu);
        CHECK_EQ(::verilator_utils::width_cast(static_cast<::std::uint32_t>(0xFFFF'FFFFu), 32zu), 0xFFFF'FFFFu);
        CHECK_EQ(::verilator_utils::width_cast(static_cast<::std::int32_t>(-1), 32zu), 0xFFFF'FFFFu);
        CHECK_EQ(::verilator_utils::width_cast(0x0123'4567'89ABull, 48zu), 0x0123'4567'89ABull);
        CHECK_EQ(::verilator_utils::width_cast(::CData{0xABu}, 8zu), 0xABu);
        CHECK_EQ(::verilator_utils::width_cast(::SData{0xCDEFu}, 16zu), 0xCDEFu);
        CHECK_EQ(::verilator_utils::width_cast(::IData{0xDEAD'BEEFu}, 32zu), 0xDEAD'BEEFu);
        CHECK_EQ(::verilator_utils::width_cast(::QData{0x0123'4567'89ABull}, 48zu), 0x0123'4567'89ABull);
        static_assert(::std::same_as<decltype(::verilator_utils::width_cast(0u, 8zu)), ::std::uint64_t>);
        static_assert(::std::same_as<decltype(::verilator_utils::width_cast(::std::int8_t{0}, 8zu)), ::std::uint64_t>);
    }

    TEST_CASE("width_cast is usable in constant evaluation")
    {
        static_assert(::verilator_utils::width_cast(255u, 8zu) == 255u);
        static_assert(::verilator_utils::width_cast(0xABCu, 12zu) == 0xABCu);
        static_assert(::verilator_utils::width_cast(-1, 8zu) == 0xFFu);
        static_assert(::verilator_utils::width_cast(-128, 8zu) == 0x80u);
        static_assert(::verilator_utils::width_cast(static_cast<::std::int16_t>(-2), 16zu) == 0xFFFEu);
        static_assert(::verilator_utils::width_cast(1u, 64zu) == 1u);
        static_assert(::verilator_utils::width_cast(-1, 64zu) == ::std::numeric_limits<::std::uint64_t>::max());
        static_assert(::verilator_utils::width_cast(::std::numeric_limits<::std::int64_t>::min(), 64zu) ==
                      0x8000'0000'0000'0000ull);
        constexpr auto value{::verilator_utils::width_cast(-1, 4zu)};
        static_assert(value == 0xFu);
        CHECK_EQ(value, 0xFu);
    }

    TEST_CASE("width_cast reports the offending value and the target width")
    {
        CHECK_THROWS_WITH_AS(::verilator_utils::width_cast(256u, 8zu),
                             ::doctest::Contains{"256超出uint8的表示范围"},
                             ::verilator_utils::assertion_error);
        CHECK_THROWS_WITH_AS(::verilator_utils::width_cast(-129, 8zu),
                             ::doctest::Contains{"-129超出int8的表示范围"},
                             ::verilator_utils::assertion_error);
        CHECK_THROWS_WITH_AS(::verilator_utils::width_cast(1, 1zu),
                             ::doctest::Contains{"有符号数宽度至少为2"},
                             ::verilator_utils::assertion_error);
    }
}
