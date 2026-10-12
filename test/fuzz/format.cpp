#include <doctest_macros.hpp>
import fuzz_support;

namespace
{
    // Verilator头文件在模块的全局模块片段中引入，其全局名字对导入方不可见；
    // 显式引入需要的名字后，才能以::开头的限定名引用它们
    using ::verilator_utils::verilator::EData;
    using ::verilator_utils::verilator::VlWide;

    /**
     * @brief 数据格式的操作码
     */
    enum class format_op_t : ::std::uint8_t
    {
        /// 未设置数据格式
        unset,
        /// 十六进制
        hex,
        /// 二进制
        bin,
        /// 无符号十进制
        dec_unsigned,
        /// 有符号十进制
        dec_signed,
        /// 单精度浮点
        real_float,
        /// 双精度浮点
        real_double,
        /// 无符号定点数
        unsigned_fixed_point,
        /// 有符号定点数
        signed_fixed_point,
        /// 符号-数值定点数
        sign_mag_fixed_point,
        /// 枚举
        fsm_enum,
        /// 布尔
        boolean,
        /// 操作码总数
        total_ops,
    };

    /**
     * @brief 格式包装器的数据类型操作码
     */
    enum class type_op_t : ::std::uint8_t
    {
        /// 无符号64位整数
        uint64,
        /// 有符号64位整数
        int64,
        /// 单精度浮点
        float32,
        /// 双精度浮点
        float64,
        /// 布尔
        boolean,
        /// 单字宽数据
        vlwide1,
        /// 双字宽数据
        vlwide2,
        /// 四字宽数据
        vlwide4,
        /// 操作码总数
        total_ops,
    };

    /**
     * @brief 执行一段期望被框架校验拒绝的操作
     *
     * @return 是否抛出了断言失败异常
     */
    template <typename function_t>
    [[nodiscard]] bool rejected(function_t&& operation)
    {
        try
        {
            operation();
            return false;
        }
        catch(const ::verilator_utils::assertion_error&)
        {
            return true;
        }
    }

    /// 构造一个随机的数据格式
    [[nodiscard]] ::verilator_utils::data_format::format make_format(::fuzz::reader& input)
    {
        switch(::fuzz::read_opcode<format_op_t>(input))
        {
            case format_op_t::unset: return ::verilator_utils::data_format::format{};
            case format_op_t::hex: return ::verilator_utils::hex;
            case format_op_t::bin: return ::verilator_utils::bin;
            case format_op_t::dec_unsigned: return ::verilator_utils::dec_unsigned;
            case format_op_t::dec_signed: return ::verilator_utils::dec_signed;
            case format_op_t::real_float: return ::verilator_utils::real_float(input.boolean());
            case format_op_t::real_double: return ::verilator_utils::real_double(input.boolean());
            case format_op_t::unsigned_fixed_point:
                return ::verilator_utils::unsigned_fixed_point(static_cast<::std::uint8_t>(input.range(0, 63)),
                                                               static_cast<::std::uint8_t>(input.range(0, 63)),
                                                               input.boolean());
            case format_op_t::signed_fixed_point:
                return ::verilator_utils::signed_fixed_point(static_cast<::std::uint8_t>(input.range(0, 63)),
                                                             static_cast<::std::uint8_t>(input.range(0, 63)),
                                                             input.boolean());
            case format_op_t::sign_mag_fixed_point:
                return ::verilator_utils::sign_mag_fixed_point(static_cast<::std::uint8_t>(input.range(0, 63)),
                                                               static_cast<::std::uint8_t>(input.range(0, 63)),
                                                               input.boolean());
            case format_op_t::fsm_enum:
            {
                ::std::vector<::std::string> enum_string{};
                const auto size{static_cast<::std::size_t>(input.range(1, 6))};
                enum_string.reserve(size);
                for(auto i{0zu}; i != size; ++i) { enum_string.push_back(input.short_string(8)); }
                return ::verilator_utils::fsm_enum(::std::move(enum_string));
            }
            case format_op_t::boolean: return ::verilator_utils::boolean;
            case format_op_t::total_ops: ::std::unreachable();  // 取模映射不会产生该取值
        }
    }

