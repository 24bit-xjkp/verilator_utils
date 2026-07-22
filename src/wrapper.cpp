module;
#include <doctest_fwd.hpp>
export module verilator_utils:wrapper;
import :utils;

extern "C++"
{
#include <doctest.h>
}

export namespace verilator_utils
{
    /**
     * @brief 检查类型是否为格式包装器支持的数据类型
     *
     * @tparam type 要检查的类型
     */
    template <typename type>
    concept is_format_wrapper_data_type = ::verilator_utils::is_cpp_underlying_type<type> || ::VlIsVlWide<type>::value;

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
        constexpr inline static bool is_vl_wide{::VlIsVlWide<type>::value};
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
        constexpr inline format_wrapper(type value,
                                        ::std::size_t width,
                                        ::verilator_utils::data_format::format format = ::verilator_utils::data_format::hex) :
            underlying_value{value}, data_width{width}, data_format{::std::move(format)}
        {
            check_format();
            check_value();
        }

        constexpr inline format_wrapper(const format_wrapper&) noexcept = default;
        constexpr inline format_wrapper& operator= (const format_wrapper&) noexcept = default;
        constexpr inline format_wrapper(format_wrapper&&) noexcept = default;
        constexpr inline format_wrapper& operator= (format_wrapper&&) noexcept = default;
        constexpr inline ~format_wrapper() noexcept = default;

        /**
         * @brief 获取数据
         *
         * @return 数据引用
         */
        [[nodiscard]] constexpr inline const type& value() const noexcept { return underlying_value; }

        /**
         * @brief 获取位宽
         *
         * @return 数据位宽
         */
        [[nodiscard]] constexpr inline ::std::size_t width() const noexcept { return data_width; }

        /**
         * @brief 获取数据格式
         *
         * @return 数据格式
         */
        [[nodiscard]] constexpr inline const ::verilator_utils::data_format::format& format() const noexcept
        { return data_format; }

