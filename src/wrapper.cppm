module;
#include <assert_macros.hpp>
export module verilator_utils:wrapper;
import :utils;
import doctest;

namespace
{
    using namespace ::std::string_view_literals;
}

export namespace verilator_utils
{
    /**
     * @brief 检查类型是否为格式包装器支持的数据类型
     *
     * @tparam type 要检查的类型
     */
    template <typename type>
    concept is_format_wrapper_data_type = ::verilator_utils::is_cpp_underlying_type<type> || ::verilator_utils::is_vl_wide<type>;

    template <::verilator_utils::is_format_wrapper_data_type type>
    struct format_wrapper;

    template <::verilator_utils::is_verilator_data_type type>
    struct bit_slice;

    template <::verilator_utils::is_verilator_data_type type>
    struct vector_slice;

    template <::verilator_utils::is_verilator_data_type type, ::std::size_t n>
        requires (n != 0)
    struct unpacked_array;
}  // namespace verilator_utils

namespace verilator_utils::detail
{
    template <typename type>
    constexpr bool is_format_wrapper_impl{false};

    template <::verilator_utils::is_format_wrapper_data_type type>
    constexpr bool is_format_wrapper_impl<::verilator_utils::format_wrapper<type>>{true};

    template <typename type>
    constexpr bool is_bit_slice_impl{false};

    template <::verilator_utils::is_verilator_data_type type>
    constexpr bool is_bit_slice_impl<::verilator_utils::bit_slice<type>>{true};

    template <typename type>
    constexpr bool is_vector_slice_impl{false};

    template <::verilator_utils::is_verilator_data_type type>
    constexpr bool is_vector_slice_impl<::verilator_utils::vector_slice<type>>{true};

    template <typename type>
    constexpr bool is_unpacked_array_impl{false};

    template <typename type, ::std::size_t n>
    constexpr bool is_unpacked_array_impl<::verilator_utils::unpacked_array<type, n>>{true};

    /**
     * @brief 检查数据格式是否支持三路比较
     *
     * @param format 数据格式
     */
    void check_three_way_compare(const ::verilator_utils::format& format)
    {
        constexpr static auto bin_index{
            ::verilator_utils::variant_type_index<::verilator_utils::data_format::bin_t, ::verilator_utils::format>};
        VU_CHECK(format.index() > bin_index, "十六进制和二进制格式不支持三路比较，只支持相等比较"sv);
        VU_CHECK(!::std::holds_alternative<::verilator_utils::data_format::boolean_t>(format),
                 "布尔型不支持三路比较，只支持相等比较"sv);
    }

    /**
     * @brief 在C++基本数据类型间进行三路比较
     *
     * @tparam lhs_t 左操作数类型
     * @tparam rhs_t 右操作数类型
     * @param lhs 左操作数
     * @param rhs 右操作数
     * @return 比较结果
     */
    template <::verilator_utils::is_cpp_underlying_type lhs_t, ::verilator_utils::is_cpp_underlying_type rhs_t>
    auto three_way_compare_underlying(lhs_t lhs, rhs_t rhs) noexcept
    {
        // bool不支持三路比较
        if constexpr(::verilator_utils::same_as_any<bool, lhs_t, rhs_t>)
        {
            ::std::unreachable();
            return ::std::partial_ordering::unordered;
        }
        else if constexpr(::std::integral<lhs_t> && ::std::integral<rhs_t>)
        {
            if(::std::cmp_less(lhs, rhs)) { return ::std::strong_ordering::less; }
            if(::std::cmp_greater(lhs, rhs)) { return ::std::strong_ordering::greater; }
            return ::std::strong_ordering::equivalent;
        }
        else
        {
            return lhs <=> rhs;
        }
    }
}  // namespace verilator_utils::detail

export namespace verilator_utils
{
    /**
     * @brief 检查类型是否为格式包装器
     *
     * @tparam type 要检查的类型
     */
    template <typename type>
    concept is_format_wrapper = ::verilator_utils::detail::is_format_wrapper_impl<type>;

    /**
     * @brief 检查类型是否为位切片
     *
     * @tparam type 要检查的类型
     */
    template <typename type>
    concept is_bit_slice = ::verilator_utils::detail::is_bit_slice_impl<type>;

    /**
     * @brief 检查类型是否为向量切片
     *
     * @tparam type 要检查的类型
     */
    template <typename type>
    concept is_vector_slice = ::verilator_utils::detail::is_vector_slice_impl<type>;

    /**
     * @brief 检查类型是否为unpacked数组
     *
     * @tparam type 要检查的类型
     */
    template <typename type>
    concept is_unpacked_array = ::verilator_utils::detail::is_unpacked_array_impl<type>;

    /// 打包储存的格式，包含宽度和数据格式
    using packed_format = ::std::pair<::std::size_t, ::verilator_utils::data_format::format>;

    /**
     * @brief 格式包装器，将数据和数据格式绑定
     * 用于在测试激励中构造带有格式的数据
     * @note 支持作为常量表达式使用
     * @tparam type 数据类型
     */
    template <::verilator_utils::is_format_wrapper_data_type type>
    struct format_wrapper
    {
    private:
        /// 是否为Verilator宽数据类型
        constexpr static bool is_vl_wide{::verilator_utils::is_vl_wide<type>};
        using to_verilator_t = ::std::conditional_t<is_vl_wide, const type&, ::std::uint64_t>;
        using set_value_t = ::std::conditional_t<is_vl_wide, const type&, type>;

    public:
        using value_type = type;

        /**
         * @brief 构造格式包装器对象
         *
         * @param value 数据
         * @param width 数据宽度
         * @param format 数据格式
         */
        constexpr format_wrapper(type value,
                                 ::std::size_t width,
                                 ::verilator_utils::data_format::format format = ::verilator_utils::data_format::hex) :
            underlying_value{value}, data_width{width}, data_format{::std::move(format)}
        {
            constexpr static auto max{sizeof(value_type) * ::std::numeric_limits<::std::uint8_t>::digits};
            VU_CHECK(width <= max, "数据宽度{}超出上限{}"sv, width, max);
            check_format();
            check_value();
        }

        /**
         * @brief 构造格式包装器对象
         *
         * @param value 数据
         * @param packed_format 打包在std::pair中的格式
         */
        constexpr format_wrapper(type value, ::verilator_utils::packed_format packed_format) :
            format_wrapper{value, packed_format.first, packed_format.second}
        {
        }

        constexpr format_wrapper(const format_wrapper&) noexcept = default;
        constexpr format_wrapper& operator= (const format_wrapper&) noexcept = default;
        constexpr format_wrapper(format_wrapper&&) noexcept = default;
        constexpr format_wrapper& operator= (format_wrapper&&) noexcept = default;
        constexpr ~format_wrapper() noexcept = default;

        /**
         * @brief 获取数据
         *
         * @return 数据引用
         */
        [[nodiscard]] constexpr const type& value() const noexcept { return underlying_value; }

        /**
         * @brief 获取位宽
         *
         * @return 数据位宽
         */
        [[nodiscard]] constexpr ::std::size_t width() const noexcept { return data_width; }

        /**
         * @brief 获取数据格式
         *
         * @return 数据格式
         */
        [[nodiscard]] constexpr const ::verilator_utils::data_format::format& format() const noexcept { return data_format; }

        /**
         * @brief 设置数据
         *
         * @param value 要设置的数据
         * @return 格式包装器引用
         */
        constexpr format_wrapper& operator= (set_value_t value)
        {
            underlying_value = value;
            check_value();
            return *this;
        }