    /// 构造一个随机数据
    template <typename type>
    [[nodiscard]] type make_value(::fuzz::reader& input)
    {
        if constexpr(::std::same_as<type, ::std::uint64_t>) { return input.u64(); }
        else if constexpr(::std::same_as<type, ::std::int64_t>) { return ::std::bit_cast<::std::int64_t>(input.u64()); }
        else if constexpr(::std::same_as<type, float>)
        {
            return ::std::bit_cast<float>(static_cast<::std::uint32_t>(input.uint(4)));
        }
        else if constexpr(::std::same_as<type, double>) { return ::std::bit_cast<double>(input.u64()); }
        else if constexpr(::std::same_as<type, bool>) { return input.boolean(); }
        else
        {
            type value{};
            for(::std::size_t word{}; word != type::Words; ++word)
            {
                value.m_storage[word] = static_cast<::EData>(input.uint(4));
            }
            return value;
        }
    }

    /// 检查十六进制文本的长度与数值
    void check_hex_text(const ::std::string& text, ::std::size_t width, ::std::uint64_t low_value)
    {
        constexpr ::std::size_t prefix_size{2zu};
        constexpr ::std::size_t digit_width{4zu};
        ::fuzz::check(text.size() == (width + digit_width - 1u) / digit_width + prefix_size, width);
        ::fuzz::check(text[0] == '0' && text[1] == 'x');
        // 只校验最低64位对应的数字，宽数据的更高位无法用std::uint64_t表示
        const auto digits{text.substr(prefix_size)};
        const auto checked_digits{::std::min(::std::size_t{16zu}, digits.size())};
        const auto parsed{::std::stoull(digits.substr(digits.size() - checked_digits), nullptr, 16)};
        ::fuzz::check(parsed == low_value, low_value);
    }

    /// 检查二进制文本的长度与数值
    void check_bin_text(const ::std::string& text, ::std::size_t width, ::std::uint64_t low_value)
    {
        constexpr ::std::size_t prefix_size{2zu};
        ::fuzz::check(text.size() == width + prefix_size, width);
        ::fuzz::check(text[0] == '0' && text[1] == 'b');
        const auto digits{text.substr(prefix_size)};
        const auto checked_digits{::std::min(::std::size_t{64zu}, digits.size())};
        const auto parsed{::std::stoull(digits.substr(digits.size() - checked_digits), nullptr, 2)};
        ::fuzz::check(parsed == low_value, low_value);
    }

    /// 取打包数据的最低64位
    template <typename type>
    [[nodiscard]] ::std::uint64_t low_uint64(const type& value) noexcept
    {
        if constexpr(::verilator_utils::is_vl_wide<type>)
        {
            ::std::uint64_t result{value.m_storage[0]};
            if constexpr(type::Words > 1) { result |= static_cast<::std::uint64_t>(value.m_storage[1]) << 32zu; }
            return result;
        }
        else
        {
            return static_cast<::std::uint64_t>(value);
        }
    }

    /// 比较两个数据是否按位相等（浮点数据可能为NaN）
    template <typename type>
    void check_same_value(const type& lhs, const type& rhs)
    {
        if constexpr(::std::same_as<type, float>)
        {
            ::fuzz::check(::std::bit_cast<::std::uint32_t>(lhs) == ::std::bit_cast<::std::uint32_t>(rhs));
        }
        else if constexpr(::std::same_as<type, double>)
        {
            ::fuzz::check(::std::bit_cast<::std::uint64_t>(lhs) == ::std::bit_cast<::std::uint64_t>(rhs));
        }
        else
        {
            ::fuzz::check(lhs == rhs);
        }
    }