        /**
         * @brief 设置数据
         *
         * @param value 要设置的数据
         * @return 格式包装器引用
         */
        constexpr inline format_wrapper& operator= (set_value_t value)
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
        constexpr inline to_verilator_t to_verilator() const noexcept
        {
            if constexpr(is_vl_wide) { return underlying_value; }
            else
            {
                return data_format.visit(
                    [this]<typename format_t>(const format_t& format) noexcept -> ::std::uint64_t
                    {
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
        inline iter_t format_to(iter_t iter) const
        {
            return data_format.visit(
                [this, iter]<typename format_t>(const format_t& format) -> iter_t
                {
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
        [[nodiscard]] inline ::std::string to_string() const
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

    private:
        type underlying_value;
        ::std::size_t data_width;
        ::verilator_utils::data_format::format data_format;

        /**
         * @brief 检查格式是否合法
         *
         */
        constexpr inline void check_format()
        {
            using namespace ::std::string_view_literals;

            auto is_monostate{::std::holds_alternative<::std::monostate>(data_format)};
            if consteval
            {
                if(::std::holds_alternative<::std::monostate>(data_format)) { throw ::std::invalid_argument{"必须设定数据类型"}; }
            }
            else
            {
                REQUIRE_FALSE_MESSAGE(is_monostate, "必须设定数据类型"sv);
            }

            if constexpr(::VlIsVlWide<type>::value)
            {
                constexpr static auto bin_index{::verilator_utils::variant_type_index<::verilator_utils::data_format::hex_t,
                                                                                      ::verilator_utils::data_format::format>};
                auto is_hex_or_bin{data_format.index() <= bin_index};

                if consteval
                {
                    if(!is_hex_or_bin) { throw ::std::invalid_argument{"VlWide只支持十六进制和二进制格式"}; }
                }
                else
                {
                    REQUIRE_MESSAGE(is_hex_or_bin, "VlWide只支持十六进制和二进制格式"sv);
                }
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
                if consteval
                {
                    if(is_signed_or_floating_point_or_fixed_point)
                    {
                        throw ::std::invalid_argument{"std::uint64_t不支持有符号十进制、浮点数和定点数"};
                    }
                }
                else
                {
                    REQUIRE_FALSE_MESSAGE(is_signed_or_floating_point_or_fixed_point,
                                          "std::uint64_t不支持有符号十进制、浮点数和定点数"sv);
                }
            }
            else if constexpr(::std::same_as<type, ::std::int64_t>)
            {
                auto is_dec_signed{::std::holds_alternative<::verilator_utils::data_format::dec_signed_t>(data_format)};
                if consteval
                {
                    if(!is_dec_signed) { throw ::std::invalid_argument{"std::int64_t只支持有符号十进制格式"}; }
                    else
                    {
                        REQUIRE_MESSAGE(is_dec_signed, "std::int64_t只支持有符号十进制格式"sv);
                    }
                }
            }
            else if constexpr(::std::same_as<type, float>)
            {
                auto is_dec_signed{::std::holds_alternative<::verilator_utils::data_format::real_float_t>(data_format)};
                if consteval
                {
                    if(!is_dec_signed) { throw ::std::invalid_argument{"float只支持单精度浮点数格式"}; }
                    else
                    {
                        REQUIRE_MESSAGE(is_dec_signed, "float只支持单精度浮点数格式"sv);
                    }
                }
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
                if consteval
                {
                    if(!is_double_or_fixed_point) { throw ::std::invalid_argument{"double只支持双精度浮点数核定点数格式"}; }
                    else
                    {
                        REQUIRE_MESSAGE(is_double_or_fixed_point, "double只支持双精度浮点数核定点数格式"sv);
                    }
                }
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
        constexpr inline void check_value()
        {
            using namespace ::std::string_view_literals;

            auto do_check{[this](::std::size_t value_width) constexpr
                          {
                              if consteval
                              {
                                  if(value_width > width()) { throw ::std::invalid_argument{"数据宽度过大"}; }
                              }
                              else
                              {
                                  REQUIRE_GE(width(), value_width);
                              }
                          }};
            if constexpr(is_vl_wide)
            {
                auto value_width{::verilator_utils::detail::vlwide_width(underlying_value)};
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
            else if constexpr(::std::same_as<type, float>)
            {
                // 标准单精度浮点类型固定32位，无需宽度检查
            }
            else if constexpr(::std::same_as<type, double>)
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
                    auto convert_to_verilator{data_format.visit(
                        [this](const auto& format) noexcept -> ::std::uint64_t
                        {
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
                    auto convert_back{data_format.visit(
                        [convert_to_verilator](const auto& format) noexcept -> double
                        {
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
                    if consteval
                    {
                        if(convert_back != underlying_value)
                        {
                            throw ::std::invalid_argument{"当前数据包装器不能无修改的保存给定数据"};
                        }
                    }
                    else
                    {
                        REQUIRE_MESSAGE(convert_back == underlying_value, "当前数据包装器不能无修改的保存给定数据"sv);
                    }
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
            data{data}, left_bound{left_bound_index}, right_bound{right_bound_index}, data_format{::std::move(format)}
        {
            REQUIRE_GE(left_bound, right_bound);
            ::verilator_utils::data_format::check_format(data_format, width());
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
            data{data}, left_bound{width - 1}, right_bound{0}, data_format{::std::move(format)}
        {
            REQUIRE_NE(width, 0);
            ::verilator_utils::data_format::check_format(data_format, width);
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
        inline vector_slice operator[] (
            ::std::size_t left_bound_index,
            ::std::size_t right_bound_index,
            const ::verilator_utils::data_format::format& format = ::verilator_utils::data_format::format{}) const noexcept
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
         * @brief 相等运算符
         *
         * @param value 要比较的值
         * @return 是否相等
         */
        inline friend bool operator== (const vector_slice& self, const type& value)
            requires (is_vl_wide)  // NOLINT(readability-redundant-parentheses)
        {
            auto temp{static_cast<cast_type>(self)};
            auto value_width{::verilator_utils::detail::vlwide_width(value)};
            REQUIRE_GE(self.width(), value_width);
            return temp == value;
        }

        /**
         * @brief 相等运算符
         *
         * @param value 要比较的值
         * @return 是否相等
         */
        template <::verilator_utils::is_format_wrapper_data_type underlying_type>
        inline friend bool operator== (const vector_slice& self, ::verilator_utils::format_wrapper<underlying_type>& value)
        { return self == value.to_verilator(); }

        /**
         * @brief 三路比较运算符
         *
         * @param value 要比较的值
         * @return 比较结果，由于潜在的浮点比较，因此退化为std::partial_ordering
         */
        template <::verilator_utils::is_cpp_underlying_type underlying_type>
        inline friend ::std::partial_ordering operator<=> (const vector_slice& self, underlying_type value)
        {
            using namespace ::std::string_view_literals;
            constexpr static auto bin_index{2zu};
            REQUIRE_MESSAGE(self.data_format.index() > bin_index, "十六进制和二进制格式不支持三路比较，只支持相等比较"sv);
            return self.to_underlying().visit([value](auto underlying_value) noexcept -> ::std::partial_ordering
                                              { return compare_underlying(underlying_value, value); });
        }

        /**
         * @brief 三路比较运算符
         *
         * @param value 要比较的值
         * @note 数据类型为VlWide的format_wrapper只支持十六进制和二进制，因此不能用于三路比较
         * @return 比较结果，由于潜在的浮点比较，因此退化为std::partial_ordering
         */
        template <::verilator_utils::is_cpp_underlying_type underlying_type>
        inline friend ::std::partial_ordering operator<=> (const vector_slice& self,
                                                           ::verilator_utils::format_wrapper<underlying_type>& value)
        { return self <=> value.value(); }

        /**
         * @brief 三路比较运算符
         *
         * @param value 要比较的值
         * @return 比较结果，由于潜在的浮点比较，因此退化为std::partial_ordering
         */
        inline friend ::std::partial_ordering operator<=> (const vector_slice& self, const vector_slice& other)
        {
            using namespace ::std::string_view_literals;
            constexpr static auto bin_index{2zu};
            REQUIRE_MESSAGE(self.data_format.index() > bin_index, "十六进制和二进制格式不支持三路比较，只支持相等比较"sv);
            REQUIRE_MESSAGE(other.data_format.index() > bin_index, "十六进制和二进制格式不支持三路比较，只支持相等比较"sv);
            return self.to_underlying().visit(
                [other_to_underlying = other.to_underlying()](auto self_underlying_value) noexcept -> ::std::partial_ordering
                {
                    return other_to_underlying.visit(
                        [self_underlying_value](auto other_underlying_value) noexcept -> ::std::partial_ordering
                        { return compare_underlying(self_underlying_value, other_underlying_value); });
                });
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
            ::verilator_utils::data_format::check_format(data_format, width());
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
                [aligned_value, width = width()]<typename format_t>(const format_t& format) noexcept -> underlying_type
                {
                    if constexpr(::std::same_as<format_t, ::std::monostate>)
                    {
                        ::std::unreachable();
                        return underlying_type{};
                    }
                    else
                    {
                        if constexpr(requires() {
                                         {
                                             format.to_underlying(aligned_value, width)
                                         } -> ::verilator_utils::is_cpp_underlying_type;
                                     })
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
        [[nodiscard]] inline bool is_valid() const noexcept
        {
            return data_format.visit(
                [this]<typename format_t>(const format_t& format) noexcept -> bool
                {
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
        inline iter_t format_to(iter_t iter) const
        {
            return data_format.visit(
                [this, iter]<typename format_t>(const format_t& format) -> iter_t
                {
                    if constexpr(::std::same_as<format_t, ::std::monostate>)
                    {
                        ::std::unreachable();
                        return iter;
                    }
                    else if constexpr(::std::same_as<format_t, ::verilator_utils::data_format::hex_t> ||
                                      ::std::same_as<format_t, ::verilator_utils::data_format::bin_t>)
                    {
                        auto aligned_value{static_cast<cast_type>(*this)};
                        return format.format_to(iter, aligned_value, width());
                    }
                    else
                    {
                        return to_underlying().visit([iter, &format](auto underlying_data) -> iter_t
                                                     { return format.format_to(iter, underlying_data); });
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

    private:
        template <::verilator_utils::is_cpp_underlying_type left_type, ::verilator_utils::is_cpp_underlying_type right_type>
        constexpr inline static ::std::partial_ordering compare_underlying(left_type left, right_type right) noexcept
        {
            if constexpr(::std::integral<left_type> && ::std::integral<right_type>)
            {
                if(::std::cmp_less(left, right)) { return ::std::partial_ordering::less; }
                if(::std::cmp_greater(left, right)) { return ::std::partial_ordering::greater; }
                return ::std::partial_ordering::equivalent;
            }
            else
            {
                return left <=> right;
            }
        }

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

    template <::verilator_utils::is_format_wrapper_data_type value_type>
    struct formatter<::verilator_utils::format_wrapper<value_type>>
    {
        constexpr inline static auto parse(::std::format_parse_context& ctx) noexcept { return ctx.begin(); }

        template <typename iter_t, typename char_t>
        inline static auto format(const ::verilator_utils::format_wrapper<value_type>& value,
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