        /**
         * @brief 将数据按格式转化为打包储存在std::uint64_t或VlWide中的数据
         *
         * @return 打包储存的数据，当type为VlWide时返回VlWide，其他时候返回std::uint64_t
         */
        [[nodiscard]] constexpr to_verilator_t to_verilator() const noexcept
        {
            if constexpr(is_vl_wide) { return underlying_value; }
            else
            {
                return data_format.visit([this]<typename format_t>(const format_t& format) noexcept -> ::std::uint64_t {
                    if constexpr(::std::same_as<format_t, ::std::monostate>)
                    {
                        ::std::unreachable();
                        return 0;
                    }
                    else if constexpr(requires() { format.to_verilator(underlying_value, width()); })
                    {
                        return format.to_verilator(underlying_value, width());
                    }
                    else
                    {
                        return format.to_verilator(underlying_value);
                    }
                });
            }
        }

        /**
         * @brief 将数据包装器格式化输出到缓冲区上
         *
         * @tparam iter_t 迭代器类型
         * @param iter 迭代器
         * @return 格式化后缓冲区迭代器
         */
        template <typename iter_t>
        iter_t format_to(iter_t iter) const
        {
            return data_format.visit([this, iter]<typename format_t>(const format_t& format) -> iter_t {
                if constexpr(requires() { format.format_to(iter, underlying_value, width()); })
                {
                    return format.format_to(iter, underlying_value, width());
                }
                else if constexpr(requires() { format.format_to(iter, underlying_value); })
                {
                    return format.format_to(iter, underlying_value);
                }
                else
                {
                    ::std::unreachable();
                    return iter;
                }
            });
        }

        /**
         * @brief 转换为字符串表示
         *
         * @return 字符串表示
         */
        [[nodiscard]] ::std::string to_string() const
        {
            ::std::string result{};
            // 0b和0x前缀的长度
            constexpr static auto prefix_size{2zu};
            if(::std::holds_alternative<::verilator_utils::data_format::hex_t>(data_format))
            {
                /// 每个十六进制位的位宽
                constexpr static ::std::size_t digit_width{4zu};
                auto total_len{(width() + digit_width - 1) / digit_width + prefix_size};
                result.reserve(total_len);
            }
            else if(::std::holds_alternative<::verilator_utils::data_format::bin_t>(data_format))
            {
                auto total_len{width() + prefix_size};
                result.reserve(total_len);
            }
            format_to(::std::back_inserter(result));
            return result;
        }

        /**
         * @brief 相等运算符
         *
         * @param self 要比较的值
         * @param other 要比较的值
         * @return 是否相等
         */
        template <::verilator_utils::is_format_wrapper_data_type other_type>
        constexpr friend bool operator== (const format_wrapper& self, const ::verilator_utils::format_wrapper<other_type>& other)
        {
            VU_CHECK(self.width() == other.width(), "数据宽度{}和{}不同"sv, self.width(), other.width());
            // 与三路比较保持一致的数值语义：整型（bool除外）使用std::cmp_equal避免混合符号的原始位比较
            constexpr static auto equal_values{
                []<typename lhs_t, typename rhs_t>(lhs_t lhs, rhs_t rhs) constexpr static noexcept {
                    if constexpr(::std::integral<lhs_t> && ::std::integral<rhs_t> &&
                                 !::verilator_utils::same_as_any<bool, lhs_t, rhs_t>)
                    {
                        return ::std::cmp_equal(lhs, rhs);
                    }
                    else
                    {
                        return lhs == rhs;
                    }
                }};
            constexpr static auto other_is_vl_wide{::verilator_utils::is_vl_wide<other_type>};
            if constexpr(is_vl_wide && other_is_vl_wide)
            {
                if consteval { return ::std::ranges::equal(self.value().m_storage, other.value().m_storage); }
                else
                {
                    return self.value() == other.value();
                }
            }
            else if constexpr(!is_vl_wide && !other_is_vl_wide) { return equal_values(self.value(), other.value()); }
            else if constexpr(!is_vl_wide && other_is_vl_wide) { return equal_values(self.value(), other.vl_wide_to_uint64()); }
            else
            {
                return equal_values(self.vl_wide_to_uint64(), other.value());
            }
        }

        /**
         * @brief 三路比较运算符
         *
         * @param self 要比较的值
         * @param other 要比较的值
         * @return 比较结果
         * @note 数据类型为VlWide的format_wrapper只支持十六进制和二进制，因此不能用于三路比较
         */
        template <::verilator_utils::is_format_wrapper_data_type other_type>
            requires (!is_vl_wide && !::verilator_utils::is_vl_wide<other_type>)
        constexpr friend auto operator<=> (const format_wrapper& self, const ::verilator_utils::format_wrapper<other_type>& other)
        {
            ::verilator_utils::detail::check_three_way_compare(self.format());
            ::verilator_utils::detail::check_three_way_compare(other.format());
            return ::verilator_utils::detail::three_way_compare_underlying(self.value(), other.value());
        }

    private:
        type underlying_value;
        ::std::size_t data_width;
        ::verilator_utils::data_format::format data_format;
        /// 每个字的位宽
        constexpr static ::std::size_t word_width{::std::numeric_limits<::EData>::digits};

        /**
         * @brief 检查格式是否合法
         *
         */
        constexpr void check_format()
        {

            auto is_monostate{::std::holds_alternative<::std::monostate>(data_format)};
            VU_CHECK(!is_monostate, "必须设定数据格式"sv);

            if constexpr(is_vl_wide)
            {
                constexpr static auto bin_index{::verilator_utils::variant_type_index<::verilator_utils::data_format::bin_t,
                                                                                      ::verilator_utils::data_format::format>};
                auto is_hex_or_bin{data_format.index() <= bin_index};
                VU_CHECK(is_hex_or_bin, "VlWide只支持十六进制和二进制格式"sv);
            }
            else if constexpr(::std::same_as<type, ::std::uint64_t>)
            {
                constexpr static auto dec_signed_index{
                    ::verilator_utils::variant_type_index<::verilator_utils::data_format::dec_signed_t,
                                                          ::verilator_utils::data_format::format>};
                constexpr static auto sign_mag_fixed_point_index{
                    ::verilator_utils::variant_type_index<::verilator_utils::data_format::sign_mag_fixed_point_t,
                                                          ::verilator_utils::data_format::format>};
                auto is_signed_or_floating_point_or_fixed_point{data_format.index() >= dec_signed_index &&
                                                                data_format.index() <= sign_mag_fixed_point_index};
                VU_CHECK(!is_signed_or_floating_point_or_fixed_point, "std::uint64_t不支持有符号十进制、浮点数和定点数格式"sv);
            }
            else if constexpr(::std::same_as<type, ::std::int64_t>)
            {
                auto is_dec_signed{::std::holds_alternative<::verilator_utils::data_format::dec_signed_t>(data_format)};
                VU_CHECK(is_dec_signed, "std::int64_t只支持有符号十进制格式"sv);
            }
            else if constexpr(::std::same_as<type, float>)
            {
                auto is_dec_signed{::std::holds_alternative<::verilator_utils::data_format::real_float_t>(data_format)};
                VU_CHECK(is_dec_signed, "float只支持单精度浮点数格式"sv);
            }
            else if constexpr(::std::same_as<type, double>)
            {
                constexpr static auto real_double_index{
                    ::verilator_utils::variant_type_index<::verilator_utils::data_format::real_double_t,
                                                          ::verilator_utils::data_format::format>};
                constexpr static auto sign_mag_fixed_point_index{
                    ::verilator_utils::variant_type_index<::verilator_utils::data_format::sign_mag_fixed_point_t,
                                                          ::verilator_utils::data_format::format>};
                auto is_double_or_fixed_point{data_format.index() >= real_double_index &&
                                              data_format.index() <= sign_mag_fixed_point_index};
                VU_CHECK(is_double_or_fixed_point, "double只支持双精度浮点数和定点数格式"sv);
            }
            else if constexpr(::std::same_as<type, bool>)
            {
                auto is_boolean{::std::holds_alternative<::verilator_utils::data_format::boolean_t>(data_format)};
                VU_CHECK(is_boolean, "bool只支持布尔型格式"sv);
            }
            else
            {
                static_assert(false, "未支持的格式");
            }

            ::verilator_utils::data_format::check_format(data_format, data_width);
        }