    /**
     * @brief 检查成功构造的格式包装器
     *
     * @tparam type 数据类型
     * @param wrapper 格式包装器
     * @param value 期望保存的数据
     * @param width 期望的数据宽度
     */
    template <typename type>
    void check_wrapper(const ::verilator_utils::format_wrapper<type>& wrapper, const type& value, ::std::size_t width)
    {
        ::fuzz::check(wrapper.width() == width);
        ::check_same_value(wrapper.value(), value);

        // to_string()不得抛出其他异常，且长度必须与格式和宽度一致
        const auto text{wrapper.to_string()};
        ::fuzz::check(!text.empty());

        const auto& format{wrapper.format()};
        const auto packed{wrapper.to_verilator()};
        const auto low_value{::low_uint64(packed)};

        if(::std::holds_alternative<::verilator_utils::data_format::hex_t>(format)) { ::check_hex_text(text, width, low_value); }
        else if(::std::holds_alternative<::verilator_utils::data_format::bin_t>(format))
        {
            ::check_bin_text(text, width, low_value);
        }
        else if constexpr(::verilator_utils::is_vl_wide<type>)
        {
            // 宽数据只支持十六进制和二进制，其余格式在构造时已被拒绝
            ::fuzz::check(false);
        }
        else if(::std::holds_alternative<::verilator_utils::data_format::dec_unsigned_t>(format))
        {
            ::fuzz::check(::std::stoull(text) == low_value, low_value);
        }
        else if(::std::holds_alternative<::verilator_utils::data_format::dec_signed_t>(format))
        {
            // 有符号十进制直接输出底层数据，打包表示必须能还原出底层数据
            ::fuzz::check(::std::stoll(text) == static_cast<::std::int64_t>(wrapper.value()));
            ::fuzz::check(::verilator_utils::data_format::dec_signed_t::to_underlying(low_value, width) ==
                          static_cast<::std::int64_t>(wrapper.value()));
        }
        else if(::std::holds_alternative<::verilator_utils::data_format::boolean_t>(format))
        {
            ::fuzz::check(text == (wrapper.value() ? "true" : "false"));
        }
        else if constexpr(::std::same_as<type, float>)
        {
            // 单精度格式的打包表示即float的位表示
            ::fuzz::check(static_cast<::std::uint32_t>(packed) == ::std::bit_cast<::std::uint32_t>(value));
        }
        else if constexpr(::std::same_as<type, double>)
        {
            if(::std::holds_alternative<::verilator_utils::data_format::real_double_t>(format))
            {
                ::fuzz::check(packed == ::std::bit_cast<::std::uint64_t>(value));
            }
            else
            {
                // 定点数格式必须能无损地往返，该性质已由构造函数校验，这里再次确认
                const auto recovered{format.visit([packed](const auto& format_t) noexcept -> double {
                    if constexpr(requires() { format_t.to_underlying(packed); }) { return format_t.to_underlying(packed); }
                    else
                    {
                        return 0.0;
                    }
                })};
                ::fuzz::check(recovered == value);
            }
        }
        else if constexpr(::std::same_as<type, ::std::uint64_t>)
        {
            // 其余格式（枚举）不会改变数据的打包表示
            ::fuzz::check(packed == value);
        }
        else if constexpr(::std::same_as<type, ::std::int64_t>)
        {
            // 有符号十进制的打包表示为宽度内的补码
            const auto mask{width == 64 ? ::std::numeric_limits<::std::uint64_t>::max() : (1zu << width) - 1zu};
            ::fuzz::check(packed == (static_cast<::std::uint64_t>(value) & mask));
        }
    }

