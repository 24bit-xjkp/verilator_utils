module;
#include <doctest_fwd.hpp>
export module verilator_utils:utils;
import :verilator;
import :internal;

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
                                     ::std::same_as<type, float> || ::std::same_as<type, double> || ::std::same_as<type, bool>;

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

namespace verilator_utils
{
    namespace detail
    {
        struct double_format_to_impl_t
        {
            /**
             * @brief 转换verilator数据对象为字符串表示，写入到缓冲区上
             *
             * @tparam iter_t 输出迭代器类型
             * @tparam type 数据类型
             * @param iter 输出缓冲区迭代器
             * @param data 数据对象
             * @return 更新后的迭代器
             */
            template <typename iter_t>
            [[nodiscard]] inline iter_t format_to(this const auto& self, iter_t iter, double data)
            {
                if(self.format_as_hex) { return ::std::format_to(iter, "{:a}", data); }
                return ::std::format_to(iter, "{}", data);
            }
        };

        /**
         * @brief 检查类型是否为数字
         *
         * @tparam type 要检查的类型
         */
        template <typename type>
        concept is_number_type = ::std::integral<type> || ::std::floating_point<type>;

        /**
         * @brief 计算绝对值，运行时转发到std::abs，编译时使用简易实现
         *
         * @note <cmath>尚不支持编译时计算，因此实现该包装函数
         * @param value 要计算的值
         * @return 绝对值
         */
        constexpr inline auto abs(::verilator_utils::detail::is_number_type auto value) noexcept
        {
            if consteval
            {
                if constexpr(::std::integral<decltype(value)>)
                {
                    return ::verilator_utils::detail::abs(static_cast<double>(value));
                }
                else
                {
                    return value < 0 ? -value : value;
                }
            }
            else
            {
                return ::std::abs(value);
            }
        }

        /**
         * @brief 加载指数到浮点数，运行时转发到std::ldexp
         *
         * @note <cmath>尚不支持编译时计算，因此实现该包装函数
         * @param value 要计算的值
         *
         * @return 加载指数后的浮点数
         */
        constexpr inline auto ldexp(::verilator_utils::detail::is_number_type auto value, int exp) noexcept
        {
            if consteval
            {
                using value_t = decltype(value);
                if constexpr(::std::integral<value_t>)
                {
                    return ::verilator_utils::detail::ldexp(static_cast<double>(value), exp);
                }
                else
                {
                    constexpr value_t radix{::std::numeric_limits<value_t>::radix};
                    value_t scale{exp < 0 ? 1 / radix : radix};
                    value_t total_scale{1.0};
                    auto abs_exp{static_cast<::std::size_t>(exp < 0 ? -exp : exp)};
                    while(abs_exp != 0)
                    {
                        if((abs_exp & 1zu) == 1) { total_scale *= scale; }
                        scale *= scale;
                        abs_exp >>= 1zu;
                    }
                    return value * total_scale;
                }
            }
            else
            {
                return ::std::ldexp(value, exp);
            }
        }

        /**
         * @brief 获取符号位，运行时转发到std::signbit，编译时使用简易实现
         *
         * @note <cmath>尚不支持编译时计算，因此实现该包装函数
         * @param value 要计算的值
         * @return 符号位
         */
        constexpr inline bool signbit(::verilator_utils::detail::is_number_type auto value) noexcept
        {
            if consteval
            {
                if constexpr(::std::integral<decltype(value)>) { return value < 0; }
                else
                {
                    using array_t = ::std::array<::std::uint8_t, sizeof(value) / sizeof(::std::uint8_t)>;
                    return ::std::bit_cast<array_t>(value).back() >> 7zu;
                }
            }
            else
            {
                return ::std::signbit(value);
            }
        }

        /**
         * @brief 计算VlWide的宽度
         *
         * @tparam n VlWide元素个数
         * @param value VlWide数据
         * @return 宽度
         */
        template <::std::size_t n>
        constexpr inline ::std::size_t vlwide_width(const ::VlWide<n>& value) noexcept
        {
            constexpr static ::std::size_t word_width{::std::numeric_limits<::EData>::digits};
            constexpr static auto total_bits{n * word_width};
            ::std::size_t leading_zeros{};
            for(auto word: value.m_storage | ::std::views::reverse)
            {
                if(word == 0) { leading_zeros += word_width; }
                else
                {
                    leading_zeros += ::std::countl_zero(word);
                    break;
                }
            }
            return total_bits - leading_zeros;
        }

