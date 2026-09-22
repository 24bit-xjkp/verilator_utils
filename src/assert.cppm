module;
#include <doctest_macros.hpp>
#include <clear_all_cpp_std_headers.h>
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Winclude-angled-in-module-purview"
#endif

export module verilator_utils:assert;
import std;
import std.compat;
import doctest;

extern "C++"
{
#include <cpptrace/cpptrace.hpp>
}
#ifdef __clang__
    #pragma clang diagnostic pop
#endif

namespace
{
    using namespace ::std::string_view_literals;
}

namespace verilator_utils::trace
{
    using ::cpptrace::generate_trace;
    using ::cpptrace::stacktrace;
    using ::cpptrace::stacktrace_frame;
}  // namespace verilator_utils::trace

namespace verilator_utils::detail
{
    /**
     * @brief 断言消息的颜色配置
     *
     * 与doctest框架的--force-colors和--no-colors命令行选项相对应，
     * 由main函数解析doctest命令行参数后通过set_assertion_color_config配置
     */
    struct assertion_color_config_t
    {
        /// 强制使用彩色输出
        static bool force_colors;
        /// 强制不使用彩色输出
        static bool no_colors;
    };

    constinit bool ::verilator_utils::detail::assertion_color_config_t::force_colors{};
    constinit bool ::verilator_utils::detail::assertion_color_config_t::no_colors{};

    /**
     * @brief 判断断言消息是否使用彩色输出
     *
     * 优先遵循no_colors和force_colors配置；
     * 未配置时根据标准错误输出是否为控制台自适应决定
     * @return 是否使用彩色输出
     */
    [[nodiscard]] bool should_colorize_assertion_message() noexcept
    {
        using config_t = ::verilator_utils::detail::assertion_color_config_t;
        if(config_t::no_colors) { return false; }
        if(config_t::force_colors) { return true; }
        return ::cpptrace::isatty(::cpptrace::stderr_fileno);
    }

    /// ANSI颜色转义序列
    ///
    /// @note green、yellow和blue与Stack trace(cpptrace)使用的颜色一致，
    /// 用于保证协程栈回溯与Stack trace的同名字段着色相同
    export namespace assertion_color
    {
        constexpr auto reset{"\033[0m"sv};
        constexpr auto cyan{"\033[36m"sv};
        constexpr auto green{"\033[32m"sv};
        constexpr auto yellow{"\033[33m"sv};
        constexpr auto blue{"\033[34m"sv};
        constexpr auto red{"\033[31m"sv};
        constexpr auto none{""sv};
    }  // namespace assertion_color

    /**
     * @brief 生成断言失败时的调用栈
     *
     * 生成调用栈并过滤掉断言机制自身的栈帧，使调用栈从断言调用处开始
     * @return 过滤后的调用栈
     */
    ::verilator_utils::trace::stacktrace generate_assertion_trace() noexcept
    {
        try
        {
            auto trace{::verilator_utils::trace::generate_trace()};
            constexpr static ::std::array internal_names{
                "cpptrace::"sv,
                "verilator_utils::assertion_error"sv,
                "verilator_utils::detail::assert_fail"sv,
                "verilator_utils::detail::generate_assertion_trace"sv,
                "verilator_utils::detail::check"sv,
            };
            const auto erase_begin{
                ::std::ranges::find_if(trace.frames, [](const ::verilator_utils::trace::stacktrace_frame& frame) {
                    return ::std::ranges::none_of(internal_names,
                                                  [&frame](::std::string_view name) { return frame.symbol.contains(name); });
                })};
            trace.frames.erase(trace.frames.begin(), erase_begin);
            return trace;
        }
        catch(...)
        {
            return ::verilator_utils::trace::stacktrace{};
        }
    }
}  // namespace verilator_utils::detail

export namespace verilator_utils
{
    /**
     * @brief 断言消息的颜色配置
     *
     */
    struct assertion_color_config_t
    {
        /// 强制使用彩色输出
        bool force_colors;
        /// 强制不使用彩色输出
        bool no_colors;
    };

    /**
     * @brief 获取断言彩色输出设置
     *
     * @return 彩色输出设置
     */
    ::verilator_utils::assertion_color_config_t assertion_color_config() noexcept
    {
        using config_t = ::verilator_utils::detail::assertion_color_config_t;
        return {config_t::force_colors, config_t::no_colors};
    }

