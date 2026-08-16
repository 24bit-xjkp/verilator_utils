module;
#include <clear_all_cpp_std_headers.h>
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Winclude-angled-in-module-purview"
#endif

export module verilator_utils:assert;
import std;
import std.compat;

extern "C++"
{
#include <cpptrace/cpptrace.hpp>
}
#ifdef __clang__
    #pragma clang diagnostic pop
#endif

export namespace verilator_utils::trace
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
    export struct assertion_color_config_t
    {
        /// 强制使用彩色输出
        bool force_colors{};
        /// 强制不使用彩色输出
        bool no_colors{};
    };

    constinit ::verilator_utils::detail::assertion_color_config_t assertion_color_config{};

    /**
     * @brief 判断断言消息是否使用彩色输出
     *
     * 优先遵循no_colors和force_colors配置；
     * 未配置时根据标准错误输出是否为控制台自适应决定
     * @return 是否使用彩色输出
     */
    [[nodiscard]] inline bool should_colorize_assertion_message() noexcept
    {
        const auto& config{::verilator_utils::detail::assertion_color_config};
        if(config.no_colors) { return false; }
        if(config.force_colors) { return true; }
        return ::cpptrace::isatty(::cpptrace::stderr_fileno);
    }

    /// ANSI颜色转义序列
    namespace assertion_color
    {
        using namespace ::std::string_view_literals;
        constexpr inline auto reset{"\033[0m"sv};
        constexpr inline auto cyan{"\033[36m"sv};
        constexpr inline auto yellow{"\033[33m"sv};
        constexpr inline auto red{"\033[31m"sv};
    }  // namespace assertion_color
}  // namespace verilator_utils::detail

export namespace verilator_utils
{
    /**
     * @brief 获取断言彩色输出设置
     *
     * @return 彩色输出设置
     */
    ::verilator_utils::detail::assertion_color_config_t get_assertion_color_config() noexcept
    { return ::verilator_utils::detail::assertion_color_config; }

    /**
     * @brief 设置断言彩色输出设置
     *
     * @param config 彩色输出设置
     */
    void set_assertion_color_config(::verilator_utils::detail::assertion_color_config_t config) noexcept
    { ::verilator_utils::detail::assertion_color_config = config; }

    namespace detail
    {
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
        inline ::std::string compose_assertion_message(::std::string_view message,
                                                       const ::std::source_location& location,
                                                       const ::verilator_utils::trace::stacktrace& trace)
        {
            const auto use_colors{::verilator_utils::detail::should_colorize_assertion_message()};
            if(use_colors)
            {
                return ::std::format("At {}{}:{}:{}{}: {}{}{}: {}{}{}\n{}",
                                     assertion_color::cyan,
                                     location.file_name(),
                                     location.line(),
                                     location.column(),
                                     assertion_color::reset,
                                     assertion_color::yellow,
                                     location.function_name(),
                                     assertion_color::reset,
                                     assertion_color::red,
                                     message,
                                     assertion_color::reset,
                                     trace.to_string(true));
            }
            return ::std::format("At {}:{}:{}: {}: {}\n{}",
                                 location.file_name(),
                                 location.line(),
                                 location.column(),
                                 location.function_name(),
                                 message,
                                 trace.to_string(false));
        }

        /**
         * @brief 生成断言失败时的调用栈
         *
         * 生成调用栈并过滤掉断言机制自身的栈帧，使调用栈从断言调用处开始
         * @return 过滤后的调用栈
         */
        inline ::verilator_utils::trace::stacktrace generate_assertion_trace() noexcept
        {
            using namespace ::std::string_view_literals;
            try
            {
                auto trace{::verilator_utils::trace::generate_trace()};
                constexpr static ::std::array internal_names{
                    "cpptrace::"sv,
                    "verilator_utils::assertion_error"sv,
                    "verilator_utils::detail::assert_fail"sv,
                    "verilator_utils::detail::generate_assertion_trace"sv,
                    "verilator_utils::check"sv,
                };
                auto erase_begin{
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
    }  // namespace detail

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
        ::std::string composed_message;

    public:
        /**
         * @brief 构造断言失败异常
         *
         * @param message 断言失败消息
         * @param location 断言失败的源代码位置
         */
        assertion_error(::std::string message, ::std::source_location location) :
            message_{::std::move(message)}, location_{location}, trace_{::verilator_utils::detail::generate_assertion_trace()},
            composed_message{::verilator_utils::detail::compose_assertion_message(message_, location_, trace_)}
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
        [[nodiscard]] const char* what() const noexcept override { return composed_message.c_str(); }
    };

    namespace detail
    {
        /**
         * @brief 常量求值语境中的断言失败异常
         *
         * 断言函数在常量求值语境中失败时抛出该异常，使常量求值失败并产生编译错误
         * @note 该异常只能在常量求值语境中被捕获
         */
        struct constexpr_assertion_failure
        {
        };

        /**
         * @brief 断言失败处理函数，无自定义消息
         *
         * @param location 断言失败的源代码位置
         */
        [[noreturn]] constexpr void assert_fail(::std::source_location location)
        {
            if consteval { throw ::verilator_utils::detail::constexpr_assertion_failure{}; }
            else
            {
                throw ::verilator_utils::assertion_error{"断言失败", location};
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
            if consteval { throw ::verilator_utils::detail::constexpr_assertion_failure{}; }
            else
            {
                throw ::verilator_utils::assertion_error{::std::format(fmt, ::std::forward<args_t>(args)...), location};
            }
        }
    }  // namespace detail

    /**
     * @brief 断言检查函数
     *
     * 检查条件是否成立，不成立时抛出携带源代码位置的verilator_utils::assertion_error异常
     * @note 支持在常量求值语境中使用，条件不成立时使常量求值失败并产生编译错误
     * @param condition 断言条件
     * @param location 断言调用处的源代码位置
     */
    constexpr void check(bool condition, ::std::source_location location = ::std::source_location::current())
    {
        if(condition) [[likely]] { return; }
        ::verilator_utils::detail::assert_fail(location);
    }

    /**
     * @brief 断言检查函数，携带格式化消息
     *
     * 检查条件是否成立，不成立时抛出携带格式化消息的verilator_utils::assertion_error异常
     * @note 支持在常量求值语境中使用，条件不成立时使常量求值失败并产生编译错误
     * @note 由于参数包之后不能携带默认参数，源代码位置需要显式传入，
     *       通常使用assert_macros.hpp中的VU_CHECK宏自动捕获
     * @tparam args_t 格式化参数类型
     * @param condition 断言条件
     * @param location 断言调用处的源代码位置
     * @param fmt 格式化字符串
     * @param args 格式化参数
     * @code {.cpp}
     * check(width >= 3 && width <= 64, ::std::source_location::current(), "LFSR宽度{}超出范围[3, 64]", width);
     * @endcode
     */
    template <typename... args_t>
    constexpr void check(bool condition, ::std::source_location location, ::std::format_string<args_t...> fmt, args_t&&... args)
    {
        if(condition) [[likely]] { return; }
        ::verilator_utils::detail::assert_fail(location, fmt, ::std::forward<args_t>(args)...);
    }
}  // namespace verilator_utils
