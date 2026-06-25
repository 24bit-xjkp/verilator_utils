#include <doctest.h>
#include <verilated.h>
import verilator_utils;
import std;

TEST_SUITE("verilator_utils/utils")
{
    using namespace ::verilator_utils::literals;

    TEST_CASE("femtosecond literals convert to femtoseconds")
    {
        CHECK_EQ(static_cast<::std::uint64_t>(0_fs), 0u);
        CHECK_EQ(static_cast<::std::uint64_t>(1_fs), 1u);
        CHECK_EQ(static_cast<::std::uint64_t>(1.5_fs), 1u);
        CHECK_EQ(static_cast<::std::uint64_t>(2_ps), 2'000u);
        CHECK_EQ(static_cast<::std::uint64_t>(2.5_ps), 2'500u);
        CHECK_EQ(static_cast<::std::uint64_t>(3_ns), 3'000'000u);
        CHECK_EQ(static_cast<::std::uint64_t>(3.5_ns), 3'500'000u);
        CHECK_EQ(static_cast<::std::uint64_t>(0.5_ps), 500u);
        CHECK_EQ(static_cast<::std::uint64_t>(0.000'001_ns), 1u);
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
        CHECK_EQ(static_cast<::std::uint64_t>(1_fs * 1.5), 1u);
        CHECK_EQ(static_cast<::std::uint64_t>(6_ps / static_cast<::std::uint64_t>(3)), 2'000u);
        CHECK_EQ(static_cast<::std::uint64_t>(3_ps / 1.5), 2'000u);
        CHECK_EQ(static_cast<::std::uint64_t>(1_ps / 3.0), 333u);
        CHECK_LT(1_ps, 2_ps);
        CHECK_EQ(1_ns, 1'000_ps);
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

        static_assert(::std::same_as<::verilator_utils::verilator_type_to_underlying<::CData>, ::CData>);
        static_assert(::std::same_as<::verilator_utils::verilator_type_to_underlying<::VlWide<2>>, ::EData>);
    }

    TEST_CASE("verilator unpacked array type traits identify supported types")
    {
        static_assert(::verilator_utils::is_verilator_unpacked_array_type<::VlUnpacked<::CData, 1>>);
        static_assert(::verilator_utils::is_verilator_unpacked_array_type<::VlUnpacked<::SData, 1>>);
        static_assert(::verilator_utils::is_verilator_unpacked_array_type<::VlUnpacked<::IData, 1>>);
        static_assert(::verilator_utils::is_verilator_unpacked_array_type<::VlUnpacked<::QData, 1>>);
        static_assert(::verilator_utils::is_verilator_unpacked_array_type<::VlUnpacked<::VlWide<1>, 1>>);

        static_assert(::verilator_utils::is_verilator_unpacked_array_type<::VlUnpacked<::VlUnpacked<::CData, 1>, 1>>);
        static_assert(::verilator_utils::is_verilator_unpacked_array_type<::VlUnpacked<::VlUnpacked<::SData, 1>, 1>>);
        static_assert(::verilator_utils::is_verilator_unpacked_array_type<::VlUnpacked<::VlUnpacked<::IData, 1>, 1>>);
        static_assert(::verilator_utils::is_verilator_unpacked_array_type<::VlUnpacked<::VlUnpacked<::QData, 1>, 1>>);
        static_assert(::verilator_utils::is_verilator_unpacked_array_type<::VlUnpacked<::VlUnpacked<::VlWide<1>, 1>, 1>>);

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

    TEST_CASE("hex wrapper formats values with signal width")
    {
        ::verilator_utils::hex_wrapper_t zero{0u, 1};
        CHECK_EQ(zero.to_string(), "0x0");

        ::verilator_utils::hex_wrapper_t one_byte{0xau, 1};
        CHECK_EQ(one_byte.to_string(), "0xa");
        one_byte.width = 4;
        CHECK_EQ(one_byte.to_string(), "0xa");
        one_byte.width = 5;
        CHECK_EQ(one_byte.to_string(), "0x0a");
        one_byte.width = 8;
        CHECK_EQ(one_byte.to_string(), "0x0a");

        ::verilator_utils::hex_wrapper_t two_bytes{0xabu, 16};
        CHECK_EQ(two_bytes.to_string(), "0x00ab");

        ::verilator_utils::hex_wrapper_t full_word{0xffffffffu, 32};
        CHECK_EQ(full_word.to_string(), "0xffffffff");

        ::VlWide<2> wide_value{0x89abcdefu, 0x1u};
        CHECK_EQ(::verilator_utils::detail::verilator_data_to_string(wide_value, 40), "0x0189abcdef");

        ::VlWide<2> word_aligned_wide_value{0xffffffffu, 0xffffffffu};
        CHECK_EQ(::verilator_utils::detail::verilator_data_to_string(word_aligned_wide_value, 64), "0xffffffffffffffff");

        ::VlWide<3> partially_used_wide_value{0x89abcdefu, 0x1u, 0xffffffffu};
        CHECK_EQ(::verilator_utils::detail::verilator_data_to_string(partially_used_wide_value, 40), "0x0189abcdef");
    }

    TEST_CASE("std format supports hex wrapper")
    {
        CHECK_EQ(::std::format("{}", ::verilator_utils::hex_wrapper_t{0u, 1}), "0x0");
        CHECK_EQ(::std::format("{}", ::verilator_utils::hex_wrapper_t{0xau, 4}), "0xa");
        CHECK_EQ(::std::format("{}", ::verilator_utils::hex_wrapper_t{0xau, 5}), "0x0a");
        CHECK_EQ(::std::format("value={}", ::verilator_utils::hex_wrapper_t{0xabu, 16}), "value=0x00ab");
        CHECK_EQ(::std::format("{}", ::verilator_utils::hex_wrapper_t{0xffffffffu, 32}), "0xffffffff");
    }
}
