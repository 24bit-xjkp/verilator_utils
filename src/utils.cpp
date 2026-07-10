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

    /**
     * @brief 将Verilator数据类型转化到底层C++数据类型
     *
     * @tparam type Verilator数据类型
     */
    template <::verilator_utils::is_verilator_data_type type>
    using verilator_type_to_underlying = ::std::conditional_t<::VlIsVlWide<type>::value, ::EData, type>;

    namespace detail
    {
        /**
         * @brief 转换verilator数据对象为字符串的最大长度
         *
         * @param width 宽度
         * @return 字符串最大长度
         */
        inline ::std::size_t verilator_data_to_string_size(::std::size_t width)
        {
            // 0x前缀长度
            constexpr static auto prefix_size{2zu};
            return (width + 3) / 4 + prefix_size;
        }

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
        inline iter_t verilator_data_format_to(iter_t iter, const type& data, ::std::size_t width)
        {
            /// 每个字的位宽
            constexpr static ::std::size_t word_width{::std::numeric_limits<::EData>::digits};
            if constexpr(::VlIsVlWide<type>::value)
            {
                // 最高字中信号宽度
                auto left_word_width{width % word_width};
                if(left_word_width == 0) { left_word_width = word_width; }
                auto begin{data.data()};
                auto end{data.data() + (width + word_width - 1) / word_width};
                iter = ::std::format_to(iter, "{:#0{}x}", *(--end), (left_word_width + 3) / 4 + 2);
                for(auto value: ::std::views::reverse(::std::ranges::subrange{begin, end}))
                {
                    iter = ::std::format_to(iter, "{:08x}", value);
                }
                return iter;
            }
            else
            {
                return ::std::format_to(iter, "{:#0{}x}", data, (width + 3) / 4 + 2);
            }
        }

        /**
         * @brief 转换verilator数据对象为字符串表示
         *
         * @param data 数据对象
         * @param width 宽度，要求数据有效位在[0, width-1]范围内，超出部分为0
         * @return ::std::string 字符串表示
         */
        inline ::std::string verilator_data_to_string(const ::verilator_utils::is_verilator_data_type auto& data,
                                                      ::std::size_t width)
        {
            ::std::string result{};
            result.reserve(::verilator_utils::detail::verilator_data_to_string_size(width));
            ::verilator_utils::detail::verilator_data_format_to(::std::back_inserter(result), data, width);
            return result;
        }
    }  // namespace detail
}  // namespace verilator_utils

export namespace verilator_utils::detail
{
    extern "C++" inline int argc{};
    extern "C++" inline const char** argv{};
}  // namespace verilator_utils::detail