    /**
     * @brief 设置断言彩色输出设置
     *
     * @param config 彩色输出设置
     */
    void set_assertion_color_config(::verilator_utils::assertion_color_config_t config) noexcept
    {
        using config_t = ::verilator_utils::detail::assertion_color_config_t;
        config_t::force_colors = config.force_colors;
        config_t::no_colors = config.no_colors;
    }

    /**
     * @brief 断言失败异常
     *
     * 由verilator_utils::check断言函数在运行时抛出，携带断言失败时的源代码位置、自定义消息和调用栈
     * @note 在常量求值语境中断言失败不会抛出该异常，而是抛出
     *       verilator_utils::detail::constexpr_assertion_failure使常量求值失败并产生编译错误
     */
    struct assertion_error : ::std::exception
    {
    private:
        /// 断言失败消息
        ::std::string message_;
        /// 断言失败的源代码位置
        ::std::source_location location_;
        /// 断言失败时的调用栈
        ::verilator_utils::trace::stacktrace trace_;
        /// 根据失败消息、源代码位置和调用栈组合成的完整错误信息
        mutable ::std::string composed_message{};

        /**
         * @brief 组合断言失败消息和源代码位置为异常描述字符串
         *
         * 根据全局颜色配置和标准错误输出是否为控制台自适应用色：
         * 控制台默认使用彩色输出，非控制台默认不使用彩色输出，
         * 也可通过set_assertion_color_config强制启用或禁用
         * @param message 断言失败消息
         * @param location 源代码位置
         * @param trace 栈回溯信息
         * @return 异常描述字符串
         */
        [[nodiscard]] ::std::string generate_message() const noexcept
        {
            const auto use_color{::verilator_utils::detail::should_colorize_assertion_message()};
            using namespace ::verilator_utils::detail::assertion_color;
            try
            {
                return ::std::format("{}At {}{}:{}{}{}:{}{}{}: {}{}{}: {}{}{}\n{}"sv,
                                     use_color ? reset : none,
                                     use_color ? green : none,
                                     location_.file_name(),
                                     use_color ? blue : none,
                                     location_.line(),
                                     use_color ? reset : none,
                                     use_color ? blue : none,
                                     location_.column(),
                                     use_color ? reset : none,
                                     use_color ? yellow : none,
                                     location_.function_name(),
                                     use_color ? reset : none,
                                     use_color ? red : none,
                                     message_,
                                     use_color ? reset : none,
                                     trace_.to_string(use_color));
            }
            catch(...)
            {
                ::std::terminate();
            }
        }

    public:
        /**
         * @brief 构造断言失败异常
         *
         * @param message 断言失败消息
         * @param location 断言失败的源代码位置
         */
        assertion_error(::std::string message, ::std::source_location location) :
            message_{::std::move(message)}, location_{location}, trace_{::verilator_utils::detail::generate_assertion_trace()}
        {
        }

        /**
         * @brief 获取断言失败消息
         *
         * @return 断言失败消息
         */
        [[nodiscard]] const ::std::string& message() const noexcept { return message_; }

        /**
         * @brief 获取断言失败的源代码位置
         *
         * @return 源代码位置
         */
        [[nodiscard]] ::std::source_location location() const noexcept { return location_; }

        /**
         * @brief 获取断言失败时的调用栈
         *
         * @return 调用栈
         */
        [[nodiscard]] const ::verilator_utils::trace::stacktrace& trace() const noexcept { return trace_; }

        /**
         * @brief 将调用栈打印到标准错误输出
         *
         */
        void print_trace() const { trace_.print(); }

        /**
         * @brief 获取异常描述字符串，包含断言失败消息和源代码位置
         *
         * @return 异常描述字符串
         */
        [[nodiscard]] const char* what() const noexcept override
        {
            if(composed_message.empty()) { composed_message = generate_message(); }
            return composed_message.c_str();
        }
    };

    /**
     * @brief 常量求值语境中的断言失败异常
     *
     * 断言函数在常量求值语境中失败时抛出该异常，使常量求值失败并产生编译错误
     * @note 该异常只能在常量求值语境中被捕获
     */
    struct constexpr_assertion_error
    {
    };
}  // namespace verilator_utils

namespace verilator_utils::detail
{
    /**
     * @brief 断言失败处理函数，无自定义消息
     *
     * @param location 断言失败的源代码位置
     */
    [[noreturn]] constexpr void assert_fail(::std::source_location location)
    {
        // NOLINTNEXTLINE(bugprone-std-exception-baseclass)
        if consteval { throw ::verilator_utils::constexpr_assertion_error{}; }
        else
        {
            throw ::verilator_utils::assertion_error{::std::string{"断言失败"sv}, location};
        }
    }