        /**
         * @brief 检查数据是否合法
         *
         */
        constexpr void check_value()
        {

            auto do_check{[this](::std::size_t value_width) constexpr {
                VU_CHECK(value_width <= width(), "数据宽度{}过大，不能超过{}"sv, value_width, width());
            }};
            if constexpr(is_vl_wide)
            {
                auto value_width{::verilator_utils::detail::vl_wide_width(underlying_value)};
                do_check(value_width);
            }
            else if constexpr(::std::same_as<::std::uint64_t, type>)
            {
                auto value_width{::std::bit_width(underlying_value)};
                do_check(value_width);
            }
            else if constexpr(::std::same_as<::std::int64_t, type>)
            {
                auto value_width{::verilator_utils::detail::signed_integral_width(underlying_value)};
                do_check(value_width);
            }
            else if constexpr(::verilator_utils::same_as_any<type, float, bool>)
            {
                // 标准单精度浮点类型固定32位，无需宽度检查
                // 标准bool类型固定1位，无需宽度检查
            }
            else if constexpr(::std::same_as<double, type>)
            {
                constexpr static auto unsigned_fixed_point_index{
                    ::verilator_utils::variant_type_index<::verilator_utils::data_format::unsigned_fixed_point_t,
                                                          ::verilator_utils::data_format::format>};
                constexpr static auto sign_mag_fixed_point_index{
                    ::verilator_utils::variant_type_index<::verilator_utils::data_format::sign_mag_fixed_point_t,
                                                          ::verilator_utils::data_format::format>};

                auto is_fixed_point{data_format.index() >= unsigned_fixed_point_index &&
                                    data_format.index() <= sign_mag_fixed_point_index};

                if(is_fixed_point)
                {
                    auto convert_to_verilator{data_format.visit([this](const auto& format) noexcept -> ::std::uint64_t {
                        if constexpr(requires() { format.to_verilator(underlying_value); })
                        {
                            return format.to_verilator(underlying_value);
                        }
                        else
                        {
                            ::std::unreachable();
                            return 0;
                        }
                    })};
                    auto convert_back{data_format.visit([convert_to_verilator](const auto& format) noexcept -> double {
                        if constexpr(requires() { format.to_underlying(convert_to_verilator); })
                        {
                            return format.to_underlying(convert_to_verilator);
                        }
                        else
                        {
                            ::std::unreachable();
                            return 0.0;
                        }
                    })};
                    VU_CHECK(convert_back == underlying_value, "当前数据包装器不能无修改的保存给定数据"sv);
                }
                else
                {
                    // 标准双精度浮点类型固定64位，无需宽度检查
                }
            }
            else
            {
                static_assert(false, "未支持的格式");
            }
        }

        template <::verilator_utils::is_format_wrapper_data_type other_type>
        friend struct format_wrapper;

        /**
         * @brief 将VlWide转化为std::uint64_t
         *
         */
        [[nodiscard]] constexpr ::std::uint64_t vl_wide_to_uint64() const noexcept
            requires (is_vl_wide && (type::Words == 1 || type::Words == 2))
        {
            if constexpr(type::Words == 1) { return underlying_value.m_storage[0]; }
            else
            {
                return static_cast<::std::uint64_t>(underlying_value.m_storage[1]) << word_width | underlying_value.m_storage[0];
            }
        }
    };

    /**
     * @brief 位切片
     *
     * @tparam type Verilator数据类型
     */
    template <::verilator_utils::is_verilator_data_type type>
    struct bit_slice
    {
        using value_type = type;

        /**
         * @brief 构造一个位切片对象
         *
         * @param data 数据引用
         * @param index 位索引
         * @param format 数据格式，只影响格式化输出，支持十六进制、二进制、十进制无符号、枚举和布尔型
         */
        explicit bit_slice(value_type& data,
                           ::std::size_t index,
                           ::verilator_utils::data_format::format format = ::verilator_utils::data_format::dec_unsigned) :
            data{data}, index{index}, data_format{::std::move(format)}
        {
            constexpr static auto max{sizeof(value_type) * ::std::numeric_limits<::std::uint8_t>::digits};
            VU_CHECK(index < max, "位索引{}超出上限{}"sv, index, max);
            check_format();
        }

        /**
         * @brief 从数据引用的最低位构造一个位切片对象
         *
         * @param data 数据引用
         * @param format 数据格式，只影响格式化输出，支持十六进制、二进制、十进制无符号、枚举和布尔型
         */
        explicit bit_slice(value_type& data,
                           ::verilator_utils::data_format::format format = ::verilator_utils::data_format::dec_unsigned) :
            bit_slice{data, 0, format}
        {
        }

        /**
         * @brief 赋值运算符
         *
         * @param value 要赋值的值
         * @return bool_wrapper& 赋值后对象的引用
         */
        bit_slice& operator= (::std::uint64_t value)
        {
            VU_CHECK(value <= 1, "位包装器只能赋值0或1"sv);
            if constexpr(is_vl_wide)
            {
                auto word_index{index / word_width};
                auto index_in_word{index % word_width};
                auto mask{::EData{1} << index_in_word};
                data[word_index] = (data[word_index] & ~mask) | (value << index_in_word);
            }
            else
            {
                auto mask{1zu << index};
                data = (data & ~mask) | (value << index);
            }
            return *this;
        }

        /**
         * @brief 转换为std::uint64_t类型
         *
         * @return 转换后的整数值
         */
        operator ::std::uint64_t () const noexcept  // NOLINT(*-explicit-constructor)
        {
            if constexpr(is_vl_wide)
            {
                auto word_index{index / word_width};
                auto index_in_word{index % word_width};
                auto mask{::EData{1} << index_in_word};
                return (data[word_index] & mask) >> index_in_word;
            }
            else
            {
                auto mask{value_type{1} << index};
                return (data & mask) >> index;
            }
        }

        /**
         * @brief 赋值运算符
         *
         * @param value 要赋值的值
         * @return bool_wrapper& 赋值后对象的引用
         */
        template <::verilator_utils::same_as_any<::std::uint64_t, bool> underlying_type>
        bit_slice& operator= (const ::verilator_utils::format_wrapper<underlying_type>& value)
        {
            VU_CHECK(value.width() == 1, "期待宽度为1，实际宽度为{}"sv, value.width());
            return *this = value.to_verilator();
        }

        /**
         * @brief 相等运算符
         *
         * @param self 要比较的值
         * @param value 要比较的值
         * @return 是否相等
         */
        template <::verilator_utils::same_as_any<::std::uint64_t, bool> underlying_type>
        friend bool operator== (const bit_slice& self, const ::verilator_utils::format_wrapper<underlying_type>& value)
        {
            VU_CHECK(value.width() == 1, "期待宽度为1，实际宽度为{}"sv, value.width());
            return static_cast<::std::uint64_t>(self) == value.to_verilator();
        }

        /**
         * @brief 相等运算符
         *
         * @param self 要比较的值
         * @param other 要比较的值
         * @return 是否相等
         */
        template <::verilator_utils::is_verilator_data_type other_type>
        friend bool operator== (const bit_slice& self, const ::verilator_utils::bit_slice<other_type>& other)
        { return static_cast<::std::uint64_t>(self) == static_cast<::std::uint64_t>(other); }

