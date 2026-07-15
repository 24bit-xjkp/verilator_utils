module;
#include <doctest_fwd.hpp>
export module verilator_utils:wrapper;
import :utils;

extern "C++"
{
#include <doctest.h>
}

namespace verilator_utils::detail
{
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
    inline iter_t verilator_data_format_to_hex(iter_t iter, const type& data, ::std::size_t width)
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
            iter = ::std::format_to(iter, "{:#0{}x}", *(--end), (left_word_width + digit_width - 1) / digit_width + prefix_size);
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
    inline iter_t verilator_data_format_to_bin(iter_t iter, const type& data, ::std::size_t width)
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

}  // namespace verilator_utils::detail

export namespace verilator_utils
{
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
         */
        constexpr inline explicit bit_slice(value_type& data, ::std::size_t index = 0) noexcept : data{data}, index{index} {}

        /**
         * @brief 赋值运算符
         *
         * @param value 要赋值的值
         * @return bool_wrapper& 赋值后对象的引用
         */
        constexpr inline bit_slice& operator= (::std::uint64_t value)
        {
            REQUIRE_MESSAGE(value <= 1, "位包装器只能赋值0或1");
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
        constexpr inline operator ::std::uint64_t () const noexcept
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
         * @brief 获取位宽
         *
         * @return std::size_t 位宽
         */
        [[nodiscard]] constexpr inline ::std::size_t width() const noexcept { return 1; }

        /**
         * @brief 转换为字符串表示
         *
         * @return 字符串表示
         */
        [[nodiscard]] inline ::std::string to_string() const { return ::std::format("{:#x}", ::std::uint64_t{*this}); }

    private:
        /// 数据引用
        value_type& data;
        /// 位索引
        ::std::size_t index;
        /// 每个字的位宽
        constexpr inline static ::std::size_t word_width{::std::numeric_limits<::EData>::digits};
        /// 是否为Verilator宽数据类型
        constexpr inline static bool is_vl_wide{::VlIsVlWide<value_type>::value};
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
        constexpr inline static bool is_vl_wide{::VlIsVlWide<value_type>::value};

    public:
        /// 可转换的目标类型
        using cast_type = std::conditional_t<is_vl_wide, value_type, ::std::uint64_t>;
        /// 能转换到的C++基础数据类型
        using underlying_type = ::std::variant<::std::uint64_t, ::std::int64_t, float, double>;

        /**
         * @brief 构造一个向量切片对象，索引为闭区间
         *
         * @param data 数据引用
         * @param left_bound_index 索引上界
         * @param right_bound_index 索引下界
         * @param format 数据格式
         */
        inline explicit vector_slice(value_type& data,
                                     ::std::size_t left_bound_index,
                                     ::std::size_t right_bound_index,
                                     ::verilator_utils::data_format::format format = ::verilator_utils::data_format::hex) :
            data{data}, left_bound{left_bound_index}, right_bound{right_bound_index}, data_format{format}
        {
            REQUIRE_GE(left_bound, right_bound);
            check_format(data_format);
        }

        /**
         * @brief 构造一个向量切片对象
         *
         * @param data 数据引用
         * @param width 宽度
         * @param format 数据格式
         */
        inline explicit vector_slice(value_type& data,
                                     ::std::size_t width,
                                     ::verilator_utils::data_format::format format = ::verilator_utils::data_format::hex) :
            data{data}, left_bound{width - 1}, right_bound{0}, data_format{format}
        {
            REQUIRE_NE(width, 0);
            check_format(data_format);
        }

        inline vector_slice(const vector_slice&) = default;
        inline vector_slice(vector_slice&&) = default;
        inline vector_slice& operator= (vector_slice&&) = default;
        inline ~vector_slice() = default;

        /**
         * @brief 获取位宽
         *
         * @return 位宽
         */
        [[nodiscard]] constexpr inline ::std::size_t width() const noexcept { return left_bound - right_bound + 1; }

        /**
         * @brief 获取数据类型
         *
         * @return 数据类型
         */
        [[nodiscard]] constexpr inline ::verilator_utils::data_format::format format() const noexcept { return data_format; }

        /**
         * @brief 下标运算符，用于访问向量切片的指定位
         * 索引为当前切片范围内的相对索引，范围为[0, width() - 1]
         * @param index 索引值
         * @return bit_slice 对应位的包装对象
         */
        inline bit_slice<type> operator[] (::std::size_t index) const noexcept
        {
            REQUIRE_LE(index, width() - 1);
            return bit_slice<type>{data, index + right_bound};
        }

        /**
         * @brief 下标运算符，用于访问向量切片的指定切片
         * 索引为当前切片范围内的相对索引，范围为[0, width() - 1]
         * @param left_bound_index 左边界索引
         * @param right_bound_index 右边界索引
         * @param format 数据格式，为std::monostate表示使用当前对象的数据格式
         * @return 向量切片的包装对象
         */
        inline vector_slice
            operator[] (::std::size_t left_bound_index,
                        ::std::size_t right_bound_index,
                        ::verilator_utils::data_format::format format = ::verilator_utils::data_format::format{}) const noexcept
        {
            REQUIRE_GE(left_bound_index, right_bound_index);
            REQUIRE_LE(right_bound_index, left_bound);
            REQUIRE_LE(left_bound_index, width() - 1);
            if(!::verilator_utils::detail::is_variable_width_format(data_format) &&
               left_bound_index - right_bound_index + 1 != width())
            {
                using namespace ::std::string_view_literals;
                REQUIRE_FALSE_MESSAGE(::std::holds_alternative<::std::monostate>(format),
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
        inline operator cast_type() const noexcept
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
        inline vector_slice& operator= (::std::uint64_t value)
        {
            REQUIRE_GE(64, width());
            auto width_is_enough{width() == 64 || (value >> width()) == 0};
            REQUIRE_MESSAGE(width_is_enough, "值宽度超出向量宽度");
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

        inline vector_slice& operator= (const vector_slice& other)
        {
            REQUIRE_EQ(width(), other.width());
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
         * @brief 相等运算符
         *
         * @param value 要比较的值
         * @return 是否相等
         */
        inline friend bool operator== (const vector_slice& self, ::std::uint64_t value)
        {
            REQUIRE_GE(64, self.width());
            auto width_is_enough{self.width() == 64 || (value >> self.width()) == 0};
            REQUIRE_MESSAGE(width_is_enough, "值宽度超出向量宽度");
            auto temp{static_cast<cast_type>(self)};
            if constexpr(is_vl_wide) { return wide_to_uint64(temp) == value; }
            else
            {
                return temp == value;
            }
        }

        /**
         * @brief 赋值运算符, 从其他向量切片赋值
         *
         * @tparam other_type 其他向量切片的类型
         * @param other 其他向量切片对象
         * @return 赋值后对象的引用
         */
        template <::verilator_utils::is_verilator_data_type other_type>
        inline vector_slice& operator= (const ::verilator_utils::vector_slice<other_type>& other)
        {
            REQUIRE_EQ(width(), other.width());
            auto aligned_value{static_cast<::verilator_utils::vector_slice<other_type>::cast_type>(other)};
            if constexpr(is_vl_wide)
            {
                value_type temp{};
                if constexpr(::VlIsVlWide<other_type>::value)
                {
                    ::std::ranges::copy_n(aligned_value.data(), temp.size(), temp.data());
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
                if constexpr(::VlIsVlWide<other_type>::value)
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

        inline vector_slice& operator= (const value_type& value)
            requires (is_vl_wide)  // NOLINT(readability-redundant-parentheses)
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
        inline friend bool operator== (const vector_slice& self, const ::verilator_utils::vector_slice<other_type>& other)
        {
            REQUIRE_EQ(self.width(), other.width());
            auto aligned_other{static_cast<::verilator_utils::vector_slice<other_type>::cast_type>(other)};
            if constexpr(is_vl_wide)
            {
                auto aligned_value{static_cast<cast_type>(self)};
                auto words{(self.width() + word_width - 1u) / word_width};
                if constexpr(::VlIsVlWide<other_type>::value)
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
                if constexpr(::VlIsVlWide<other_type>::value)
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
        inline vector_slice convert(::verilator_utils::data_format::format format)
        {
            check_format(format);
            return vector_slice{data, left_bound, right_bound, format};
        }

        /**
         * @brief 转化为C++基础数据类型
         * 整数转化为std::uint64_t，浮点数按格式转化为float或double，定点数转化为double
         * @return C++基础数据类型
         */
        [[nodiscard]] inline underlying_type to_underlying() const
        {
            REQUIRE_GE(64, width());
            ::std::uint64_t aligned_value{};
            if constexpr(is_vl_wide) { aligned_value = wide_to_uint64(static_cast<cast_type>(*this)); }
            else
            {
                aligned_value = static_cast<::std::uint64_t>(*this);
            }

            return data_format.visit(
                [aligned_value, width = width()](auto format) noexcept -> underlying_type
                {
                    using format_t = decltype(format);

                    if constexpr(::std::same_as<format_t, ::std::monostate>)
                    {
                        ::std::unreachable();
                        return underlying_type{};
                    }
                    else if constexpr(::std::same_as<format_t, ::verilator_utils::detail::data_format_hex_t> ||
                                      ::std::same_as<format_t, ::verilator_utils::detail::data_format_bin_t> ||
                                      ::std::same_as<format_t, ::verilator_utils::detail::data_format_unsigned_t>)
                    {
                        return underlying_type{aligned_value};
                    }
                    else if constexpr(::std::same_as<format_t, ::verilator_utils::detail::data_format_signed_t>)
                    {
                        auto shift{64zu - width};
                        auto sign_extended_value{static_cast<::std::int64_t>(aligned_value << shift) >> shift};
                        return underlying_type{sign_extended_value};
                    }
                    else if constexpr(::std::same_as<format_t, ::verilator_utils::detail::data_format_float_t>)
                    {
                        auto temp{static_cast<::std::uint32_t>(aligned_value)};
                        return underlying_type{::std::bit_cast<float>(temp)};
                    }
                    else if constexpr(::std::same_as<format_t, ::verilator_utils::detail::data_format_double_t>)
                    {
                        return underlying_type{::std::bit_cast<double>(aligned_value)};
                    }
                    else if constexpr(::std::same_as<format_t, ::verilator_utils::detail::data_format_unsigned_fixed_point_t>)
                    {
                        return underlying_type{::std::ldexp(static_cast<double>(aligned_value), -format.fractional_bit)};
                    }
                    else if constexpr(::std::same_as<format_t, ::verilator_utils::detail::data_format_signed_fixed_point_t>)
                    {
                        auto shift{64zu - format.width()};
                        auto signed_value{static_cast<::std::int64_t>(aligned_value << shift) >> shift};
                        return underlying_type{::std::ldexp(static_cast<double>(signed_value), -format.fractional_bit)};
                    }
                    else if constexpr(::std::same_as<format_t, ::verilator_utils::detail::data_format_sign_mag_t>)
                    {
                        auto sign_bit_index{format.width() - 1};
                        auto sign{aligned_value >> sign_bit_index};
                        auto magnitude_mask{(1zu << sign_bit_index) - 1zu};
                        auto magnitude{::std::ldexp(static_cast<double>(aligned_value & magnitude_mask), -format.fractional_bit)};
                        return underlying_type{sign ? -magnitude : magnitude};
                    }
                    else
                    {
                        static_assert(false, "未实现所有格式的转化");
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
        inline iter_t format_to(iter_t iter) const
        {
            return data_format.visit(
                [this, iter](auto format) -> iter_t
                {
                    using format_t = decltype(format);
                    if constexpr(::std::same_as<format_t, ::std::monostate>)
                    {
                        ::std::unreachable();
                        return iter;
                    }
                    else if constexpr(::std::same_as<format_t, ::verilator_utils::detail::data_format_hex_t>)
                    {
                        auto aligned_value{static_cast<cast_type>(*this)};
                        return ::verilator_utils::detail::verilator_data_format_to_hex(iter, aligned_value, width());
                    }
                    else if constexpr(::std::same_as<format_t, ::verilator_utils::detail::data_format_bin_t>)
                    {
                        auto aligned_value{static_cast<cast_type>(*this)};
                        return ::verilator_utils::detail::verilator_data_format_to_bin(iter, aligned_value, width());
                    }
                    else if constexpr(::std::same_as<format_t, ::verilator_utils::detail::data_format_signed_t>)
                    {
                        auto underlying_data{to_underlying()};
                        return ::std::format_to(iter, "{}", ::std::get<::std::int64_t>(underlying_data));
                    }
                    else if constexpr(::std::same_as<format_t, ::verilator_utils::detail::data_format_unsigned_t>)
                    {
                        auto underlying_data{to_underlying()};
                        return ::std::format_to(iter, "{}", ::std::get<::std::uint64_t>(underlying_data));
                    }
                    else if constexpr(::std::same_as<format_t, ::verilator_utils::detail::data_format_float_t>)
                    {
                        auto underlying_data{to_underlying()};
                        if(format.format_as_hex) { return ::std::format_to(iter, "{:a}", ::std::get<float>(underlying_data)); }
                        return ::std::format_to(iter, "{}", ::std::get<float>(underlying_data));
                    }
                    else if constexpr(::std::same_as<format_t, ::verilator_utils::detail::data_format_double_t> ||
                                      ::std::same_as<format_t, ::verilator_utils::detail::data_format_unsigned_fixed_point_t> ||
                                      ::std::same_as<format_t, ::verilator_utils::detail::data_format_signed_fixed_point_t> ||
                                      ::std::same_as<format_t, ::verilator_utils::detail::data_format_sign_mag_t>)
                    {
                        auto underlying_data{to_underlying()};
                        if(format.format_as_hex) { return ::std::format_to(iter, "{:a}", ::std::get<double>(underlying_data)); }
                        return ::std::format_to(iter, "{}", ::std::get<double>(underlying_data));
                    }
                    else
                    {
                        static_assert(false, "未实现所有格式的转化");
                    }
                });
        }

        /**
         * @brief 转换为字符串表示
         *
         * @return 字符串表示
         */
        [[nodiscard]] inline ::std::string to_string() const
        {
            ::std::string result{};
            // 0b和0x前缀的长度
            constexpr static auto prefix_size{2zu};
            if(::std::holds_alternative<::verilator_utils::detail::data_format_hex_t>(data_format))
            {
                /// 每个十六进制位的位宽
                constexpr static ::std::size_t digit_width{4zu};
                auto total_len{(width() + digit_width - 1) / digit_width + prefix_size};
                result.reserve(total_len);
            }
            else if(::std::holds_alternative<::verilator_utils::detail::data_format_bin_t>(data_format))
            {
                auto total_len{width() + prefix_size};
                result.reserve(total_len);
            }
            format_to(::std::back_inserter(result));
            return result;
        }

    private:
        /// 每个字的位宽
        constexpr inline static ::std::size_t word_width{::std::numeric_limits<::EData>::digits};
        /// 数据引用
        value_type& data;
        /// 左边界索引
        ::std::size_t left_bound;
        /// 右边界索引
        ::std::size_t right_bound;
        /// 数据类型
        ::verilator_utils::data_format::format data_format;

        /**
         * @brief 检查数据格式是否合法
         *
         * @param format 要检查的数据格式
         */
        inline void check_format(::verilator_utils::data_format::format format) const
        {
            format.visit(
                [width = width()](auto format)
                {
                    if constexpr(::std::same_as<decltype(format), ::std::monostate>)
                    {
                        using namespace ::std::string_view_literals;
                        REQUIRE_MESSAGE(false, "必须设置数据类型"sv);
                    }
                    else if constexpr(requires() {
                                          { format.min_width() } -> ::std::same_as<::std::size_t>;
                                          { format.max_width() } -> ::std::same_as<::std::size_t>;
                                      })
                    {
                        auto min_width{format.min_width()};
                        auto max_width{format.max_width()};
                        REQUIRE_GE(width, min_width);
                        REQUIRE_LE(width, max_width);
                    }
                    else
                    {
                        REQUIRE_EQ(width, format.width());
                    }
                });
        }

        constexpr inline static ::std::uint64_t scalar_mask(::std::size_t width, ::std::size_t right_bound) noexcept
        {
            auto lower_mask{width == 64 ? ::std::numeric_limits<::std::uint64_t>::max() : (::std::uint64_t{1} << width) - 1u};
            return lower_mask << right_bound;
        }

        template <typename wide_type>
        constexpr inline static ::std::uint64_t wide_to_uint64(const wide_type& value) noexcept
        {
            ::std::uint64_t result{value.at(0)};
            if constexpr(wide_type::Words > 1) { result |= static_cast<::std::uint64_t>(value.at(1)) << word_width; }
            return result;
        }

        inline void assign_aligned_value(const value_type& aligned_value) noexcept
            requires (is_vl_wide)  // NOLINT(readability-redundant-parentheses)
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
        constexpr inline static ::std::size_t size() noexcept { return n; }

        /**
         * @brief 获取元素的切片宽度
         *
         * @return std::size_t 元素的切片宽度
         */
        [[nodiscard]] inline ::std::size_t width() const noexcept { return data.front().width(); }

        /**
         * @brief 获取数据类型
         *
         * @return 数据类型
         */
        [[nodiscard]] inline ::verilator_utils::data_format::format format() const noexcept { return data.front().format(); }

    private:
        template <::std::size_t... indexes>
        explicit inline unpacked_array(unpacked_array_type& data,
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
        explicit inline unpacked_array(unpacked_array_type& data,
                                       ::std::size_t width,
                                       ::verilator_utils::data_format::format format = ::verilator_utils::data_format::hex) :
            unpacked_array{data, width, format, ::std::make_index_sequence<n>{}}
        {
        }

        inline unpacked_array(const unpacked_array&) = default;
        inline unpacked_array(unpacked_array&&) = default;
        inline unpacked_array& operator= (unpacked_array&&) = default;
        inline ~unpacked_array() = default;

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
        inline auto span(this auto&& self) noexcept { return ::std::span{self.data}; }

        template <typename span_value_type>
            requires (::std::is_assignable_v<actual_value_type, span_value_type>)
        inline unpacked_array& operator= (::std::span<span_value_type, n> value)
        {
            ::std::ranges::copy(value, data.begin());
            return *this;
        }

        inline unpacked_array& operator= (const unpacked_array& other)
        {
            REQUIRE_EQ(width(), other.width());
            ::std::ranges::copy(other.data, data.begin());
            return *this;
        }

        template <::std::equality_comparable_with<actual_value_type> span_value_type>
        inline bool operator== (::std::span<span_value_type, n> value) const
        { return ::std::ranges::equal(data, value); }

        inline bool operator== (const unpacked_array& other) const
        {
            REQUIRE_EQ(width(), other.width());
            return data == other.data;
        }

        /**
         * @brief 转换为字符串表示
         *
         * @return 字符串表示
         */
        [[nodiscard]] inline ::std::string to_string() const { return ::std::format("{}", data); }
    };

    /**
     * @brief 创建unpacked数组包装器，递归展开多维unpacked数组
     *
     * @tparam type Verilator unpacked数组类型
     * @param data unpacked数组数据对象
     * @param width 最内层元素的切片宽度
     * @return 一维数组返回unpacked_array，多维数组返回嵌套std::array
     */
    template <::verilator_utils::is_verilator_unpacked_array_type type>
    inline auto make_unpacked_array(type& data, ::std::size_t width)
    {
        using traits = ::verilator_utils::verilator_unpacked_array_type_traits<type>;
        using value_type = traits::value_type;
        if constexpr(::verilator_utils::is_verilator_data_type<value_type>)
        {
            return ::verilator_utils::unpacked_array<value_type, traits::n>{data, width};
        }
        else
        {
            return [&]<::std::size_t... indexes>(::std::index_sequence<indexes...>)
            {
                return ::std::array{::verilator_utils::make_unpacked_array(data[indexes], width)...};
            }(::std::make_index_sequence<traits::n>{});
        }
    }
}  // namespace verilator_utils

namespace verilator_utils::detail
{
    template <typename type>
    constexpr inline bool is_bit_slice_impl{false};

    template <::verilator_utils::is_verilator_data_type type>
    constexpr inline bool is_bit_slice_impl<::verilator_utils::bit_slice<type>>{true};

    template <typename type>
    constexpr inline bool is_vector_slice_impl{false};

    template <::verilator_utils::is_verilator_data_type type>
    constexpr inline bool is_vector_slice_impl<::verilator_utils::vector_slice<type>>{true};

    template <typename type>
    constexpr inline bool is_unpacked_array_impl{false};

    template <typename type, ::std::size_t n>
    constexpr inline bool is_unpacked_array_impl<::verilator_utils::unpacked_array<type, n>>{true};
}  // namespace verilator_utils::detail

export namespace verilator_utils
{
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
}  // namespace verilator_utils

export namespace std
{
    template <::verilator_utils::is_verilator_data_type value_type>
    struct formatter<::verilator_utils::bit_slice<value_type>>
    {
        constexpr inline static auto parse(::std::format_parse_context& ctx) noexcept { return ctx.begin(); }

        template <typename iter_t, typename char_t>
        inline static auto format(const ::verilator_utils::bit_slice<value_type>& value,
                                  ::std::basic_format_context<iter_t, char_t>& ctx)
        { return ::std::format_to(ctx.out(), "{:#x}", static_cast<::std::uint64_t>(value)); }
    };

    template <::verilator_utils::is_verilator_data_type value_type>
    struct formatter<::verilator_utils::vector_slice<value_type>>
    {
        constexpr inline static auto parse(::std::format_parse_context& ctx) noexcept { return ctx.begin(); }

        template <typename iter_t, typename char_t>
        inline static auto format(const ::verilator_utils::vector_slice<value_type>& value,
                                  ::std::basic_format_context<iter_t, char_t>& ctx)
        { return value.format_to(ctx.out()); }
    };

    template <typename type, ::std::size_t n>
    struct formatter<::verilator_utils::unpacked_array<type, n>>
    {
        constexpr inline static auto parse(::std::format_parse_context& ctx) noexcept { return ctx.begin(); }

        template <typename iter_t, typename char_t>
        inline static auto format(const ::verilator_utils::unpacked_array<type, n>& value,
                                  ::std::basic_format_context<iter_t, char_t>& ctx)
        { return ::std::format_to(ctx.out(), "{}", value.data); }
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
}  // namespace doctest
