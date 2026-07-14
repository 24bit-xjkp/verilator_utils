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
            constexpr inline static ::std::size_t min_width() noexcept { return 1; }

            /**
             * @brief 获取格式支持的最小宽度
             *
             * @return 最小宽度
             */
            constexpr inline static ::std::size_t max_width() noexcept { return -1zu; }
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
            constexpr inline static ::std::size_t min_width() noexcept { return 1; }

            /**
             * @brief 获取格式支持的最小宽度
             *
             * @return 最小宽度
             */
            constexpr inline static ::std::size_t max_width() noexcept { return -1zu; }
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
            constexpr inline static ::std::size_t min_width() noexcept { return 2; }

            /**
             * @brief 获取格式支持的最大宽度
             *
             * @return 最大宽度
             */
            constexpr inline static ::std::size_t max_width() noexcept { return 64; }
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
            constexpr inline static ::std::size_t min_width() noexcept { return 1; }

            /**
             * @brief 获取格式支持的最大宽度
             *
             * @return 最大宽度
             */
            constexpr inline static ::std::size_t max_width() noexcept { return 64; }
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
            constexpr inline static ::std::size_t width() noexcept { return 32; }
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
            constexpr inline static ::std::size_t width() noexcept { return 64; }
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

            constexpr inline data_format_unsigned_fixed_point_t(::std::uint8_t integer_bit,
                                                                ::std::uint8_t fractional_bit,
                                                                bool format_as_hex = false) :
                integer_bit{integer_bit}, fractional_bit{fractional_bit}, format_as_hex{format_as_hex}
            {
            }

            /**
             * @brief 获取格式支持的最小宽度
             *
             * @return 最小宽度
             */
            constexpr inline static ::std::size_t min_width() noexcept { return 1; }

            /**
             * @brief 获取格式支持的最大宽度
             *
             * @return 最大宽度
             */
            constexpr inline static ::std::size_t max_width() noexcept
            {
                // 定点数会转化为double处理，能无损失的表示的最大位数为double尾数位数
                return ::std::numeric_limits<double>::digits - 1;
            }

            /**
             * @brief 获取定点数宽度
             *
             * @return 定点数宽度
             */
            constexpr inline ::std::size_t width() const noexcept { return integer_bit + fractional_bit; }
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

            constexpr inline data_format_signed_fixed_point_t(::std::uint8_t integer_bit,
                                                              ::std::uint8_t fractional_bit,
                                                              bool format_as_hex = false) :
                integer_bit{integer_bit}, fractional_bit{fractional_bit}, format_as_hex{format_as_hex}
            {
            }

            /**
             * @brief 获取格式支持的最小宽度
             *
             * @return 最小宽度
             */
            constexpr inline static ::std::size_t min_width() noexcept { return 2; }

            /**
             * @brief 获取格式支持的最大宽度
             *
             * @return 最大宽度
             */
            constexpr inline static ::std::size_t max_width() noexcept
            {
                // 定点数会转化为double处理，能无损失的表示的最大位数为double尾数位数+符号位
                return ::std::numeric_limits<double>::digits;
            }

            /**
             * @brief 获取定点数宽度
             *
             * @return 定点数宽度
             */
            constexpr inline ::std::size_t width() const noexcept { return integer_bit + fractional_bit + 1; }
        };

        /**
         * @brief 采用符号-幅值表示的定点数，即原码表示
         * 存储在std::uint64_t中，格式为[前导0][符号][整数部分，原码][小数部分，原码]
         */
        struct data_format_sign_mag_t
        {
            /// 整数位数
            ::std::uint8_t integer_bit;
            /// 小数位数
            ::std::uint8_t fractional_bit;
            /// 格式化时使用十六进制浮点格式，而不是十进制浮点格式
            bool format_as_hex;

            constexpr inline data_format_sign_mag_t(::std::uint8_t integer_bit,
                                                    ::std::uint8_t fractional_bit,
                                                    bool format_as_hex = false) :
                integer_bit{integer_bit}, fractional_bit{fractional_bit}, format_as_hex{format_as_hex}
            {
            }

            /**
             * @brief 获取格式支持的最小宽度
             *
             * @return 最小宽度
             */
            constexpr inline static ::std::size_t min_width() noexcept { return 2; }

            /**
             * @brief 获取格式支持的最大宽度
             *
             * @return 最大宽度
             */
            constexpr inline static ::std::size_t max_width() noexcept
            {
                // 定点数会转化为double处理，能无损失的表示的最大位数为double尾数位数+符号位
                return ::std::numeric_limits<double>::digits + 1;
            }

            /**
             * @brief 获取定点数宽度
             *
             * @return 定点数宽度
             */
            constexpr inline ::std::size_t width() const noexcept { return integer_bit + fractional_bit + 1; }
        };
    }  // namespace detail

    export namespace data_format
    {
        /// 十六进制
        constexpr inline ::verilator_utils::detail::data_format_hex_t hex{};
        /// 二进制
        constexpr inline ::verilator_utils::detail::data_format_bin_t bin{};
        /// 有符号十进制
        constexpr inline ::verilator_utils::detail::data_format_signed_t dec_signed{};
        /// 无符号十进制
        constexpr inline ::verilator_utils::detail::data_format_unsigned_t dec_unsigned{};
        /// 单精度浮点数
        constexpr inline ::verilator_utils::detail::data_format_float_t real_float{};
        /// 双精度浮点数
        constexpr inline ::verilator_utils::detail::data_format_double_t real_double{};
        /// 无符号定点数
        using unsigned_fixed_point = ::verilator_utils::detail::data_format_unsigned_fixed_point_t;
        /// 有符号定点数
        using signed_fixed_point = ::verilator_utils::detail::data_format_signed_fixed_point_t;
        /// 符号-幅值定点数
        using sign_mag = ::verilator_utils::detail::data_format_sign_mag_t;

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
                                      ::verilator_utils::detail::data_format_sign_mag_t>;
    }  // namespace data_format

    namespace detail
    {
        /**
         * @brief 判断是否是宽度可变的数据格式
         * hex, bin, signed和unsigned是可变宽度的
         * @param format 要判断的格式
         * @return 是否宽度可变
         */
        constexpr inline bool is_variable_width_format(::verilator_utils::data_format::format format) noexcept
        {
            constexpr static auto hex_index{1zu};
            constexpr static auto unsigned_index{4zu};
            return format.index() >= hex_index && format.index() <= unsigned_index;
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
