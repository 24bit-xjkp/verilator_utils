#include <doctest_macros.hpp>
#include <assert_macros.hpp>
import verilator_utils.full;
using namespace ::verilator_utils::verilator;

TEST_SUITE("verilator_utils/utils")
{
    using namespace ::verilator_utils::literals;
    using namespace ::verilator_utils::verilator;

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
        CHECK_EQ(static_cast<::std::uint64_t>(1_fs * 1.5), 1u);
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
}

TEST_SUITE("verilator_utils/assert")
{
    using namespace ::std::string_view_literals;

    TEST_CASE("check passes without throwing")
    {
        ::verilator_utils::check(true);
        VU_CHECK(true);
        VU_CHECK(1 == 1, "消息{}", 1);
    }

    TEST_CASE("check is usable in constant evaluation")
    {
        constexpr auto ok_result{[] {
            ::verilator_utils::check(true);
            VU_CHECK(2 == 2, "常量求值消息{}", 2);
            return true;
        }()};
        static_assert(ok_result);
    }

    TEST_CASE("check throws assertion_error with formatted message")
    {
        CHECK_THROWS_AS(VU_CHECK(false, "自定义消息{}", 42), ::verilator_utils::assertion_error);
        try
        {
            VU_CHECK(false, "自定义消息{}", 42);
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK_EQ(error.message(), "自定义消息42");
            CHECK(::std::string_view{error.what()}.find("自定义消息42") != ::std::string_view::npos);
        }
    }

    TEST_CASE("assertion_error carries source location")
    {
        try
        {
            VU_CHECK(false, "位置测试");
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK(::std::string_view{error.location().file_name()}.ends_with("test/utils.cpp"));
            CHECK_GT(error.location().line(), 0u);
            CHECK_FALSE(::std::string_view{error.location().function_name()}.empty());
            CHECK(::std::string_view{error.what()}.find("位置测试") != ::std::string_view::npos);
            CHECK(::std::string_view{error.what()}.find("test/utils.cpp") != ::std::string_view::npos);
        }
    }

    TEST_CASE("assertion_error carries stack trace without internal frames")
    {
        try
        {
            VU_CHECK(false, "调用栈测试");
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK_FALSE(error.trace().empty());
            CHECK_FALSE(error.trace().frames.empty());
            for(const auto& frame: error.trace().frames)
            {
                CHECK_FALSE(::std::string_view{frame.symbol}.find("verilator_utils::assertion_error") !=
                            ::std::string_view::npos);
            }
            error.print_trace();
        }
    }
}
