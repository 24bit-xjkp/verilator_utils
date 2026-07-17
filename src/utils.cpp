module;
#include <doctest_fwd.hpp>
export module verilator_utils:utils;
import :verilator;
import std;

extern "C++"
{
#include <doctest.h>
}

export namespace verilator_utils
{
    /**
     * @brief Verilator 时间单位枚举类
     *
     */
    enum class verilator_time_unit : ::std::uint8_t
    {
        /// 微秒
        us = 6,
        /// 100微秒
        us_100 = 7,
        /// 10微秒
        us_10 = 8,
        /// 纳秒
        ns = 9,
        /// 100皮秒
        ps_100 = 10,
        /// 10皮秒
        ps_10 = 11,
        /// 皮秒
        ps = 12,
        /// 100飞秒
        fs_100 = 13,
        /// 10飞秒
        fs_10 = 14,
        /// 飞秒
        fs = 15,
    };

    /**
     * @brief 飞秒时间类型
     *
     */
    struct femtosecond_t
    {
        /// 飞秒数
        ::std::uint64_t rep{};

        /**
         * @brief 构造一个飞秒时间对象
         *
         * @param rep 飞秒数
         */
        constexpr inline explicit femtosecond_t(::std::uint64_t rep) noexcept : rep{rep} {}

        /**
         * @brief 类型转换运算符，将飞秒时间对象转换为std::uint64_t
         *
         * @return std::uint64_t 飞秒数
         */
        constexpr inline explicit operator ::std::uint64_t () const noexcept { return rep; }

        /**
         * @brief 加法运算
         *
         * @param lhs 左操作数
         * @param rhs 右操作数
         * @return femtosecond_t 加法结果
         */
        constexpr inline friend femtosecond_t operator+ (femtosecond_t lhs, femtosecond_t rhs) noexcept
        { return femtosecond_t{lhs.rep + rhs.rep}; }

        /**
         * @brief 减法运算
         *
         * @param lhs 左操作数
         * @param rhs 右操作数
         * @return femtosecond_t 减法结果
         */
        constexpr inline friend femtosecond_t operator- (femtosecond_t lhs, femtosecond_t rhs) noexcept
        { return femtosecond_t{lhs.rep - rhs.rep}; }

        /**
         * @brief 数乘运算
         *
         * @param lhs 左操作数
         * @param rhs 右操作数
         * @return femtosecond_t 乘法结果
         */
        constexpr inline friend femtosecond_t operator* (femtosecond_t lhs, ::std::uint64_t rhs) noexcept
        { return femtosecond_t{lhs.rep * rhs}; }

        /**
         * @brief 数乘运算
         *
         * @param lhs 左操作数
         * @param rhs 右操作数
         * @return femtosecond_t 乘法结果
         */
        constexpr inline friend femtosecond_t operator* (femtosecond_t lhs, double rhs) noexcept
        { return femtosecond_t{static_cast<::std::uint64_t>(static_cast<double>(lhs.rep) * rhs)}; }

        /**
         * @brief 数除运算
         *
         * @param lhs 左操作数
         * @param rhs 右操作数
         * @return femtosecond_t 除法结果
         */
        constexpr inline friend femtosecond_t operator/ (femtosecond_t lhs, ::std::uint64_t rhs) noexcept
        { return femtosecond_t{lhs.rep / rhs}; }

        /**
         * @brief 数除运算
         *
         * @param lhs 左操作数
         * @param rhs 右操作数
         * @return femtosecond_t 除法结果
         */
        constexpr inline friend femtosecond_t operator/ (femtosecond_t lhs, double rhs) noexcept
        { return femtosecond_t{static_cast<::std::uint64_t>(static_cast<double>(lhs.rep) / rhs)}; }

        constexpr inline friend ::std::strong_ordering operator<=> (femtosecond_t, femtosecond_t) noexcept = default;
        constexpr inline friend bool operator== (femtosecond_t, femtosecond_t) noexcept = default;
    };
}  // namespace verilator_utils