        /**
         * @brief 三路比较运算符
         *
         * @param other 要比较的值
         * @return 比较结果
         * @note 数据类型为VlWide的bit_slice只支持十六进制和二进制，因此不能用于三路比较
         */
        template <::verilator_utils::is_verilator_data_type other_type>
        friend ::std::strong_ordering operator<=> (const bit_slice& self, const ::verilator_utils::bit_slice<other_type>& other)
        {
            ::verilator_utils::detail::check_three_way_compare(self.format());
            ::verilator_utils::detail::check_three_way_compare(other.format());
            return static_cast<::std::uint64_t>(self) <=> static_cast<::std::uint64_t>(other);
        }

        /**
         * @brief 三路比较运算符
         *
         * @param self 要比较的值
         * @param value 要比较的值
         * @return 比较结果
         */
        friend ::std::strong_ordering operator<=> (const bit_slice& self,
                                                   const ::verilator_utils::format_wrapper<::std::uint64_t>& value)
        {
            ::verilator_utils::detail::check_three_way_compare(self.data_format);
            ::verilator_utils::detail::check_three_way_compare(value.format());
            VU_CHECK(value.width() == 1, "期待宽度为1，实际宽度为{}"sv, value.width());
            return static_cast<::std::uint64_t>(self) <=> value.value();
        }

        /**
         * @brief 获取位宽
         *
         * @return std::size_t 位宽
         */
        [[nodiscard]] constexpr ::std::size_t width() const noexcept { return 1; }

        /**
         * @brief 获取数据类型
         *
         * @return 数据类型
         */
        [[nodiscard]] ::verilator_utils::data_format::format format() const noexcept { return data_format; }

        /**
         * @brief 将数据包装器格式化输出到缓冲区上
         *
         * @tparam iter_t 迭代器类型
         * @param iter 迭代器
         * @return 格式化后缓冲区迭代器
         */
        template <typename iter_t>
        iter_t format_to(iter_t iter) const
        {
            return data_format.visit([this, iter]<typename format_t>(const format_t& format) -> iter_t {
                auto aligned_value{static_cast<::std::uint64_t>(*this)};
                using namespace ::verilator_utils::data_format;
                if constexpr(::verilator_utils::same_as_any<format_t, hex_t, bin_t>)
                {
                    return format.format_to(iter, aligned_value, width());
                }
                else if constexpr(::verilator_utils::same_as_any<format_t, dec_unsigned_t, fsm_enum_t, boolean_t>)
                {
                    return format.format_to(iter, aligned_value);
                }
                else
                {
                    ::std::unreachable();
                    return iter;
                }
            });
        }

        /**
         * @brief 转换为字符串表示
         *
         * @return 字符串表示
         */
        [[nodiscard]] ::std::string to_string() const
        {
            // 格式化输出字符较少，一般小于sso容量，因此不进行预留
            ::std::string result{};
            format_to(::std::back_inserter(result));
            return result;
        }

        /**
         * @brief 将当前位切片的值和格式转化为格式包装器对象
         *
         * @tparam underlying_type 数据类型，需要和格式兼容
         * @return 格式包装器对象
         */
        template <::verilator_utils::same_as_any<::std::uint64_t, bool> underlying_type = ::std::uint64_t>
        [[nodiscard]] ::verilator_utils::format_wrapper<underlying_type> dump() const
        { return ::verilator_utils::format_wrapper{static_cast<underlying_type>(*this), width(), format()}; }

        /**
         * @brief 打包当前位切片的格式
         *
         * @return 打包的格式
         */
        [[nodiscard]] ::verilator_utils::packed_format dump_format() const noexcept
        { return ::verilator_utils::packed_format{width(), format()}; }

    private:
        /// 数据引用
        value_type& data;
        /// 位索引
        ::std::size_t index;
        /// 数据格式
        ::verilator_utils::data_format::format data_format;
        /// 每个字的位宽
        constexpr static ::std::size_t word_width{::std::numeric_limits<::EData>::digits};
        /// 是否为Verilator宽数据类型
        constexpr static bool is_vl_wide{::verilator_utils::is_vl_wide<type>};
        constexpr static auto hex_index{
            ::verilator_utils::variant_type_index<::verilator_utils::data_format::hex_t, ::verilator_utils::data_format::format>};
        constexpr static auto dec_unsigned_index{
            ::verilator_utils::variant_type_index<::verilator_utils::data_format::dec_unsigned_t,
                                                  ::verilator_utils::data_format::format>};

        /**
         * @brief 检查格式是否合法
         *
         */
        void check_format() const
        {
            VU_CHECK(!::std::holds_alternative<::std::monostate>(data_format), "必须设定数据格式"sv);
            auto is_hex_bin_unsigned{data_format.index() >= hex_index && data_format.index() <= dec_unsigned_index};
            auto is_enum{::std::holds_alternative<::verilator_utils::data_format::fsm_enum_t>(data_format)};
            auto is_boolean{::std::holds_alternative<::verilator_utils::data_format::boolean_t>(data_format)};
            VU_CHECK((is_hex_bin_unsigned || is_enum || is_boolean),
                     "位切片只支持十六进制、二进制、十进制无符号、枚举和布尔型格式"sv);
        }
    };

    /**
     * @brief 向量切片
     *
     * @tparam type Verilator数据类型
     */
    template <::verilator_utils::is_verilator_data_type type>
    struct vector_slice
    {
        using value_type = type;

    private:
        /// 是否为Verilator宽数据类型
        constexpr static bool is_vl_wide{::verilator_utils::is_vl_wide<type>};

    public:
        /// 可转换的目标类型
        using cast_type = std::conditional_t<is_vl_wide, value_type, ::std::uint64_t>;
        /// 能转换到的C++基础数据类型
        using underlying_type = ::std::variant<::std::uint64_t, ::std::int64_t, float, double, bool>;

        /**
         * @brief 构造一个向量切片对象，索引为闭区间
         *
         * @param data 数据引用
         * @param left_bound_index 索引上界
         * @param right_bound_index 索引下界
         * @param format 数据格式
         */
        explicit vector_slice(value_type& data,
                              ::std::size_t left_bound_index,
                              ::std::size_t right_bound_index,
                              ::verilator_utils::data_format::format format = ::verilator_utils::data_format::hex) :
            data{data}, left_bound{left_bound_index}, right_bound{right_bound_index}, data_format{::std::move(format)}
        {
            constexpr static auto max{sizeof(value_type) * ::std::numeric_limits<::std::uint8_t>::digits};
            VU_CHECK(left_bound >= right_bound, "切片上界{}不能小于下界{}"sv, left_bound, right_bound);
            VU_CHECK(left_bound_index < max, "切片上界{}超出上限{}"sv, left_bound_index, max);
            ::verilator_utils::data_format::check_format(data_format, width());
        }

        /**
         * @brief 构造一个向量切片对象
         *
         * @param data 数据引用
         * @param width 宽度
         * @param format 数据格式
         */
        explicit vector_slice(value_type& data,
                              ::std::size_t width,
                              ::verilator_utils::data_format::format format = ::verilator_utils::data_format::hex) :
            data{data}, left_bound{width - 1}, right_bound{0}, data_format{::std::move(format)}
        {
            VU_CHECK(width != 0, "切片宽度不能为0，实际为{}"sv, width);
            ::verilator_utils::data_format::check_format(data_format, width);
        }

        vector_slice(const vector_slice&) = default;
        vector_slice(vector_slice&&) = default;
        vector_slice& operator= (vector_slice&&) = default;
        ~vector_slice() = default;

        /**
         * @brief 获取位宽
         *
         * @return 位宽
         */
        [[nodiscard]] constexpr ::std::size_t width() const noexcept { return left_bound - right_bound + 1; }

        /**
         * @brief 获取数据类型
         *
         * @return 数据类型
         */
        [[nodiscard]] constexpr ::verilator_utils::data_format::format format() const noexcept { return data_format; }

