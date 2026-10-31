#include <doctest_macros.hpp>
import unit_test;

TEST_SUITE("verilator_utils/assert")
{
    using namespace ::std::string_view_literals;

    TEST_CASE("check passes without throwing")
    {
        ::verilator_utils::check{}(true);
        ::verilator_utils::check{}(true);
        ::verilator_utils::check{}(1 == 1, "消息{}"sv, 1);  // NOLINT(misc-redundant-expression)
    }

    TEST_CASE("check is usable in constant evaluation")
    {
        constexpr auto ok_result{[] {
            ::verilator_utils::check{}(true);
            ::verilator_utils::check{}(2 == 2, "常量求值消息{}"sv, 2);  // NOLINT(misc-redundant-expression)
            return true;
        }()};
        static_assert(ok_result);
    }

    TEST_CASE("check throws assertion_error with formatted message")
    {
        CHECK_THROWS_AS(::verilator_utils::check{}(false, "自定义消息{}"sv, 42), ::verilator_utils::assertion_error);
        try
        {
            ::verilator_utils::check{}(false, "自定义消息{}"sv, 42);
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK_EQ(error.message(), "自定义消息42"sv);
            CHECK(::std::string_view{error.what()}.contains("自定义消息42"sv));
        }
    }

    TEST_CASE("check accepts plain and default assertion messages")
    {
        try
        {
            ::verilator_utils::check{}(false);
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK_EQ(error.message(), "断言失败"sv);
        }

        try
        {
            ::verilator_utils::check{}(false, "纯文本消息"sv);
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK_EQ(error.message(), "纯文本消息"sv);
            CHECK(::std::string_view{error.what()}.contains("纯文本消息"sv));
        }

        try
        {
            ::verilator_utils::check{}(false, "字面量消息");
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK_EQ(error.message(), "字面量消息"sv);
        }
    }

    TEST_CASE("assertion_error carries source location")
    {
        try
        {
            ::verilator_utils::check{}(false, "位置测试"sv);
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
            ::verilator_utils::check{}(false, "调用栈测试"sv);
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

    TEST_CASE("assertion_error omits the stack trace when it is disabled")
    {
        const auto backup{::verilator_utils::assertion_config()};
        ::verilator_utils::set_assertion_config({.stacktrace = false});
        try
        {
            ::verilator_utils::check{}(false, "禁用调用栈"sv);
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            // 关闭栈回溯后异常仍携带消息与位置，但调用栈为空
            CHECK(error.trace().empty());
            CHECK(::std::string_view{error.what()}.contains("禁用调用栈"sv));
            CHECK(::std::string_view{error.what()}.contains("test/assert.cpp"sv));
        }
        ::verilator_utils::set_assertion_config(backup);
    }

    TEST_CASE("assertion_error message colorization follows color configuration")
    {
        const auto backup{::verilator_utils::assertion_config()};
        // 强制不使用彩色输出
        ::verilator_utils::set_assertion_config({.force_colors = false, .no_colors = true});
        try
        {
            ::verilator_utils::check{}(false, "颜色配置测试"sv);
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK_FALSE(::std::string_view{error.what()}.contains("\033["sv));
            CHECK(::std::string_view{error.what()}.contains("颜色配置测试"sv));
        }

        // 强制使用彩色输出
        ::verilator_utils::set_assertion_config({.force_colors = true, .no_colors = false});
        try
        {
            ::verilator_utils::check{}(false, "颜色配置测试"sv);
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK(::std::string_view{error.what()}.contains("\033["sv));
            CHECK(::std::string_view{error.what()}.contains("颜色配置测试"sv));
        }

        // 恢复默认配置
        ::verilator_utils::set_assertion_config(backup);
    }

    TEST_CASE("assertion message colorization adapts to the terminal when no color option is set")
    {
        const auto backup{::verilator_utils::assertion_config()};
        // 既未强制彩色也未禁用彩色时按标准错误输出是否为控制台自适应
        ::verilator_utils::set_assertion_config({.force_colors = false, .no_colors = false});
        try
        {
            ::verilator_utils::check{}(false, "自适应颜色"sv);
        }
        catch(const ::verilator_utils::assertion_error& error)
        {
            CHECK(::std::string_view{error.what()}.contains("自适应颜色"sv));
            CHECK_EQ(::std::string_view{error.what()}.contains("\033["sv), ::verilator_utils::detail::is_stderr_tty());
        }
        ::verilator_utils::set_assertion_config(backup);
    }

    TEST_CASE("check nothrow overload reports through the test framework instead of throwing")
    {
        ::verilator_utils::check{}(::std::nothrow, true);
        ::verilator_utils::check{}(::std::nothrow, 3 == 3);  // NOLINT(misc-redundant-expression)

        // 常量求值语境下同样可用，条件成立时不影响常量求值
        constexpr auto evaluated{[] {
            ::verilator_utils::check{}(::std::nothrow, true);
            return true;
        }()};
        static_assert(evaluated);
    }

    TEST_CASE("warn reports warnings through the test framework without throwing")
    {
        static_assert(noexcept(::verilator_utils::warn{}(false)));

        ::verilator_utils::warn{}(true);
        ::verilator_utils::warn{}(true, "不报告警告{}"sv, 1);

        // 常量求值语境下不报告警告，也不影响常量求值
        constexpr auto evaluated{[] {
            ::verilator_utils::warn{}(true);
            ::verilator_utils::warn{}(true, "常量求值不报告警告{}"sv, 2);
            return true;
        }()};
        static_assert(evaluated);

        // 条件不成立时只向doctest报告警告，不抛出异常
        CHECK_NOTHROW(::verilator_utils::warn{}(false));
        CHECK_NOTHROW(::verilator_utils::warn{}(false, "纯文本警告"sv));
        CHECK_NOTHROW(::verilator_utils::warn{}(false, "测试警告{}"sv, 42));
    }
}