        /**
         * @brief 计算有符号数的宽度
         *
         * @param value 数据
         * @return 宽度
         */
        constexpr inline ::std::size_t signed_integral_width(::std::int64_t value) noexcept
        {
            auto unsigned_value{static_cast<::std::uint64_t>(value)};
            unsigned_value = value < 0 ? -unsigned_value : unsigned_value;
            return ::std::bit_width(unsigned_value);
        }
    }  // namespace detail

    export namespace data_format
    {
        /**
         * @brief 十六进制
         *
         */
        struct hex_t
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

            /**
             * @brief 转换verilator数据对象为字符串表示，写入到缓冲区上
             *
             * @tparam iter_t 输出迭代器类型
             * @tparam type 数据类型
             * @param iter 输出缓冲区迭代器
             * @param data 数据对象
             * @param width 宽度
             * @return 更新后的迭代器
             */
            template <typename iter_t, ::verilator_utils::is_verilator_data_type type>
            [[nodiscard]] inline iter_t format_to(iter_t iter, const type& data, ::std::size_t width) const
            {
                /// 每个字的位宽
                constexpr static ::std::size_t word_width{::std::numeric_limits<::EData>::digits};
                /// 每个十六进制位的位宽
                constexpr static ::std::size_t digit_width{4zu};
                // 0x前缀长度
                constexpr static auto prefix_size{2zu};
                if constexpr(::VlIsVlWide<type>::value)
                {
                    // 最高字中信号宽度
                    auto left_word_width{width % word_width};
                    if(left_word_width == 0) { left_word_width = word_width; }
                    auto begin{data.data()};
                    auto end{data.data() + (width + word_width - 1) / word_width};
                    iter = ::std::format_to(iter,
                                            "{:#0{}x}",
                                            *(--end),
                                            (left_word_width + digit_width - 1) / digit_width + prefix_size);
                    for(auto value: ::std::views::reverse(::std::ranges::subrange{begin, end}))
                    {
                        iter = ::std::format_to(iter, "{:0{}x}", value, word_width / digit_width);
                    }
                    return iter;
                }
                else
                {
                    return ::std::format_to(iter, "{:#0{}x}", data, (width + digit_width - 1) / digit_width + prefix_size);
                }
            }

            /**
             * @brief 将打包储存在std::uint64_t中的数据转换为C++底层数据类型
             *
             * @param packed_value 打包储存的数据
             * @return 转换后的数据
             */
            [[nodiscard]] constexpr inline static ::std::uint64_t to_underlying(::std::uint64_t packed_value) noexcept
            { return packed_value; }

            /**
             * @brief 将C++底层数据转换为打包储存在std::uint64_t中的数据
             *
             * @param underlying_value C++底层数据
             * @return 打包储存的数据
             */
            [[nodiscard]] constexpr inline static ::std::uint64_t to_verilator(::std::uint64_t underlying_value) noexcept
            { return underlying_value; }
        };

        /**
         * @brief 二进制
         *
         */
        struct bin_t
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

            /**
             * @brief 转换verilator数据对象为字符串表示，写入到缓冲区上
             *
             * @tparam iter_t 输出迭代器类型
             * @tparam type 数据类型
             * @param iter 输出缓冲区迭代器
             * @param data 数据对象
             * @param width 宽度
             * @return 更新后的迭代器
             */
            template <typename iter_t, ::verilator_utils::is_verilator_data_type type>
            [[nodiscard]] inline iter_t format_to(iter_t iter, const type& data, ::std::size_t width) const
            {
                /// 每个字的位宽
                constexpr static ::std::size_t word_width{::std::numeric_limits<::EData>::digits};
                // 0b前缀长度
                constexpr static auto prefix_size{2zu};
                if constexpr(::VlIsVlWide<type>::value)
                {
                    // 最高字中信号宽度
                    auto left_word_width{width % word_width};
                    if(left_word_width == 0) { left_word_width = word_width; }
                    auto begin{data.data()};
                    auto end{data.data() + (width + word_width - 1) / word_width};
                    iter = ::std::format_to(iter, "{:#0{}b}", *(--end), left_word_width + prefix_size);
                    for(auto value: ::std::views::reverse(::std::ranges::subrange{begin, end}))
                    {
                        iter = ::std::format_to(iter, "{:0{}b}", value, word_width);
                    }
                    return iter;
                }
                else
                {
                    return ::std::format_to(iter, "{:#0{}b}", data, width + prefix_size);
                }
            }