        /**
         * @brief 下标运算符，用于访问向量切片的指定位
         *
         * 索引为当前切片范围内的相对索引，范围为[0, width() - 1]
         * @param index 索引值
         * @return bit_slice 对应位的包装对象
         */
        bit_slice<type> operator[] (::std::size_t index) const
        {
            VU_CHECK(index <= width() - 1, "位索引{}超出切片宽度{}"sv, index, width());
            return bit_slice<type>{data, index + right_bound};
        }

        /**
         * @brief 下标运算符，用于访问向量切片的指定切片
         *
         * 索引为当前切片范围内的相对索引，范围为[0, width() - 1]
         * @param left_bound_index 左边界索引
         * @param right_bound_index 右边界索引
         * @param format 数据格式，为std::monostate表示使用当前对象的数据格式
         * @return 向量切片的包装对象
         */
        vector_slice
            operator[] (::std::size_t left_bound_index,
                        ::std::size_t right_bound_index,
                        const ::verilator_utils::data_format::format& format = ::verilator_utils::data_format::format{}) const
        {
            VU_CHECK(left_bound_index >= right_bound_index, "切片上界{}不能小于下界{}"sv, left_bound_index, right_bound_index);
            VU_CHECK(right_bound_index <= left_bound, "切片下界{}超出切片上界{}"sv, right_bound_index, left_bound);
            VU_CHECK(left_bound_index <= width() - 1, "切片上界{}超出切片宽度{}"sv, left_bound_index, width());
            if(!::verilator_utils::detail::is_variable_width_format(data_format) &&
               left_bound_index - right_bound_index + 1 != width())
            {
                VU_CHECK(!::std::holds_alternative<::std::monostate>(format),
                         "当前对象的数据格式是固定宽度的，必须传入新的格式才能创建不同宽度的切片"sv);
            }
            return vector_slice{
                data,
                left_bound_index + right_bound,
                right_bound_index + right_bound,
                ::std::holds_alternative<::std::monostate>(format) ? data_format : format,
            };
        }

        /**
         * @brief 转化为值类型
         *
         * @return 向量切片的对应的值
         */
        operator cast_type() const noexcept  // NOLINT(*-explicit-constructor)
        {
            if constexpr(is_vl_wide)
            {
                value_type result{};
                auto result_words{(width() + word_width - 1u) / word_width};
                for(::std::size_t result_word_index{}; result_word_index < result_words; ++result_word_index)
                {
                    auto source_bit_index{right_bound + result_word_index * word_width};
                    auto source_word_index{source_bit_index / word_width};
                    auto source_index_in_word{source_bit_index % word_width};
                    result.at(result_word_index) = data.at(source_word_index) >> source_index_in_word;
                    if(source_index_in_word != 0 && source_word_index + 1 < data.size())
                    {
                        result.at(result_word_index) |= data.at(source_word_index + 1) << (word_width - source_index_in_word);
                    }
                }
                auto bits_in_top_word{width() % word_width};
                if(bits_in_top_word != 0) { result.at(result_words - 1) &= (::EData{1} << bits_in_top_word) - 1u; }
                return result;
            }
            else
            {
                auto shift_left{63 - left_bound};
                auto shift_right{right_bound + shift_left};
                return static_cast<::std::uint64_t>(data) << shift_left >> shift_right;
            }
        }

        /**
         * @brief 赋值运算符
         *
         * @param value 要赋值的值
         * @return 赋值后对象的引用
         */
        vector_slice& operator= (::std::uint64_t value)
        {
            VU_CHECK(width() <= 64, "向量宽度{}不能超过64位"sv, width());
            auto width_is_enough{width() == 64 || (value >> width()) == 0};
            VU_CHECK(width_is_enough, "值宽度超出向量宽度"sv);
            if constexpr(is_vl_wide)
            {
                value_type temp{};
                temp.at(0) = static_cast<::EData>(value);
                if constexpr(value_type::Words > 1) { temp.at(1) = static_cast<::EData>(value >> word_width); }
                assign_aligned_value(temp);
            }
            else
            {
                auto mask{scalar_mask(width(), right_bound)};
                data = (data & ~mask) | (value << right_bound);
            }
            return *this;
        }

        /**
         * @brief 赋值运算符
         *
         * @param other 要赋值的值
         * @return 赋值后对象的引用
         */
        vector_slice& operator= (const vector_slice& other)
        {
            VU_CHECK(width() == other.width(), "切片宽度{}与赋值源宽度{}不同"sv, width(), other.width());
            auto aligned_value{static_cast<cast_type>(other)};
            if constexpr(is_vl_wide) { assign_aligned_value(aligned_value); }
            else
            {
                auto mask{scalar_mask(width(), right_bound)};
                data = (data & ~mask) | (aligned_value << right_bound);
            }
            return *this;
        }

        /**
         * @brief 赋值运算符
         *
         * @param value 要赋值的值
         * @return bool_wrapper& 赋值后对象的引用
         */
        template <::verilator_utils::is_format_wrapper_data_type underlying_type>
            requires (is_vl_wide || !::verilator_utils::is_vl_wide<underlying_type>)
        vector_slice& operator= (const ::verilator_utils::format_wrapper<underlying_type>& value)
        {
            VU_CHECK(width() == value.width(), "切片宽度{}与赋值源宽度{}不同"sv, width(), value.width());
            return *this = value.to_verilator();
        }

        /**
         * @brief 相等运算符
         *
         * @param self 要比较的值
         * @param value 要比较的值
         * @return 是否相等
         */
        friend bool operator== (const vector_slice& self, ::std::uint64_t value)
        {
            VU_CHECK(self.width() <= 64, "向量宽度{}不能超过64位"sv, self.width());
            auto width_is_enough{self.width() == 64 || (value >> self.width()) == 0};
            VU_CHECK(width_is_enough, "值宽度超出向量宽度"sv);
            auto temp{static_cast<cast_type>(self)};
            if constexpr(is_vl_wide) { return wide_to_uint64(temp) == value; }
            else
            {
                return temp == value;
            }
        }

        /**
         * @brief 相等运算符
         *
         * @param self 要比较的值
         * @param value 要比较的值
         * @return 是否相等
         */
        friend bool operator== (const vector_slice& self, const type& value)
            requires (is_vl_wide)
        {
            auto temp{static_cast<cast_type>(self)};
            auto value_width{::verilator_utils::detail::vl_wide_width(value)};
            VU_CHECK(self.width() >= value_width, "切片宽度{}小于值宽度{}"sv, self.width(), value_width);
            return temp == value;
        }

        /**
         * @brief 相等运算符
         *
         * @param self 要比较的值
         * @param value 要比较的值
         * @return 是否相等
         */
        template <::verilator_utils::is_format_wrapper_data_type underlying_type>
        friend bool operator== (const vector_slice& self, const ::verilator_utils::format_wrapper<underlying_type>& value)
        { return self == value.to_verilator(); }

        /**
         * @brief 相等运算符
         *
         * @param self 要比较的值
         * @param value 要比较的值
         * @return 是否相等
         */
        template <::verilator_utils::is_format_wrapper_data_type underlying_type>
        friend bool operator== (const vector_slice& self, const ::verilator_utils::bit_slice<underlying_type>& value)
        { return self == static_cast<::std::uint64_t>(value); }

        /**
         * @brief 三路比较运算符
         *
         * @param self 要比较的值
         * @param value 要比较的值
         * @return 比较结果，由于潜在的浮点比较，因此退化为std::partial_ordering
         */
        template <::verilator_utils::is_cpp_underlying_type underlying_type>
            requires (!::std::same_as<bool, underlying_type>)
        friend ::std::partial_ordering operator<=> (const vector_slice& self, underlying_type value)
        {
            ::verilator_utils::detail::check_three_way_compare(self.data_format);
            return self.to_underlying().visit([value](auto underlying_value) noexcept -> ::std::partial_ordering {
                return ::verilator_utils::detail::three_way_compare_underlying(underlying_value, value);
            });
        }