export namespace verilator_utils::literals
{
    // NOLINTBEGIN(google-runtime-float)

    /**
     * @brief 飞秒时间字面值运算符
     *
     * @param rep 飞秒数
     * @return femtosecond_t 对应的飞秒时间类型
     */
    consteval inline ::verilator_utils::femtosecond_t operator""_fs (unsigned long long rep) noexcept
    { return ::verilator_utils::femtosecond_t{rep}; }

    /**
     * @brief 飞秒时间字面值运算符
     *
     * @param rep 飞秒数
     * @return femtosecond_t 对应的飞秒时间类型
     */
    consteval inline ::verilator_utils::femtosecond_t operator""_fs (long double rep) noexcept
    { return ::verilator_utils::femtosecond_t{static_cast<::std::uint64_t>(rep)}; }

    /**
     * @brief 皮秒时间字面值运算符
     *
     * @param rep 皮秒数
     * @return femtosecond_t 对应的皮秒时间类型
     */
    consteval inline ::verilator_utils::femtosecond_t operator""_ps (unsigned long long rep) noexcept
    { return ::verilator_utils::femtosecond_t{rep * 1'000u}; }

    /**
     * @brief 皮秒时间字面值运算符
     *
     * @param rep 皮秒数
     * @return femtosecond_t 对应的皮秒时间类型
     */
    consteval inline ::verilator_utils::femtosecond_t operator""_ps (long double rep) noexcept
    { return ::verilator_utils::femtosecond_t{static_cast<::std::uint64_t>(rep * 1'000u)}; }

    /**
     * @brief 纳秒时间字面值运算符
     *
     * @param rep 纳秒数
     * @return femtosecond_t 对应的纳秒时间类型
     */
    consteval inline ::verilator_utils::femtosecond_t operator""_ns (unsigned long long rep) noexcept
    { return ::verilator_utils::femtosecond_t{rep * 1'000'000u}; }

    /**
     * @brief 纳秒时间字面值运算符
     *
     * @param rep 纳秒数
     * @return femtosecond_t 对应的纳秒时间类型
     */
    consteval inline ::verilator_utils::femtosecond_t operator""_ns (long double rep) noexcept
    { return ::verilator_utils::femtosecond_t{static_cast<::std::uint64_t>(rep * 1'000'000u)}; }

    // NOLINTEND(google-runtime-float)
}  // namespace verilator_utils::literals

namespace verilator_utils
{
    namespace detail
    {
        /**
         * @brief 十六进制
         *
         */
        struct data_format_hex_t
        {
            /**
             * @brief 获取格式支持的最大宽度
             *
             * @return 最大宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t min_width() noexcept { return 1; }

            /**
             * @brief 获取格式支持的最小宽度
             *
             * @return 最小宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t max_width() noexcept { return -1zu; }
        };

        /**
         * @brief 二进制
         *
         */
        struct data_format_bin_t
        {
            /**
             * @brief 获取格式支持的最大宽度
             *
             * @return 最大宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t min_width() noexcept { return 1; }

            /**
             * @brief 获取格式支持的最小宽度
             *
             * @return 最小宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t max_width() noexcept { return -1zu; }
        };

        /**
         * @brief 有符号十进制
         * 存储在std::uint64_t中，格式为[前导0][二进制补码]
         */
        struct data_format_signed_t
        {
            /**
             * @brief 获取格式支持的最小宽度
             *
             * @return 最小宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t min_width() noexcept { return 2; }

            /**
             * @brief 获取格式支持的最大宽度
             *
             * @return 最大宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t max_width() noexcept { return 64; }
        };

        /**
         * @brief 无符号十进制
         * 存储在std::uint64_t中，格式为[前导0][数据]
         */
        struct data_format_unsigned_t
        {
            /**
             * @brief 获取格式支持的最小宽度
             *
             * @return 最小宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t min_width() noexcept { return 1; }

            /**
             * @brief 获取格式支持的最大宽度
             *
             * @return 最大宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t max_width() noexcept { return 64; }
        };

        /**
         * @brief 单精度浮点数
         * 存储在std::uint64_t中，格式为[前导0][float格式数据]
         */
        struct data_format_float_t
        {
            /// 格式化时使用十六进制浮点格式，而不是十进制浮点格式
            bool format_as_hex{};

            /**
             * @brief 获取数据宽度
             *
             * @return 数据宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t width() noexcept { return 32; }
        };

        /**
         * @brief 双精度浮点数
         * 存储在std::uint64_t中，格式为[double格式数据]
         */
        struct data_format_double_t
        {
            /// 格式化时使用十六进制浮点格式，而不是十进制浮点格式
            bool format_as_hex{};

            /**
             * @brief 获取数据宽度
             *
             * @return 数据宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t width() noexcept { return 64; }
        };

        /**
         * @brief 无符号定点数
         * 存储在std::uint64_t中，格式为[前导0][整数部分][小数部分]
         */
        struct data_format_unsigned_fixed_point_t
        {
            /// 整数位数
            ::std::uint8_t integer_bit;
            /// 小数位数
            ::std::uint8_t fractional_bit;
            /// 格式化时使用十六进制浮点格式，而不是十进制浮点格式
            bool format_as_hex;

            /**
             * @brief 获取格式支持的最小宽度
             *
             * @return 最小宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t min_width() noexcept { return 1; }

            /**
             * @brief 获取格式支持的最大宽度
             *
             * @return 最大宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t max_width() noexcept
            { return ::std::numeric_limits<double>::digits; }

            /**
             * @brief 获取定点数宽度
             *
             * @return 定点数宽度
             */
            [[nodiscard]] constexpr inline ::std::size_t width() const noexcept { return integer_bit + fractional_bit; }
        };

        /**
         * @brief 有符号定点数，即补码表示
         * 存储在std::uint64_t中，格式为[前导0][符号][整数部分，补码][小数部分，补码]
         */
        struct data_format_signed_fixed_point_t
        {
            /// 整数位数
            ::std::uint8_t integer_bit;
            /// 小数位数
            ::std::uint8_t fractional_bit;
            /// 格式化时使用十六进制浮点格式，而不是十进制浮点格式
            bool format_as_hex;

            /**
             * @brief 获取格式支持的最小宽度
             *
             * @return 最小宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t min_width() noexcept { return 2; }

            /**
             * @brief 获取格式支持的最大宽度
             *
             * @return 最大宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t max_width() noexcept
            { return ::std::numeric_limits<double>::digits + 1; }

            /**
             * @brief 获取定点数宽度
             *
             * @return 定点数宽度
             */
            [[nodiscard]] constexpr inline ::std::size_t width() const noexcept { return integer_bit + fractional_bit + 1; }
        };

        /**
         * @brief 采用符号-幅值表示的定点数，即原码表示
         * 存储在std::uint64_t中，格式为[前导0][符号][整数部分，原码][小数部分，原码]
         */
        struct data_format_sign_mag_fixed_point_t
        {
            /// 整数位数
            ::std::uint8_t integer_bit;
            /// 小数位数
            ::std::uint8_t fractional_bit;
            /// 格式化时使用十六进制浮点格式，而不是十进制浮点格式
            bool format_as_hex;

            /**
             * @brief 获取格式支持的最小宽度
             *
             * @return 最小宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t min_width() noexcept { return 2; }

            /**
             * @brief 获取格式支持的最大宽度
             *
             * @return 最大宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t max_width() noexcept
            { return ::std::numeric_limits<double>::digits + 1; }

            /**
             * @brief 获取定点数宽度
             *
             * @return 定点数宽度
             */
            [[nodiscard]] constexpr inline ::std::size_t width() const noexcept { return integer_bit + fractional_bit + 1; }
        };

        /**
         * @brief 枚举类型，采用自然二进制编码，从0开始编码
         *
         */
        struct data_format_enum_t
        {
            /// 枚举项名称列表，按枚举顺序排列
            ::std::vector<::std::string> enum_string;

            /**
             * @brief 获取格式支持的最小宽度
             *
             * @return 最小宽度
             */
            [[nodiscard]] constexpr inline ::std::size_t min_width() const noexcept
            { return enum_string.size() <= 1 ? 1 : ::std::bit_width(enum_string.size() - 1); }

            /**
             * @brief 获取格式支持的最大宽度
             *
             * @return 最大宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t max_width() noexcept { return 64; }
        };
    }  // namespace detail

    export namespace data_format
    {
        /// 数据格式类型
        using format = ::std::variant<::std::monostate,
                                      ::verilator_utils::detail::data_format_hex_t,
                                      ::verilator_utils::detail::data_format_bin_t,
                                      ::verilator_utils::detail::data_format_signed_t,
                                      ::verilator_utils::detail::data_format_unsigned_t,
                                      ::verilator_utils::detail::data_format_float_t,
                                      ::verilator_utils::detail::data_format_double_t,
                                      ::verilator_utils::detail::data_format_unsigned_fixed_point_t,
                                      ::verilator_utils::detail::data_format_signed_fixed_point_t,
                                      ::verilator_utils::detail::data_format_sign_mag_fixed_point_t,
                                      ::verilator_utils::detail::data_format_enum_t>;
        /// 十六进制
        constexpr inline ::verilator_utils::data_format::format hex{::verilator_utils::detail::data_format_hex_t{}};
        /// 二进制
        constexpr inline ::verilator_utils::data_format::format bin{::verilator_utils::detail::data_format_bin_t{}};
        /// 有符号十进制
        constexpr inline ::verilator_utils::data_format::format dec_signed{::verilator_utils::detail::data_format_signed_t{}};
        /// 无符号十进制
        constexpr inline ::verilator_utils::data_format::format dec_unsigned{::verilator_utils::detail::data_format_unsigned_t{}};

        /**
         * @brief 单精度浮点数
         *
         * @param format_as_hex 格式化时使用十六进制浮点格式，而不是十进制浮点格式
         * @return 数据格式对象
         */
        constexpr inline ::verilator_utils::data_format::format real_float(bool format_as_hex = false)
        { return ::verilator_utils::detail::data_format_float_t{format_as_hex}; }

        /**
         * @brief 双精度浮点数
         *
         * @param format_as_hex 格式化时使用十六进制浮点格式，而不是十进制浮点格式
         * @return 数据格式对象
         */
        constexpr inline ::verilator_utils::data_format::format real_double(bool format_as_hex = false)
        { return ::verilator_utils::detail::data_format_double_t{format_as_hex}; }

        /**
         * @brief 无符号定点数
         *
         * @param integer_bit 整数位数
         * @param fractional_bit 小数位数
         * @param format_as_hex 格式化时使用十六进制浮点格式，而不是十进制浮点格式
         * @return 数据格式对象
         */
        constexpr inline ::verilator_utils::data_format::format
            unsigned_fixed_point(::std::uint8_t integer_bit, ::std::uint8_t fractional_bit, bool format_as_hex = false)
        {
            return ::verilator_utils::detail::data_format_unsigned_fixed_point_t{
                integer_bit,
                fractional_bit,
                format_as_hex,
            };
        }

        /**
         * @brief 有符号定点数
         *
         * @param integer_bit 整数位数
         * @param fractional_bit 小数位数
         * @param format_as_hex 格式化时使用十六进制浮点格式，而不是十进制浮点格式
         * @return 数据格式对象
         */
        constexpr inline ::verilator_utils::data_format::format
            signed_fixed_point(::std::uint8_t integer_bit, ::std::uint8_t fractional_bit, bool format_as_hex = false)
        {
            return ::verilator_utils::detail::data_format_signed_fixed_point_t{
                integer_bit,
                fractional_bit,
                format_as_hex,
            };
        }

        /**
         * @brief 符号-幅值定点数
         *
         * @param integer_bit 整数位数
         * @param fractional_bit 小数位数
         * @param format_as_hex 格式化时使用十六进制浮点格式，而不是十进制浮点格式
         * @return 数据格式对象
         */
        constexpr inline ::verilator_utils::data_format::format
            sign_mag_fixed_point(::std::uint8_t integer_bit, ::std::uint8_t fractional_bit, bool format_as_hex = false)
        {
            return ::verilator_utils::detail::data_format_sign_mag_fixed_point_t{
                integer_bit,
                fractional_bit,
                format_as_hex,
            };
        }

        /// 枚举，常用于FSM状态变量，采用从0开始的自然二进制编码
        using fsm_enum = ::verilator_utils::detail::data_format_enum_t;

        /**
         * @brief 枚举，常用于FSM状态变量，采用从0开始的自然二进制编码
         *
         * @param enum_string 枚举项名称列表，按枚举顺序排列
         * @return 数据格式对象
         */
        constexpr inline ::verilator_utils::data_format::format sign_mag_fixed_point(::std::vector<::std::string> enum_string)
        { return ::verilator_utils::detail::data_format_enum_t{::std::move(enum_string)}; }
    }  // namespace data_format

    namespace detail
    {
        /**
         * @brief 判断是否是宽度可变的数据格式
         * hex, bin, signed、unsigned和enum是可变宽度的
         * @param format 要判断的格式
         * @return 是否宽度可变
         */
        constexpr inline bool is_variable_width_format(const ::verilator_utils::data_format::format& format) noexcept
        {
            constexpr static auto hex_index{1zu};
            constexpr static auto unsigned_index{4zu};
            return (format.index() >= hex_index && format.index() <= unsigned_index) ||
                   ::std::holds_alternative<::verilator_utils::detail::data_format_enum_t>(format);
        }
    }  // namespace detail
}  // namespace verilator_utils

export namespace verilator_utils
{
    /**
     * @brief 检查类型是否为Verilator数据类型
     *
     * @tparam type 要检查的类型
     * @note VlUnpacked不在此列，使用unpacked array应当首先解引用
     */
    template <typename type>
    concept is_verilator_data_type = ::std::same_as<type, ::CData> || ::std::same_as<type, ::SData> ||
                                     ::std::same_as<type, ::IData> || ::std::same_as<type, ::QData> || ::VlIsVlWide<type>::value;

    /**
     * @brief 检查类型是否为Verilator unpacked array类型
     *
     * @tparam type 要检查的类型
     */
    template <typename type>
    concept is_verilator_unpacked_array_type = ::IsVlUnpacked<type>::value;

    /**
     * @brief 检查类型是否为verilator数据格式可转换到的C++基本数据类型
     *
     * @tparam type 要检查的类型
     */
    template <typename type>
    concept is_cpp_underlying_type = ::std::same_as<type, ::std::int64_t> || ::std::same_as<type, ::std::uint64_t> ||
                                     ::std::same_as<type, float> || ::std::same_as<type, double>;

    template <typename type>
    struct verilator_unpacked_array_type_traits
    {
    };

    template <typename type, ::std::size_t size>
    struct verilator_unpacked_array_type_traits<::VlUnpacked<type, size>>
    {
        using value_type = type;
        constexpr inline static ::std::size_t n{size};
    };
}  // namespace verilator_utils

export namespace verilator_utils::detail
{
    extern "C++" inline int argc{};
    extern "C++" inline const char** argv{};
}  // namespace verilator_utils::detail

export namespace verilator_utils
{
    /**
     * @brief 协程生成器
     * 由于libc++尚未支持std::generator，因此添加该简易实现
     * @note 为了可以优化掉堆分配，不支持生成器嵌套
     * @tparam type 生成器值类型
     */
    template <typename type>
    struct generator : ::std::ranges::view_interface<generator<type>>
    {
        struct promise_type;
        using value_type = type;

    private:
        using handle_t = ::std::coroutine_handle<promise_type>;

        struct iterator
        {
            using difference_type = ::std::ptrdiff_t;
            using value_type = type;
            handle_t handle{};

            constexpr inline iterator& operator++ ()
            {
                handle.resume();
                handle.promise().rethrow_exception();
                return *this;
            }

            constexpr inline void operator++ (int) { ++(*this); }

            constexpr inline type& operator* () const noexcept { return *handle.promise().ptr; }

            constexpr inline friend bool operator== (iterator iter, ::std::default_sentinel_t /* unused */) noexcept
            { return iter.handle.done(); }
        };

        handle_t handle{};

    public:
        struct promise_type
        {
            type* ptr{};
            ::std::exception_ptr exception{};

            /**
             * @brief 获取返回对象
             *
             * @return 生成器对象
             */
            constexpr inline generator get_return_object() noexcept { return generator{handle_t::from_promise(*this)}; }

            /**
             * @brief 初始挂起
             *
             * @return 可等待体，总是挂起协程
             */
            constexpr inline ::std::suspend_always initial_suspend() noexcept { return ::std::suspend_always{}; }

            /**
             * @brief 最终挂起
             *
             * @return 可等待体，总是挂起协程
             */
            constexpr inline ::std::suspend_always final_suspend() noexcept { return ::std::suspend_always{}; }

            /**
             * @brief 生产值
             *
             * @param value 要生产的值
             */
            template <typename ref_t>
            constexpr inline ::std::suspend_always
                yield_value(ref_t&& value) noexcept  // NOLINT(cppcoreguidelines-missing-std-forward)
                requires (::std::same_as<type, ::std::remove_reference_t<ref_t>>)
            {
                ptr = ::std::addressof(value);
                return ::std::suspend_always{};
            }

            /**
             * @brief 退出生成器
             *
             */
            constexpr inline void return_void() noexcept {}

            /**
             * @brief 处理未捕获异常
             *
             */
            constexpr inline void unhandled_exception() noexcept { exception = ::std::current_exception(); }

            /**
             * @brief 重新抛出协程中抛出的异常
             *
             */
            inline void rethrow_exception() const
            {
                if(exception) { ::std::rethrow_exception(exception); }
            }
        };

        /**
         * @brief 构造函数
         *
         * @param handle 协程句柄
         */
        constexpr inline explicit generator(handle_t handle) noexcept : ::std::ranges::view_interface<generator>{}, handle{handle}
        {
        }

        /**
         * @brief 析构函数
         *
         */
        constexpr inline ~generator() noexcept
        {
            if(handle) { handle.destroy(); }
        }

        inline generator(const generator&) = delete;
        inline generator& operator= (const generator&) = delete;

        /**
         * @brief 移动构造函数
         *
         * @param other 要移动的生成器
         */
        constexpr inline generator(generator&& other) noexcept : handle{::std::exchange(other.handle, nullptr)} {}

        /**
         * @brief 移动赋值函数
         *
         * @param other 要移动的生成器
         */
        inline generator& operator= (generator&& other) noexcept
        {
            if(handle) { handle.destroy(); }
            handle = ::std::exchange(other, nullptr);
        }

        /**
         * @brief 获取迭代器
         *
         * @return 迭代器对象
         */
        constexpr inline iterator begin() const
        {
            // 启动生成器
            handle.resume();
            handle.promise().rethrow_exception();
            return {handle};
        }

        /**
         * @brief 获取哨位
         *
         * @return 哨位对象
         */
        constexpr inline static ::std::default_sentinel_t end() noexcept { return ::std::default_sentinel; }
    };
}  // namespace verilator_utils