            /**
             * @brief 将打包储存在std::uint64_t中的数据转换为C++底层数据类型
             *
             * @param packed_value 打包储存的数据
             * @return 转换后的数据
             */
            [[nodiscard]] constexpr inline static ::std::uint64_t to_underlying(::std::uint64_t packed_value) noexcept
            { return packed_value; }

            /**
             * @brief 将C++底层数据转换为打包储存在std::uint64_t中的数据
             *
             * @param underlying_value C++底层数据
             * @return 打包储存的数据
             */
            [[nodiscard]] constexpr inline static ::std::uint64_t to_verilator(::std::uint64_t underlying_value) noexcept
            { return underlying_value; }
        };

        /**
         * @brief 有符号十进制
         * 存储在std::uint64_t中，格式为[前导0][二进制补码]
         */
        struct dec_signed_t
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

            /**
             * @brief 转换verilator数据对象为字符串表示，写入到缓冲区上
             *
             * @tparam iter_t 输出迭代器类型
             * @tparam type 数据类型
             * @param iter 输出缓冲区迭代器
             * @param data 数据对象，要求经过符号扩展
             * @return 更新后的迭代器
             */
            template <typename iter_t>
            [[nodiscard]] inline iter_t format_to(iter_t iter, ::std::int64_t data) const
            { return ::std::format_to(iter, "{}", data); }

            /**
             * @brief 将打包储存在std::uint64_t中的数据转换为C++底层数据类型
             *
             * @param packed_value 打包储存的数据
             * @param width 数据宽度
             * @return 转换后的数据
             */
            [[nodiscard]] constexpr inline static ::std::int64_t to_underlying(::std::uint64_t packed_value,
                                                                               ::std::size_t width) noexcept
            {
                auto shift{64zu - width};
                // NOLINTNEXTLINE(hicpp-signed-bitwise)
                auto sign_extended_value{static_cast<::std::int64_t>(packed_value << shift) >> shift};
                return sign_extended_value;
            }

            /**
             * @brief 将C++底层数据转换为打包储存在std::uint64_t中的数据
             *
             * @param underlying_value C++底层数据
             * @param width 数据宽度
             * @return 打包储存的数据
             */
            [[nodiscard]] constexpr inline static ::std::uint64_t to_verilator(::std::int64_t underlying_value,
                                                                               ::std::size_t width) noexcept
            {
                auto shift{64zu - width};
                return static_cast<::std::uint64_t>(underlying_value) << shift >> shift;
            }
        };

        /**
         * @brief 无符号十进制
         * 存储在std::uint64_t中，格式为[前导0][数据]
         */
        struct dec_unsigned_t
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

            /**
             * @brief 转换verilator数据对象为字符串表示，写入到缓冲区上
             *
             * @tparam iter_t 输出迭代器类型
             * @tparam type 数据类型
             * @param iter 输出缓冲区迭代器
             * @param data 数据对象，要求经过零扩展
             * @return 更新后的迭代器
             */
            template <typename iter_t>
            [[nodiscard]] inline iter_t format_to(iter_t iter, ::std::uint64_t data) const
            { return ::std::format_to(iter, "{}", data); }

            /**
             * @brief 将打包储存在std::uint64_t中的数据转换为C++底层数据类型
             *
             * @param packed_value 打包储存的数据
             * @return 转换后的数据
             */
            [[nodiscard]] constexpr inline static ::std::uint64_t to_underlying(::std::uint64_t packed_value) noexcept
            { return packed_value; }

            /**
             * @brief 将C++底层数据转换为打包储存在std::uint64_t中的数据
             *
             * @param underlying_value C++底层数据
             * @return 打包储存的数据
             */
            [[nodiscard]] constexpr inline static ::std::uint64_t to_verilator(::std::uint64_t underlying_value) noexcept
            { return underlying_value; }
        };

        /**
         * @brief 单精度浮点数
         * 存储在std::uint64_t中，格式为[前导0][float格式数据]
         */
        struct real_float_t
        {
            /// 格式化时使用十六进制浮点格式，而不是十进制浮点格式
            bool format_as_hex{};

            /**
             * @brief 获取数据宽度
             *
             * @return 数据宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t width() noexcept { return 32; }

            /**
             * @brief 转换verilator数据对象为字符串表示，写入到缓冲区上
             *
             * @tparam iter_t 输出迭代器类型
             * @tparam type 数据类型
             * @param iter 输出缓冲区迭代器
             * @param data 数据对象
             * @return 更新后的迭代器
             */
            template <typename iter_t>
            [[nodiscard]] inline iter_t format_to(iter_t iter, float data) const
            {
                if(format_as_hex) { return ::std::format_to(iter, "{:a}", data); }
                return ::std::format_to(iter, "{}", data);
            }

            /**
             * @brief 将打包储存在std::uint64_t中的数据转换为C++底层数据类型
             *
             * @param packed_value 打包储存的数据
             * @return 转换后的数据
             */
            [[nodiscard]] constexpr inline static float to_underlying(::std::uint64_t packed_value) noexcept
            { return ::std::bit_cast<float>(static_cast<::std::uint32_t>(packed_value)); }

            /**
             * @brief 将C++底层数据转换为打包储存在std::uint64_t中的数据
             *
             * @param underlying_value C++底层数据
             * @return 打包储存的数据
             */
            [[nodiscard]] constexpr inline static ::std::uint64_t to_verilator(float underlying_value) noexcept
            { return ::std::bit_cast<::std::uint32_t>(underlying_value); }
        };

        /**
         * @brief 双精度浮点数
         * 存储在std::uint64_t中，格式为[double格式数据]
         */
        struct real_double_t : ::verilator_utils::detail::double_format_to_impl_t
        {
            /// 格式化时使用十六进制浮点格式，而不是十进制浮点格式
            bool format_as_hex{};

            /**
             * @brief 获取数据宽度
             *
             * @return 数据宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t width() noexcept { return 64; }

            /**
             * @brief 将打包储存在std::uint64_t中的数据转换为C++底层数据类型
             *
             * @param packed_value 打包储存的数据
             * @return 转换后的数据
             */
            [[nodiscard]] constexpr inline static double to_underlying(::std::uint64_t packed_value) noexcept
            { return ::std::bit_cast<double>(packed_value); }

            /**
             * @brief 将C++底层数据转换为打包储存在std::uint64_t中的数据
             *
             * @param underlying_value C++底层数据
             * @return 打包储存的数据
             */
            [[nodiscard]] constexpr inline static ::std::uint64_t to_verilator(double underlying_value) noexcept
            { return ::std::bit_cast<::std::uint64_t>(underlying_value); }
        };

        /**
         * @brief 无符号定点数
         * 存储在std::uint64_t中，格式为[前导0][整数部分][小数部分]
         */
        struct unsigned_fixed_point_t : ::verilator_utils::detail::double_format_to_impl_t
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

            /**
             * @brief 将打包储存在std::uint64_t中的数据转换为C++底层数据类型
             *
             * @param packed_value 打包储存的数据
             * @return 转换后的数据
             */
            [[nodiscard]] constexpr inline double to_underlying(::std::uint64_t packed_value) const noexcept
            { return ::verilator_utils::detail::ldexp(packed_value, -fractional_bit); }

            /**
             * @brief 将C++底层数据转换为打包储存在std::uint64_t中的数据
             *
             * @param underlying_value C++底层数据
             * @return 打包储存的数据
             */
            [[nodiscard]] constexpr inline ::std::uint64_t to_verilator(double underlying_value) const noexcept
            { return static_cast<::std::uint64_t>(::verilator_utils::detail::ldexp(underlying_value, fractional_bit)); }
        };

        /**
         * @brief 有符号定点数，即补码表示
         * 存储在std::uint64_t中，格式为[前导0][符号][整数部分，补码][小数部分，补码]
         */
        struct signed_fixed_point_t : ::verilator_utils::detail::double_format_to_impl_t
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

            /**
             * @brief 将打包储存在std::uint64_t中的数据转换为C++底层数据类型
             *
             * @param packed_value 打包储存的数据
             * @return 转换后的数据
             */
            [[nodiscard]] constexpr inline double to_underlying(::std::uint64_t packed_value) const noexcept
            {
                auto signed_value{::verilator_utils::data_format::dec_signed_t::to_underlying(packed_value, width())};
                return ::verilator_utils::detail::ldexp(signed_value, -fractional_bit);
            }

            /**
             * @brief 将C++底层数据转换为打包储存在std::uint64_t中的数据
             *
             * @param underlying_value C++底层数据
             * @return 打包储存的数据
             */
            [[nodiscard]] constexpr inline ::std::uint64_t to_verilator(double underlying_value) const noexcept
            {
                auto signed_value{
                    static_cast<::std::int64_t>(::verilator_utils::detail::ldexp(underlying_value, fractional_bit))};
                return ::verilator_utils::data_format::dec_signed_t::to_verilator(signed_value, width());
            }
        };

        /**
         * @brief 采用符号-幅值表示的定点数，即原码表示
         * 存储在std::uint64_t中，格式为[前导0][符号][整数部分，原码][小数部分，原码]
         */
        struct sign_mag_fixed_point_t : ::verilator_utils::detail::double_format_to_impl_t
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

            /**
             * @brief 将打包储存在std::uint64_t中的数据转换为C++底层数据类型
             *
             * @param packed_value 打包储存的数据
             * @return 转换后的数据
             */
            [[nodiscard]] constexpr inline double to_underlying(::std::uint64_t packed_value) const noexcept
            {
                auto sign_bit_index{width() - 1};
                auto sign{packed_value >> sign_bit_index};
                auto magnitude_mask{(1zu << sign_bit_index) - 1zu};
                auto magnitude{::verilator_utils::detail::ldexp(packed_value & magnitude_mask, -fractional_bit)};
                return sign == 0 ? magnitude : -magnitude;
            }

            /**
             * @brief 将C++底层数据转换为打包储存在std::uint64_t中的数据
             *
             * @param underlying_value C++底层数据
             * @return 打包储存的数据
             */
            [[nodiscard]] constexpr inline ::std::uint64_t to_verilator(double underlying_value) const noexcept
            {
                auto sign{static_cast<::std::uint64_t>(::verilator_utils::detail::signbit(underlying_value))};
                auto magnitude{
                    ::verilator_utils::detail::ldexp(::verilator_utils::detail::abs(underlying_value), fractional_bit)};
                return sign << (width() - 1) | static_cast<::std::uint64_t>(magnitude);
            }
        };

        /**
         * @brief 枚举类型，采用自然二进制编码，从0开始编码
         *
         */
        struct fsm_enum_t
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

            /**
             * @brief 转换verilator数据对象为字符串表示，写入到缓冲区上
             *
             * @tparam iter_t 输出迭代器类型
             * @tparam type 数据类型
             * @param iter 输出缓冲区迭代器
             * @param data 数据对象
             * @return 更新后的迭代器
             */
            template <typename iter_t>
            [[nodiscard]] inline iter_t format_to(iter_t iter, ::std::uint64_t data) const
            {
                return ::std::format_to(iter,
                                        "{}",
                                        data < enum_string.size() ? enum_string[data]
                                                                  : ::std::format("\"invalid enum: {}\"", data));
            }

            /**
             * @brief 将打包储存在std::uint64_t中的数据转换为C++底层数据类型
             *
             * @param packed_value 打包储存的数据
             * @return 转换后的数据
             */
            [[nodiscard]] constexpr inline static ::std::uint64_t to_underlying(::std::uint64_t packed_value) noexcept
            { return packed_value; }

            /**
             * @brief 将C++底层数据转换为打包储存在std::uint64_t中的数据
             *
             * @param underlying_value C++底层数据
             * @return 打包储存的数据
             */
            [[nodiscard]] constexpr inline static ::std::uint64_t to_verilator(::std::uint64_t underlying_value) noexcept
            { return underlying_value; }
        };

        /**
         * @brief 布尔型
         *
         */
        struct boolean_t
        {
            /**
             * @brief 获取布尔型宽度
             *
             * @return 布尔型宽度
             */
            [[nodiscard]] constexpr inline static ::std::size_t width() noexcept { return 1; }

            /**
             * @brief 转换verilator数据对象为字符串表示，写入到缓冲区上
             *
             * @tparam iter_t 输出迭代器类型
             * @tparam type 数据类型
             * @param iter 输出缓冲区迭代器
             * @param data 数据对象
             * @return 更新后的迭代器
             */
            template <typename iter_t>
            [[nodiscard]] inline iter_t format_to(iter_t iter, bool data) const
            { return ::std::format_to(iter, "{}", data); }

            /**
             * @brief 将打包储存在std::uint64_t中的数据转换为C++底层数据类型
             *
             * @param packed_value 打包储存的数据
             * @return 转换后的数据
             */
            [[nodiscard]] constexpr inline static bool to_underlying(::std::uint64_t packed_value) noexcept
            { return static_cast<bool>(packed_value); }

            /**
             * @brief 将C++底层数据转换为打包储存在std::uint64_t中的数据
             *
             * @param underlying_value C++底层数据
             * @return 打包储存的数据
             */
            [[nodiscard]] constexpr inline static ::std::uint64_t to_verilator(bool underlying_value) noexcept
            { return static_cast<::std::uint64_t>(underlying_value); }
        };

        /// 数据格式类型
        using format = ::std::variant<::std::monostate,
                                      ::verilator_utils::data_format::hex_t,
                                      ::verilator_utils::data_format::bin_t,
                                      ::verilator_utils::data_format::dec_unsigned_t,
                                      ::verilator_utils::data_format::dec_signed_t,
                                      ::verilator_utils::data_format::real_float_t,
                                      ::verilator_utils::data_format::real_double_t,
                                      ::verilator_utils::data_format::unsigned_fixed_point_t,
                                      ::verilator_utils::data_format::signed_fixed_point_t,
                                      ::verilator_utils::data_format::sign_mag_fixed_point_t,
                                      ::verilator_utils::data_format::fsm_enum_t,
                                      ::verilator_utils::data_format::boolean_t>;
        /// 十六进制
        constexpr inline ::verilator_utils::data_format::format hex{::verilator_utils::data_format::hex_t{}};
        /// 二进制
        constexpr inline ::verilator_utils::data_format::format bin{::verilator_utils::data_format::bin_t{}};
        /// 无符号十进制
        constexpr inline ::verilator_utils::data_format::format dec_unsigned{::verilator_utils::data_format::dec_unsigned_t{}};
        /// 有符号十进制
        constexpr inline ::verilator_utils::data_format::format dec_signed{::verilator_utils::data_format::dec_signed_t{}};

        /**
         * @brief 单精度浮点数
         *
         * @param format_as_hex 格式化时使用十六进制浮点格式，而不是十进制浮点格式
         * @return 数据格式对象
         */
        constexpr inline ::verilator_utils::data_format::format real_float(bool format_as_hex = false)
        { return ::verilator_utils::data_format::real_float_t{format_as_hex}; }

        /**
         * @brief 双精度浮点数
         *
         * @param format_as_hex 格式化时使用十六进制浮点格式，而不是十进制浮点格式
         * @return 数据格式对象
         */
        constexpr inline ::verilator_utils::data_format::format real_double(bool format_as_hex = false)
        { return ::verilator_utils::data_format::real_double_t{.format_as_hex = format_as_hex}; }

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
            return ::verilator_utils::data_format::unsigned_fixed_point_t{
                .integer_bit = integer_bit,
                .fractional_bit = fractional_bit,
                .format_as_hex = format_as_hex,
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
            return ::verilator_utils::data_format::signed_fixed_point_t{
                .integer_bit = integer_bit,
                .fractional_bit = fractional_bit,
                .format_as_hex = format_as_hex,
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
            return ::verilator_utils::data_format::sign_mag_fixed_point_t{
                .integer_bit = integer_bit,
                .fractional_bit = fractional_bit,
                .format_as_hex = format_as_hex,
            };
        }

        /**
         * @brief 枚举，常用于FSM状态变量，采用从0开始的自然二进制编码
         *
         * @param enum_string 枚举项名称列表，按枚举顺序排列
         * @return 数据格式对象
         */
        constexpr inline ::verilator_utils::data_format::format fsm_enum(::std::vector<::std::string> enum_string)
        {
            REQUIRE_FALSE(enum_string.empty());
            return ::verilator_utils::data_format::fsm_enum_t{::std::move(enum_string)};
        }

        /// 布尔型
        constexpr inline ::verilator_utils::data_format::format boolean{::verilator_utils::data_format::boolean_t{}};

        /**
         * @brief 检查数据格式是否合法
         *
         * @param format 要检查的数据格式
         * @param width 数据宽度
         */
        constexpr inline void check_format(const ::verilator_utils::data_format::format& format, ::std::size_t width)
        {
            format.visit(
                [width]<typename format_t>(const format_t& format) -> void
                {
                    using namespace ::std::string_view_literals;
                    if constexpr(::std::same_as<format_t, ::std::monostate>)
                    {
                        if consteval { throw ::std::invalid_argument{"必须设置数据类型"}; }
                        else
                        {
                            REQUIRE_MESSAGE(false, "必须设置数据类型"sv);
                        }
                    }
                    else if constexpr(requires() {
                                          format.min_width();
                                          format.max_width();
                                      })
                    {
                        if constexpr(::std::same_as<format_t, ::verilator_utils::data_format::fsm_enum_t>)
                        {
                            if consteval
                            {
                                if(format.enum_string.empty()) { throw ::std::invalid_argument{"枚举列表不能为空"}; }
                            }
                            else
                            {
                                REQUIRE_FALSE(format.enum_string.empty());
                            }
                        }

                        auto min_width{format.min_width()};
                        auto max_width{format.max_width()};

                        if consteval
                        {
                            if(width < min_width || width > max_width)
                            {
                                throw ::std::invalid_argument{"数据宽度超出格式允许范围"};
                            }
                        }
                        else
                        {
                            REQUIRE_GE(width, min_width);
                            REQUIRE_LE(width, max_width);
                        }
                    }
                    else
                    {
                        if consteval
                        {
                            if(width != format.width()) { throw ::std::invalid_argument{"数据宽度与格式宽度不同"}; }
                        }
                        else
                        {
                            REQUIRE_EQ(width, format.width());
                        }
                    }
                });
        }
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
            constexpr static auto hex_index{::verilator_utils::variant_type_index<::verilator_utils::data_format::hex_t,
                                                                                  ::verilator_utils::data_format::format>};
            constexpr static auto unsigned_index{
                ::verilator_utils::variant_type_index<::verilator_utils::data_format::dec_unsigned_t,
                                                      ::verilator_utils::data_format::format>};
            return (format.index() >= hex_index && format.index() <= unsigned_index) ||
                   ::std::holds_alternative<::verilator_utils::data_format::fsm_enum_t>(format);
        }
    }  // namespace detail
}  // namespace verilator_utils

export namespace verilator_utils::detail
{
    /// 传递给verilator模型的argc
    inline int argc{};
    /// 传递给verilator模型的argv
    inline const char** argv{};
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
        using yielded = type&&;

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
            constexpr inline ::std::suspend_always yield_value(yielded value) noexcept
            {
                ptr = ::std::addressof(value);
                return ::std::suspend_always{};
            }

            /**
             * @brief 生产值
             *
             * @param value 要生产的值
             */
            constexpr inline auto yield_value(const std::remove_reference_t<yielded>& ref) noexcept
                requires (std::constructible_from<std::remove_cvref_t<yielded>, const std::remove_reference_t<yielded>&>)
            {
                struct awaiter
                {
                    std::remove_cvref_t<yielded> value;

                    constexpr inline static bool await_ready() noexcept { return false; }

                    constexpr inline void await_suspend(handle_t handle) noexcept { handle.ptr = ::std::addressof(value); }

                    constexpr inline static void await_resume() noexcept {}
                };

                return awaiter{ref};
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