        /**
         * @brief 三路比较运算符
         *
         * @param self 要比较的值
         * @param value 要比较的值
         * @return 比较结果，由于潜在的浮点比较，因此退化为std::partial_ordering
         * @note 数据类型为VlWide的format_wrapper只支持十六进制和二进制，因此不能用于三路比较
         */
        template <::verilator_utils::is_cpp_underlying_type underlying_type>
            requires (!::std::same_as<bool, underlying_type>)
        friend ::std::partial_ordering operator<=> (const vector_slice& self,
                                                    const ::verilator_utils::format_wrapper<underlying_type>& value)
        { return self <=> value.value(); }

        /**
         * @brief 三路比较运算符
         *
         * @param self 要比较的值
         * @param other 要比较的值
         * @return 比较结果，由于潜在的浮点比较，因此退化为std::partial_ordering
         */
        template <::verilator_utils::is_verilator_data_type other_type>
        friend ::std::partial_ordering operator<=> (const vector_slice& self,
                                                    const ::verilator_utils::vector_slice<other_type>& other)
        {
            ::verilator_utils::detail::check_three_way_compare(self.format());
            ::verilator_utils::detail::check_three_way_compare(other.format());
            return self.to_underlying().visit([other_to_underlying = other.to_underlying()](
                                                  auto self_underlying_value) noexcept -> ::std::partial_ordering {
                return other_to_underlying.visit([self_underlying_value](
                                                     auto other_underlying_value) noexcept -> ::std::partial_ordering {
                    return ::verilator_utils::detail::three_way_compare_underlying(self_underlying_value, other_underlying_value);
                });
            });
        }

        /**
         * @brief 三路比较运算符
         *
         * @param self 要比较的值
         * @param other 要比较的值
         * @return 比较结果，由于潜在的浮点比较，因此退化为std::partial_ordering
         */
        template <::verilator_utils::is_verilator_data_type other_type>
        friend ::std::partial_ordering operator<=> (const vector_slice& self,
                                                    const ::verilator_utils::bit_slice<other_type>& other)
        {
            ::verilator_utils::detail::check_three_way_compare(other.format());
            return self <=> static_cast<::std::uint64_t>(other);
        }

        /**
         * @brief 赋值运算符, 从其他向量切片赋值
         *
         * @tparam other_type 其他向量切片的类型
         * @param other 其他向量切片对象
         * @return 赋值后对象的引用
         */
        template <::verilator_utils::is_verilator_data_type other_type>
        vector_slice& operator= (const ::verilator_utils::vector_slice<other_type>& other)
        {
            VU_CHECK(width() == other.width(), "切片宽度{}与赋值源宽度{}不同"sv, width(), other.width());
            auto aligned_value{static_cast<::verilator_utils::vector_slice<other_type>::cast_type>(other)};
            if constexpr(is_vl_wide)
            {
                value_type temp{};
                if constexpr(::verilator_utils::is_vl_wide<other_type>)
                {
                    ::std::ranges::copy_n(aligned_value.data(), aligned_value.size(), temp.data());
                }
                else
                {
                    temp.at(0) = static_cast<::EData>(aligned_value);
                    if constexpr(value_type::Words > 1) { temp.at(1) = static_cast<::EData>(aligned_value >> word_width); }
                }
                assign_aligned_value(temp);
            }
            else
            {
                auto mask{scalar_mask(width(), right_bound)};
                if constexpr(::verilator_utils::is_vl_wide<other_type>)
                {
                    data = (data & ~mask) | (wide_to_uint64(aligned_value) << right_bound);
                }
                else
                {
                    data = (data & ~mask) | (aligned_value << right_bound);
                }
            }
            return *this;
        }

        vector_slice& operator= (const value_type& value)
            requires (is_vl_wide)
        {
            assign_aligned_value(value);
            return *this;
        }

        /**
         * @brief 相等运算符, 和其他向量切片进行相等比较
         *
         * @tparam other_type 其他向量切片的类型
         * @param other 其他向量切片对象
         * @return 是否相等
         */
        template <::verilator_utils::is_verilator_data_type other_type>
        friend bool operator== (const vector_slice& self, const ::verilator_utils::vector_slice<other_type>& other)
        {
            VU_CHECK(self.width() == other.width(), "切片宽度{}与赋值源宽度{}不同"sv, self.width(), other.width());
            auto aligned_other{static_cast<::verilator_utils::vector_slice<other_type>::cast_type>(other)};
            if constexpr(is_vl_wide)
            {
                auto aligned_value{static_cast<cast_type>(self)};
                auto words{(self.width() + word_width - 1u) / word_width};
                if constexpr(::verilator_utils::is_vl_wide<other_type>)
                {
                    return ::std::memcmp(aligned_value.data(), aligned_other.data(), words * sizeof(::EData)) == 0;
                }
                else
                {
                    return ::std::memcmp(aligned_value.data(), &aligned_other, words * sizeof(::EData)) == 0;
                }
            }
            else
            {
                auto mask{scalar_mask(self.width(), self.right_bound)};
                if constexpr(::verilator_utils::is_vl_wide<other_type>)
                {
                    return (self.data & mask) == (wide_to_uint64(aligned_other) << self.right_bound);
                }
                else
                {
                    return (self.data & mask) == (aligned_other << self.right_bound);
                }
            }
        }

        /**
         * @brief 转化为具有给定数据类型的切片对象
         *
         * @param format 数据类型
         * @return 新切片对象
         */
        vector_slice convert(::verilator_utils::data_format::format format)
        {
            ::verilator_utils::data_format::check_format(data_format, width());
            return vector_slice{data, left_bound, right_bound, format};
        }

        /**
         * @brief 转化为C++基础数据类型
         *
         * 数据格式与基础类型的对应关系为：
         * | 格式                   | 基础类型      |
         * | ---------------------- | ------------- |
         * | hex_t                  | std::uint64_t |
         * | bin_t                  | std::uint64_t |
         * | dec_unsigned_t         | std::uint64_t |
         * | dec_signed_t           | std::int64_t  |
         * | real_float_t           | float         |
         * | real_double_t          | double        |
         * | unsigned_fixed_point_t | double        |
         * | signed_fixed_point_t   | double        |
         * | sign_mag_fixed_point_t | double        |
         * | fsm_enum_t             | std::uint64_t |
         * | boolean_t              | bool          |
         * @return C++基础数据类型
         */
        [[nodiscard]] underlying_type to_underlying() const
        {
            VU_CHECK(width() <= 64, "向量宽度{}不能超过64位"sv, width());
            ::std::uint64_t aligned_value{};
            if constexpr(is_vl_wide) { aligned_value = wide_to_uint64(static_cast<cast_type>(*this)); }
            else
            {
                aligned_value = static_cast<::std::uint64_t>(*this);
            }

            return data_format.visit(
                [aligned_value, width = width()]<typename format_t>(const format_t& format) noexcept -> underlying_type {
                    if constexpr(::std::same_as<format_t, ::std::monostate>)
                    {
                        ::std::unreachable();
                        return underlying_type{};
                    }
                    else
                    {
                        if constexpr(requires() { format.to_underlying(aligned_value, width); })
                        {
                            return underlying_type{format.to_underlying(aligned_value, width)};
                        }
                        else
                        {
                            return underlying_type{format.to_underlying(aligned_value)};
                        }
                    }
                });
        }

        /**
         * @brief 检查向量切片的值是否有效
         *
         * @note 对于枚举等有效值范围可能小于取值范围的格式进行检查，否则总是返回true
         * @return 向量切片的值是否有效
         */
        [[nodiscard]] bool is_valid() const noexcept
        {
            return data_format.visit([this]<typename format_t>(const format_t& format) noexcept -> bool {
                if constexpr(::std::same_as<format_t, ::verilator_utils::data_format::fsm_enum_t>)
                {
                    auto underlying_data{::std::get<::std::uint64_t>(to_underlying())};
                    return underlying_data < format.enum_string.size();
                }
                else
                {
                    return true;
                }
            });
        }