    /**
     * @brief 断言失败处理函数，携带格式化消息
     *
     * @tparam args_t 格式化参数类型
     * @param location 断言失败的源代码位置
     * @param fmt 格式化字符串
     * @param args 格式化参数
     */
    template <typename... args_t>
    [[noreturn]] constexpr void
        assert_fail(::std::source_location location, ::std::format_string<args_t...> fmt, args_t&&... args)
    {
        // NOLINTNEXTLINE(bugprone-std-exception-baseclass)
        if consteval { throw ::verilator_utils::constexpr_assertion_error{}; }
        else
        {
            throw ::verilator_utils::assertion_error{::std::format(fmt, ::std::forward<args_t>(args)...), location};
        }
    }
}  // namespace verilator_utils::detail

namespace verilator_utils
{
    export namespace detail
    {
        /**
         * @brief verilator_utils框架的检查器
         *
         */
        struct check
        {
            /**
             * @brief 构造检查器对象
             *
             * @param location 断言调用处的源代码位置
             */
            explicit constexpr check(::std::source_location location = ::std::source_location::current()) noexcept :
                location_{location}
            {
            }

            /**
             * @brief 断言检查函数
             *
             * 检查条件是否成立，不成立时抛出携带源代码位置的verilator_utils::assertion_error异常
             * @note 支持在常量求值语境中使用，条件不成立时使常量求值失败并产生编译错误
             * @param condition 断言条件
             */
            constexpr void operator() (bool condition) const
            {
                if(condition) [[likely]] { return; }
                ::verilator_utils::detail::assert_fail(location_);
            }

            /**
             * @brief 断言检查函数，携带格式化消息
             *
             * 检查条件是否成立，不成立时抛出携带格式化消息的verilator_utils::assertion_error异常
             * @note 支持在常量求值语境中使用，条件不成立时使常量求值失败并产生编译错误
             * @tparam args_t 格式化参数类型
             * @param condition 断言条件
             * @param fmt 格式化字符串
             * @param args 格式化参数
             */
            template <typename... args_t>
            constexpr void operator() (bool condition, ::std::format_string<args_t...> fmt, args_t&&... args) const
            {
                if(condition) [[likely]] { return; }
                ::verilator_utils::detail::assert_fail(location_, fmt, ::std::forward<args_t>(args)...);
            }

            /**
             * @brief 断言检查函数
             *
             * 检查条件是否成立，不成立时通知测试框架然后终止程序。这是与单元测试框架的集成点。
             * @note 支持在常量求值语境中使用，条件不成立时使常量求值失败并产生编译错误
             * @param condition 断言条件
             */
            constexpr void operator() (::std::nothrow_t, bool condition) const noexcept
            {
                if consteval
                {
                    try
                    {
                        (*this)(condition);
                    }
                    catch(...)
                    {
                        ::std::terminate();
                    }
                }
                else
                {
                    // 通知单元测试框架
                    CHECK_NOTHROW((*this)(condition));
                    // 终止程序避免进入不确定状态
                    if(!condition) [[unlikely]] { ::std::terminate(); }
                }
            }

            /**
             * @brief 断言检查函数
             *
             * 检查条件是否成立，不成立时通知测试框架然后终止程序。这是与单元测试框架的集成点。
             * @note 支持在常量求值语境中使用，条件不成立时使常量求值失败并产生编译错误
             * @param condition 断言条件
             * @param fmt 格式化字符串
             * @param args 格式化参数
             */
            template <typename... args_t>
            constexpr void operator() (::std::nothrow_t,
                                       bool condition,
                                       ::std::format_string<args_t...> fmt,
                                       args_t&&... args) const noexcept
            {
                if consteval
                {
                    try
                    {
                        (*this)(condition, fmt, ::std::forward<args_t>(args)...);
                    }
                    catch(...)
                    {
                        ::std::terminate();
                    }
                }
                else
                {
                    // 通知单元测试框架
                    CHECK_NOTHROW((*this)(condition, fmt, ::std::forward<args_t>(args)...));
                    // 终止程序避免进入不确定状态
                    if(!condition) [[unlikely]] { ::std::terminate(); }
                }
            }

        private:
            ::std::source_location location_;
        };
    }  // namespace detail

    // 引入verilator_utils命名空间以简化模块内使用
    using ::verilator_utils::detail::check;  // NOLINT(misc-unused-using-decls)
}  // namespace verilator_utils
