#include <doctest_macros.hpp>
#include <assert_macros.hpp>
import verilator_utils.full;
using namespace ::verilator_utils::verilator;

TEST_SUITE("verilator_utils/assert")
{
    using namespace ::std::string_view_literals;

    TEST_CASE("check passes without throwing")
    {
        ::verilator_utils::check(true);
        VU_CHECK(true);
        VU_CHECK(1 == 1, "消息{}"sv, 1);
    }

    TEST_CASE("check is usable in constant evaluation")
    {
        constexpr auto ok_result{[] {
            ::verilator_utils::check(true);
            VU_CHECK(2 == 2, "常量求值消息{}"sv, 2);
            return true;
        }()};
        static_assert(ok_result);
    }

    TEST_CASE("check throws assertion_error with formatted message")
    {
        CHECK_THROWS_AS(VU_CHECK(false, "自定义消息{}"sv, 42), ::verilator_utils::assertion_error);
        try
        {
            VU_CHECK(false, "自定义消息{}"sv, 42);
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK_EQ(error.message(), "自定义消息42"sv);
            CHECK(::std::string_view{error.what()}.contains("自定义消息42"sv));
        }
    }

    TEST_CASE("assertion_error carries source location")
    {
        try
        {
            VU_CHECK(false, "位置测试"sv);
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK(::std::string_view{error.location().file_name()}.ends_with("test/assert.cpp"sv));
            CHECK_GT(error.location().line(), 0u);
            CHECK_FALSE(::std::string_view{error.location().function_name()}.empty());
            CHECK(::std::string_view{error.what()}.contains("位置测试"sv));
            CHECK(::std::string_view{error.what()}.contains("test/assert.cpp"sv));
        }
    }

    TEST_CASE("assertion_error carries stack trace without internal frames")
    {
        try
        {
            VU_CHECK(false, "调用栈测试"sv);
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK_FALSE(error.trace().empty());
            CHECK_FALSE(error.trace().frames.empty());
            for(const auto& frame: error.trace().frames)
            {
                CHECK_FALSE(::std::string_view{frame.symbol}.contains("verilator_utils::assertion_error"sv));
            }
        }
    }

    TEST_CASE("assertion_error message colorization follows color configuration")
    {
        // 强制不使用彩色输出
        ::verilator_utils::set_assertion_color_config({.force_colors = false, .no_colors = true});
        try
        {
            VU_CHECK(false, "颜色配置测试"sv);
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK_FALSE(::std::string_view{error.what()}.contains("\033["sv));
            CHECK(::std::string_view{error.what()}.contains("颜色配置测试"sv));
        }

        // 强制使用彩色输出
        ::verilator_utils::set_assertion_color_config({.force_colors = true, .no_colors = false});
        try
        {
            VU_CHECK(false, "颜色配置测试"sv);
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK(::std::string_view{error.what()}.contains("\033["sv));
            CHECK(::std::string_view{error.what()}.contains("颜色配置测试"sv));
        }

        // 恢复默认配置
        ::verilator_utils::set_assertion_color_config({});
    }
}