        /**
         * @brief 将向量切片格式化输出到缓冲区上
         *
         * @tparam iter_t 迭代器类型
         * @param iter 迭代器
         * @return 格式化后缓冲区迭代器
         */
        template <typename iter_t>
        iter_t format_to(iter_t iter) const
        {
            return data_format.visit([this, iter]<typename format_t>(const format_t& format) -> iter_t {
                using namespace ::verilator_utils::data_format;
                if constexpr(::std::same_as<format_t, ::std::monostate>)
                {
                    ::std::unreachable();
                    return iter;
                }
                else if constexpr(::verilator_utils::same_as_any<format_t, hex_t, bin_t>)
                {
                    auto aligned_value{static_cast<cast_type>(*this)};
                    return format.format_to(iter, aligned_value, width());
                }
                else
                {
                    return to_underlying().visit(
                        [iter, &format](auto underlying_data) -> iter_t { return format.format_to(iter, underlying_data); });
                }
            });
        }

        /**
         * @brief 转换为字符串表示
         *
         * @return 字符串表示
         */
        [[nodiscard]] ::std::string to_string() const
        {
            ::std::string result{};
            // 0b和0x前缀的长度
            constexpr static auto prefix_size{2zu};
            if(::std::holds_alternative<::verilator_utils::data_format::hex_t>(data_format))
            {
                /// 每个十六进制位的位宽
                constexpr static ::std::size_t digit_width{4zu};
                auto total_len{(width() + digit_width - 1) / digit_width + prefix_size};
                result.reserve(total_len);
            }
            else if(::std::holds_alternative<::verilator_utils::data_format::bin_t>(data_format))
            {
                auto total_len{width() + prefix_size};
                result.reserve(total_len);
            }
            format_to(::std::back_inserter(result));
            return result;
        }

        /**
         * @brief 将当前向量切片的值和格式转化为格式包装器对象
         *
         * @tparam underlying_type
         * 数据类型，需要和格式兼容。对于VlWide的切片，underlying_type默认为VlWide，否则默认为std::uint64_t
         * @return 格式包装器对象
         */
        template <::verilator_utils::is_format_wrapper_data_type underlying_type =
                      ::std::conditional_t<is_vl_wide, value_type, ::std::uint64_t>>
            requires (!::verilator_utils::is_vl_wide<underlying_type> || ::std::same_as<underlying_type, value_type>)
        [[nodiscard]] ::verilator_utils::format_wrapper<underlying_type> dump() const
        {
            if constexpr(::verilator_utils::is_vl_wide<underlying_type>)
            {
                return ::verilator_utils::format_wrapper{static_cast<value_type>(*this), width(), format()};
            }
            else
            {
                auto underlying_value{to_underlying()};
                auto* ptr{::std::get_if<underlying_type>(&underlying_value)};
                VU_CHECK(ptr != nullptr, "当前切片对象绑定的格式与设定的underlying_type不兼容"sv);
                return ::verilator_utils::format_wrapper{*ptr, width(), format()};
            }
        }

        /**
         * @brief 打包当前位切片的格式
         *
         * @return 打包的格式
         */
        [[nodiscard]] ::verilator_utils::packed_format dump_format() const noexcept
        { return ::verilator_utils::packed_format{width(), format()}; }

    private:
        /// 每个字的位宽
        constexpr static ::std::size_t word_width{::std::numeric_limits<::EData>::digits};
        /// 数据引用
        value_type& data;
        /// 左边界索引
        ::std::size_t left_bound;
        /// 右边界索引
        ::std::size_t right_bound;
        /// 数据类型
        ::verilator_utils::data_format::format data_format;

        constexpr static ::std::uint64_t scalar_mask(::std::size_t width, ::std::size_t right_bound) noexcept
        {
            auto lower_mask{width == 64 ? ::std::numeric_limits<::std::uint64_t>::max() : (::std::uint64_t{1} << width) - 1u};
            return lower_mask << right_bound;
        }

        template <typename wide_type>
        constexpr static ::std::uint64_t wide_to_uint64(const wide_type& value) noexcept
        {
            ::std::uint64_t result{value.at(0)};
            if constexpr(wide_type::Words > 1) { result |= static_cast<::std::uint64_t>(value.at(1)) << word_width; }
            return result;
        }

        void assign_aligned_value(const value_type& aligned_value) noexcept
            requires (is_vl_wide)
        {
            auto words{(width() + word_width - 1u) / word_width};
            for(auto word_index{0zu}; word_index < words; ++word_index)
            {
                auto destination_bit_index{right_bound + word_index * word_width};
                auto destination_word_index{destination_bit_index / word_width};
                auto destination_index_in_word{destination_bit_index % word_width};
                auto bits_to_write{::std::min(word_width, width() - word_index * word_width)};
                auto value{aligned_value.at(word_index)};
                if(bits_to_write != word_width) { value &= (::EData{1} << bits_to_write) - 1u; }
                auto lower_mask{bits_to_write == word_width ? ::std::numeric_limits<::EData>::max()
                                                            : (::EData{1} << bits_to_write) - 1u};
                auto destination_mask{lower_mask << destination_index_in_word};
                data.at(destination_word_index) =
                    (data.at(destination_word_index) & ~destination_mask) | (value << destination_index_in_word);
                if(destination_index_in_word != 0 && destination_word_index + 1 < data.size())
                {
                    auto high_bits{bits_to_write > word_width - destination_index_in_word
                                       ? bits_to_write - (word_width - destination_index_in_word)
                                       : 0u};
                    if(high_bits != 0)
                    {
                        auto high_mask{(::EData{1} << high_bits) - 1u};
                        data.at(destination_word_index + 1) = (data.at(destination_word_index + 1) & ~high_mask) |
                                                              (value >> (word_width - destination_index_in_word));
                    }
                }
            }
        }
    };

    /**
     * @brief unpacked数组
     *
     * @tparam type 数据类型
     * @tparam n 元素个数
     */
    template <::verilator_utils::is_verilator_data_type type, ::std::size_t n>
        requires (n != 0)
    struct unpacked_array
    {
        using value_type = type;
        using unpacked_array_type = ::VlUnpacked<value_type, n>;
        friend struct std::formatter<unpacked_array>;

        /**
         * @brief 获取数组元素个数
         *
         * @return std::size_t 数组元素个数
         */
        constexpr static ::std::size_t size() noexcept { return n; }

        /**
         * @brief 获取元素的切片宽度
         *
         * @return std::size_t 元素的切片宽度
         */
        [[nodiscard]] ::std::size_t width() const noexcept { return data.front().width(); }

        /**
         * @brief 获取数据类型
         *
         * @return 数据类型
         */
        [[nodiscard]] ::verilator_utils::data_format::format format() const noexcept { return data.front().format(); }

    private:
        template <::std::size_t... indexes>
        explicit unpacked_array(unpacked_array_type& data,
                                ::std::size_t width,
                                ::verilator_utils::data_format::format format,
                                ::std::index_sequence<indexes...> /* unused */) :
            // clang-format off
            data{actual_value_type{data.m_storage[indexes], width, format}...}
        // clang-format on
        {
        }

        using actual_value_type = ::verilator_utils::vector_slice<value_type>;
        using cast_type = actual_value_type::cast_type;
        ::std::array<actual_value_type, n> data;

    public:
        /**
         * @brief 创建unpacked数组包装器
         *
         * @param data 数据对象
         * @param width 元素的切片宽度
         * @param format 数据类型
         */
        explicit unpacked_array(unpacked_array_type& data,
                                ::std::size_t width,
                                ::verilator_utils::data_format::format format = ::verilator_utils::data_format::hex) :
            unpacked_array{data, width, format, ::std::make_index_sequence<n>{}}
        {
        }