    /// 检查格式包装器的三路比较与底层值转换
    template <typename type>
    void check_wrapper_extra(const ::verilator_utils::format_wrapper<type>& wrapper)
    {
        // 格式化输出接口与to_string必须一致
        ::std::string buffer{};
        wrapper.format_to(::std::back_inserter(buffer));
        ::fuzz::check(buffer == wrapper.to_string());
        // 标准格式化接口会实例化格式化器的parse路径，输出必须与to_string一致
        ::fuzz::check(::std::format("{}", wrapper) == wrapper.to_string());
        try
        {
            (void)::std::vformat("{:x}", ::std::make_format_args(wrapper));
            ::fuzz::fail("invalid format specifier was accepted");
        }
        catch(const ::std::format_error& error)
        {
            ::fuzz::check(!::std::string_view{error.what()}.empty());
        }

        const auto packed{wrapper.to_verilator()};
        // 浮点格式的底层值转换必须与位表示一致
        if constexpr(::std::same_as<type, float>)
        {
            if(::std::holds_alternative<::verilator_utils::data_format::real_float_t>(wrapper.format()))
            {
                ::fuzz::check(::std::bit_cast<::std::uint32_t>(::verilator_utils::data_format::real_float_t::to_underlying(
                                  packed)) == ::std::bit_cast<::std::uint32_t>(wrapper.value()));
            }
        }
        else if constexpr(::std::same_as<type, double>)
        {
            if(::std::holds_alternative<::verilator_utils::data_format::real_double_t>(wrapper.format()))
            {
                ::fuzz::check(::std::bit_cast<::std::uint64_t>(::verilator_utils::data_format::real_double_t::to_underlying(
                                  packed)) == ::std::bit_cast<::std::uint64_t>(wrapper.value()));
            }
        }

        // 宽数据类型只支持十六进制与二进制格式，不支持三路比较
        if constexpr(!::verilator_utils::is_vl_wide<type>)
        {
            const auto& format{wrapper.format()};
            const bool comparable{format.visit([]<typename format_t>(const format_t&) -> bool {
                return !(::std::same_as<format_t, ::verilator_utils::data_format::hex_t> ||
                         ::std::same_as<format_t, ::verilator_utils::data_format::bin_t> ||
                         ::std::same_as<format_t, ::verilator_utils::data_format::boolean_t>);
            })};
            const ::verilator_utils::format_wrapper<type> same{wrapper.value(), wrapper.width(), format};
            if(!comparable)
            {
                ::fuzz::check(::rejected([&] { (void)(wrapper <=> same); }));
                return;
            }
            // 浮点NaN与任何值都不等价，因此只比较非NaN值
            if constexpr(::std::same_as<type, float> || ::std::same_as<type, double>)
            {
                if(::std::isnan(wrapper.value())) { return; }
            }
            // 相等值的包装器必须等价：整数类型得到strong_ordering，浮点类型得到partial_ordering
            const auto order{wrapper <=> same};
            if constexpr(::std::same_as<type, float> || ::std::same_as<type, double>)
            {
                ::fuzz::check(order == ::std::partial_ordering::equivalent);
            }
            else
            {
                ::fuzz::check(order == ::std::strong_ordering::equivalent);
                // 不同底层值的包装器必须得到非等价的比较结果；
                // 部分格式可能拒绝相邻的取值，此时跳过该分支即可
                const auto width{wrapper.width()};
                const auto mask{width == 64zu ? ~::std::uint64_t{} : (::std::uint64_t{1zu} << width) - 1zu};
                const auto other_value{static_cast<type>((static_cast<::std::uint64_t>(wrapper.value()) + 1zu) & mask)};
                if(static_cast<::std::uint64_t>(other_value) != static_cast<::std::uint64_t>(wrapper.value()))
                {
                    (void)::rejected([&] {
                        const ::verilator_utils::format_wrapper<type> other{other_value, width, format};
                        const auto other_order{wrapper <=> other};
                        ::fuzz::check(other_order != 0);
                        if constexpr(::std::same_as<type, ::std::uint64_t>)
                        {
                            ::fuzz::check(other_order == (wrapper.value() <=> other.value()));
                        }
                        else
                        {
                            ::fuzz::check(other_order == (static_cast<::std::int64_t>(wrapper.value()) <=>
                                                          static_cast<::std::int64_t>(other.value())));
                        }
                    });
                }
            }
        }
    }

    /// 用一组输入测试指定数据类型的格式包装器
    template <typename type>
    void fuzz_wrapper(::fuzz::reader& input)
    {
        constexpr auto capacity{sizeof(type) * 8zu};
        const auto width{static_cast<::std::size_t>(input.range(0, capacity + 4))};
        auto format{::make_format(input)};
        const auto value{::make_value<type>(input)};
        try
        {
            const ::verilator_utils::format_wrapper<type> wrapper{value, width, ::std::move(format)};
            ::check_wrapper(wrapper, value, width);
            ::check_wrapper_extra(wrapper);
        }
        catch(const ::verilator_utils::assertion_error&)
        {
            // 框架的运行时校验拒绝非法输入，属于期望行为：这里只需结束本次用例
            return;
        }
    }

    int do_fuzz(const ::std::uint8_t* data, ::std::size_t size)
    {
        if(size < 3) { return 0; }
        ::fuzz::reader input{data, size};
        switch(::fuzz::read_opcode<type_op_t>(input))
        {
            case type_op_t::uint64: ::fuzz_wrapper<::std::uint64_t>(input); break;
            case type_op_t::int64: ::fuzz_wrapper<::std::int64_t>(input); break;
            case type_op_t::float32: ::fuzz_wrapper<float>(input); break;
            case type_op_t::float64: ::fuzz_wrapper<double>(input); break;
            case type_op_t::boolean: ::fuzz_wrapper<bool>(input); break;
            case type_op_t::vlwide1: ::fuzz_wrapper<::VlWide<1>>(input); break;
            case type_op_t::vlwide2: ::fuzz_wrapper<::VlWide<2>>(input); break;
            case type_op_t::vlwide4: ::fuzz_wrapper<::VlWide<4>>(input); break;
            case type_op_t::total_ops: ::std::unreachable();  // 取模映射不会产生该取值
        }
        return 0;
    }
}  // namespace

TEST_SUITE("verilator_utils/fuzz")
{
    TEST_CASE("format") { ::fuzz::run_fuzz_test(&::do_fuzz); }
}