        unpacked_array(const unpacked_array&) = default;
        unpacked_array(unpacked_array&&) = default;
        unpacked_array& operator= (unpacked_array&&) = default;
        ~unpacked_array() = default;

        /**
         * @brief 获取数组中的元素
         *
         * @param index 元素索引
         * @return 元素引用
         */
        actual_value_type& operator[] (::std::size_t index) noexcept { return data[index]; }

        /**
         * @brief 转化为视图
         *
         * @return std::span 视图
         */
        auto span(this auto&& self) noexcept { return ::std::span{self.data}; }

        template <typename span_value_type>
            requires (::std::is_assignable_v<actual_value_type, span_value_type>)
        unpacked_array& operator= (::std::span<span_value_type, n> value)
        {
            ::std::ranges::copy(value, data.begin());
            return *this;
        }

        unpacked_array& operator= (const unpacked_array& other)
        {
            VU_CHECK(width() == other.width(), "数组宽度{}与赋值源宽度{}不同"sv, width(), other.width());
            ::std::ranges::copy(other.data, data.begin());
            return *this;
        }

        template <::std::equality_comparable_with<actual_value_type> span_value_type>
        bool operator== (::std::span<span_value_type, n> value) const
        { return ::std::ranges::equal(data, value); }

        bool operator== (const unpacked_array& other) const
        {
            VU_CHECK(width() == other.width(), "数组宽度{}与比较对象宽度{}不同"sv, width(), other.width());
            return data == other.data;
        }

        /**
         * @brief 将数据包装器格式化输出到缓冲区上
         *
         * @tparam iter_t 迭代器类型
         * @param iter 迭代器
         * @return 格式化后缓冲区迭代器
         */
        template <typename iter_t>
        iter_t format_to(iter_t iter) const
        { return ::std::format_to(iter, "{}"sv, data); }

        /**
         * @brief 转换为字符串表示
         *
         * @return 字符串表示
         */
        [[nodiscard]] ::std::string to_string() const
        {
            ::std::string result{};
            format_to(::std::back_inserter(result));
            return result;
        }

        /**
         * @brief 打包当前位切片的格式
         *
         * @return 打包的格式
         */
        [[nodiscard]] ::verilator_utils::packed_format dump_format() const noexcept
        { return ::verilator_utils::packed_format{width(), format()}; }
    };

    /**
     * @brief 创建unpacked数组包装器，递归展开多维unpacked数组
     *
     * @tparam type Verilator unpacked数组类型
     * @param data unpacked数组数据对象
     * @param width 最内层元素的切片宽度
     * @return 一维数组返回unpacked_array，多维数组返回嵌套std::array
     */
    template <::verilator_utils::is_vl_unpacked_type type>
    auto make_unpacked_array(type& data, ::std::size_t width)
    {
        using traits = ::verilator_utils::verilator_unpacked_array_type_traits<type>;
        using value_type = traits::value_type;
        if constexpr(::verilator_utils::is_verilator_data_type<value_type>)
        {
            return ::verilator_utils::unpacked_array<value_type, traits::n>{data, width};
        }
        else
        {
            return [&]<::std::size_t... indexes>(::std::index_sequence<indexes...>) {
                return ::std::array{::verilator_utils::make_unpacked_array(data[indexes], width)...};
            }(::std::make_index_sequence<traits::n>{});
        }
    }
}  // namespace verilator_utils

export namespace std
{
    /**
     * @brief bit_slice格式化支持
     *
     * 不支持格式符
     * @tparam value_type Verilator数据类型
     */
    template <::verilator_utils::is_verilator_data_type value_type>
    struct formatter<::verilator_utils::bit_slice<value_type>>
    {
        constexpr static ::std::format_parse_context::iterator parse(::std::format_parse_context& ctx)
        { return ::verilator_utils::detail::parse_format_string_without_flags(ctx, "无效的verilator_utils::bit_slice格式符"sv); }

        template <typename iter_t, typename char_t>
        static auto format(const ::verilator_utils::bit_slice<value_type>& value,
                           ::std::basic_format_context<iter_t, char_t>& ctx)
        { return value.format_to(ctx.out()); }
    };

    /**
     * @brief vector_slice格式化支持
     *
     * 不支持格式符
     * @tparam value_type Verilator数据类型
     */
    template <::verilator_utils::is_verilator_data_type value_type>
    struct formatter<::verilator_utils::vector_slice<value_type>>
    {
        constexpr static ::std::format_parse_context::iterator parse(::std::format_parse_context& ctx)
        {
            return ::verilator_utils::detail::parse_format_string_without_flags(ctx,
                                                                                "无效的verilator_utils::vector_slice格式符"sv);
        }

        template <typename iter_t, typename char_t>
        static auto format(const ::verilator_utils::vector_slice<value_type>& value,
                           ::std::basic_format_context<iter_t, char_t>& ctx)
        { return value.format_to(ctx.out()); }
    };

    /**
     * @brief unpacked_array格式化支持
     *
     * 不支持格式符
     * @tparam type Verilator数据类型
     * @tparam n 数组元素个数
     */
    template <::verilator_utils::is_verilator_data_type type, ::std::size_t n>
    struct formatter<::verilator_utils::unpacked_array<type, n>>
    {
        constexpr static ::std::format_parse_context::iterator parse(::std::format_parse_context& ctx)
        {
            return ::verilator_utils::detail::parse_format_string_without_flags(ctx,
                                                                                "无效的verilator_utils::unpacked_array格式符"sv);
        }

        template <typename iter_t, typename char_t>
        static auto format(const ::verilator_utils::unpacked_array<type, n>& value,
                           ::std::basic_format_context<iter_t, char_t>& ctx)
        { return value.format_to(ctx.out()); }
    };

    /**
     * @brief format_wrapper格式化支持
     *
     * 不支持格式符
     * @tparam value_type Verilator数据类型
     */
    template <::verilator_utils::is_format_wrapper_data_type value_type>
    struct formatter<::verilator_utils::format_wrapper<value_type>>
    {
        constexpr static ::std::format_parse_context::iterator parse(::std::format_parse_context& ctx)
        {
            return ::verilator_utils::detail::parse_format_string_without_flags(ctx,
                                                                                "无效的verilator_utils::format_wrapper格式符"sv);
        }

        template <typename iter_t, typename char_t>
        static auto format(const ::verilator_utils::format_wrapper<value_type>& value,
                           ::std::basic_format_context<iter_t, char_t>& ctx)
        { return value.format_to(ctx.out()); }
    };
}  // namespace std

export namespace doctest
{
    template <::verilator_utils::is_verilator_data_type value_type>
    struct StringMaker<::verilator_utils::bit_slice<value_type>>
    {
        static ::doctest::String convert(const ::verilator_utils::bit_slice<value_type>& value) { return value.to_string(); }
    };

    template <::verilator_utils::is_verilator_data_type value_type>
    struct StringMaker<::verilator_utils::vector_slice<value_type>>
    {
        static ::doctest::String convert(const ::verilator_utils::vector_slice<value_type>& value) { return value.to_string(); }
    };

    template <typename type, ::std::size_t n>
    struct StringMaker<::verilator_utils::unpacked_array<type, n>>
    {
        static ::doctest::String convert(const ::verilator_utils::unpacked_array<type, n>& value) { return value.to_string(); }
    };

    template <::verilator_utils::is_format_wrapper_data_type value_type>
    struct StringMaker<::verilator_utils::format_wrapper<value_type>>
    {
        static ::doctest::String convert(const ::verilator_utils::format_wrapper<value_type>& value) { return value.to_string(); }
    };
}  // namespace doctest
