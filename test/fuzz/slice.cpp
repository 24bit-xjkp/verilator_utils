#include <doctest_macros.hpp>
import fuzz_support;

namespace
{
    using namespace ::verilator_utils;
    using namespace ::verilator_utils::verilator;

    /// 数据类型的位容量
    template <typename type>
    constexpr ::std::size_t capacity{sizeof(type) * 8zu};

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

    /// 把存储数据转换为位模型，索引i对应数据的第i位
    template <typename type>
    [[nodiscard]] ::std::vector<bool> to_bits(const type& data)
    {
        ::std::vector<bool> bits(capacity<type>);
        if constexpr(::verilator_utils::is_vl_wide<type>)
        {
            constexpr ::std::size_t word_width{::std::numeric_limits<::EData>::digits};
            for(::std::size_t word{}; word != type::Words; ++word)
            {
                for(::std::size_t bit{}; bit != word_width; ++bit)
                {
                    bits[word * word_width + bit] = ((data.m_storage[word] >> bit) & ::EData{1}) != 0;
                }
            }
        }
        else
        {
            const auto value{static_cast<::std::uint64_t>(data)};
            for(::std::size_t bit{}; bit != capacity<type>; ++bit) { bits[bit] = ((value >> bit) & 1u) != 0; }
        }
        return bits;
    }

    /// 从位模型中取出不超过64位的一段，低位对齐
    [[nodiscard]] ::std::uint64_t pack_bits(const ::std::vector<bool>& bits, ::std::size_t right, ::std::size_t width)
    {
        ::std::uint64_t result{};
        for(::std::size_t i{}; i != width; ++i)
        {
            if(bits[right + i]) { result |= ::std::uint64_t{1} << i; }
        }
        return result;
    }

    /// 把不超过64位的一段写入位模型
    void write_bits(::std::vector<bool>& bits, ::std::size_t right, ::std::size_t width, ::std::uint64_t value)
    {
        for(::std::size_t i{}; i != width; ++i) { bits[right + i] = ((value >> i) & 1u) != 0; }
    }

    /// 比较存储数据与位模型
    template <typename type>
    void check_storage(const type& data, const ::std::vector<bool>& bits, const char* what)
    {
        if(to_bits(data) != bits) { ::fuzz::fail(what); }
    }

    /// 比较切片读出的值与位模型
    template <typename type>
    void check_slice(const ::verilator_utils::vector_slice<type>& slice,
                     const ::std::vector<bool>& bits,
                     ::std::size_t right,
                     const char* what)
    {
        const auto width{slice.width()};
        if constexpr(::verilator_utils::is_vl_wide<type>)
        {
            const auto value_bits{to_bits(static_cast<type>(slice))};
            for(::std::size_t i{}; i != width; ++i)
            {
                if(value_bits[i] != bits[right + i]) { ::fuzz::fail(what); }
            }
        }
        else
        {
            const auto value{static_cast<::std::uint64_t>(slice)};
            for(::std::size_t i{}; i != width; ++i)
            {
                if(((value >> i) & 1u) != (bits[right + i] ? 1u : 0u)) { ::fuzz::fail(what); }
            }
        }
    }

    /// 构造一个随机的合法范围，宽度不超过limit
    [[nodiscard]] ::std::pair<::std::size_t, ::std::size_t> make_range(::fuzz::reader& input, ::std::size_t limit)
    {
        const auto max_width{::std::min(limit, capacity<::std::uint64_t>)};
        const auto width{static_cast<::std::size_t>(input.range(1, max_width))};
        const auto right{static_cast<::std::size_t>(input.range(0, limit - width))};
        return {right + width - 1, right};
    }

    /// 位切片的读取、写入与格式检查
    template <typename type>
    void op_bit_slice(type& data, ::std::vector<bool>& bits, ::fuzz::reader& input)
    {
        const auto index{static_cast<::std::size_t>(input.range(0, capacity<type> + 3))};
        if(index >= capacity<type>)
        {
            ::fuzz::check(rejected([&] { const ::verilator_utils::bit_slice<type> slice{data, index}; }), index);
            return;
        }

        const bool with_boolean_format{input.boolean()};
        ::verilator_utils::bit_slice<type> slice{data,
                                                 index,
                                                 with_boolean_format ? ::verilator_utils::boolean
                                                                     : ::verilator_utils::data_format::dec_unsigned};
        ::fuzz::check(slice.width() == 1);
        ::fuzz::check(static_cast<::std::uint64_t>(slice) == (bits[index] ? 1u : 0u), index);
        if(with_boolean_format) { ::fuzz::check(slice.to_string() == (bits[index] ? "true" : "false")); }
        else
        {
            ::fuzz::check(slice.to_string() == (bits[index] ? "1" : "0"));
        }

        // 十六进制格式的位切片同样需要可格式化
        const ::verilator_utils::bit_slice<type> hex_slice{data, index, ::verilator_utils::hex};
        ::fuzz::check(!hex_slice.to_string().empty());
        ::std::string hex_buffer{};
        hex_slice.format_to(::std::back_inserter(hex_buffer));
        ::fuzz::check(hex_buffer == hex_slice.to_string());

        const auto value{input.u8() & 1u};
        slice = value;
        bits[index] = value != 0;
        ::fuzz::check(static_cast<::std::uint64_t>(slice) == value, index);
        check_storage(data, bits, "bit slice write");
        ::fuzz::check(rejected([&] { slice = 2u; }));
        check_storage(data, bits, "rejected bit slice write");

        // 与格式包装器互相赋值：布尔格式的包装器会把底层值规范化为0或1
        const ::verilator_utils::format_wrapper<::std::uint64_t> same{value, 1, ::verilator_utils::data_format::boolean};
        const ::verilator_utils::format_wrapper<::std::uint64_t> other{value ^ 1u, 1, ::verilator_utils::data_format::boolean};
        slice = other;
        bits[index] = (value ^ 1u) != 0;
        check_storage(data, bits, "bit slice assign from wrapper");
        ::fuzz::check(static_cast<::std::uint64_t>(slice) == (value ^ 1u), index);
        ::fuzz::check(slice == other);
        ::fuzz::check(!(slice == same));
        if(with_boolean_format) { ::fuzz::check(slice.to_string() == ((value ^ 1u) != 0 ? "true" : "false")); }
    }

    /// 向量切片按上下界构造时的边界检查与读取
    template <typename type>
    void op_slice_range(type& data, ::std::vector<bool>& bits, ::fuzz::reader& input)
    {
        const auto lhs{static_cast<::std::size_t>(input.range(0, capacity<type> + 3))};
        const auto rhs{static_cast<::std::size_t>(input.range(0, capacity<type> + 3))};
        const bool expected_valid{lhs >= rhs && lhs < capacity<type>};

        ::std::optional<::verilator_utils::vector_slice<type>> slice{};
        try
        {
            slice.emplace(data, lhs, rhs);
        }
        catch(const ::verilator_utils::assertion_error&)
        {
            ::fuzz::check(!expected_valid, lhs);
            return;
        }
        ::fuzz::check(expected_valid, lhs);
        ::fuzz::check(slice->width() == lhs - rhs + 1);
        check_slice(*slice, bits, rhs, "slice range read");

        // 位索引访问
        if(input.boolean())
        {
            const auto index{static_cast<::std::size_t>(input.range(0, slice->width() + 2))};
            if(index >= slice->width())
            {
                ::fuzz::check(rejected([&] { (void)(*slice)[index]; }));
            }
            else
            {
                auto bit{(*slice)[index]};
                ::fuzz::check(static_cast<::std::uint64_t>(bit) == (bits[rhs + index] ? 1u : 0u), index);
                const auto value{input.u8() & 1u};
                bit = value;
                bits[rhs + index] = value != 0;
                check_storage(data, bits, "nested bit slice write");
            }
        }

        // 嵌套子切片
        if(input.boolean() && slice->width() > 1)
        {
            const auto left{static_cast<::std::size_t>(input.range(0, slice->width() - 1))};
            const auto right{static_cast<::std::size_t>(input.range(0, slice->width() - 1))};
            if(left >= right)
            {
                const auto sub{(*slice)[left, right]};
                ::fuzz::check(sub.width() == left - right + 1);
                check_slice(sub, bits, rhs + right, "nested slice read");
            }
            else
            {
                ::fuzz::check(rejected([&] { (void)(*slice)[left, right]; }));
            }
        }
    }

    /// 向量切片按宽度构造时的边界检查与读取
    template <typename type>
    void op_slice_width(type& data, ::std::vector<bool>& bits, ::fuzz::reader& input)
    {
        const auto width{static_cast<::std::size_t>(input.range(0, capacity<type> + 3))};
        const bool expected_valid{width >= 1 && width <= capacity<type>};

        ::std::optional<::verilator_utils::vector_slice<type>> slice{};
        try
        {
            slice.emplace(data, width);
        }
        catch(const ::verilator_utils::assertion_error&)
        {
            ::fuzz::check(!expected_valid, width);
            return;
        }
        ::fuzz::check(expected_valid, width);
        ::fuzz::check(slice->width() == width);
        check_slice(*slice, bits, 0, "slice width read");
        if(width <= 64)
        {
            ::fuzz::check(slice->to_string() == ::std::format("0x{:0{}x}", pack_bits(bits, 0, width), (width + 3zu) / 4zu));
        }
        else
        {
            // 宽数据的十六进制输出长度由最高字与剩余整字共同决定
            ::fuzz::check(slice->to_string().size() == (width + 3zu) / 4zu + 2zu);
        }
    }

    /// 向量切片的标量赋值
    template <typename type>
    void op_assign_scalar(type& data, ::std::vector<bool>& bits, ::fuzz::reader& input)
    {
        const auto [left, right]{make_range(input, capacity<type>)};
        const auto width{left - right + 1};
        ::verilator_utils::vector_slice<type> slice{data, left, right};

        const auto mask{width == 64 ? ::std::numeric_limits<::std::uint64_t>::max() : (::std::uint64_t{1} << width) - 1u};
        const auto value{input.u64() & mask};
        slice = value;
        write_bits(bits, right, width, value);
        check_storage(data, bits, "slice assign scalar");
        check_slice(slice, bits, right, "slice assign scalar read");
        ::fuzz::check(slice == value);

        if(width < 64)
        {
            ::fuzz::check(rejected([&] { slice = value | (::std::uint64_t{1} << width); }));
            check_storage(data, bits, "rejected slice assign scalar");
        }

        // 通过格式包装器赋值
        const ::verilator_utils::format_wrapper<::std::uint64_t> wrapper{value, width, ::verilator_utils::data_format::hex};
        const auto other_value{input.u64() & mask};
        const ::verilator_utils::format_wrapper<::std::uint64_t> other{other_value, width, ::verilator_utils::data_format::hex};
        slice = other;
        write_bits(bits, right, width, other_value);
        check_storage(data, bits, "slice assign wrapper");
        ::fuzz::check(slice == other);
        ::fuzz::check(wrapper.width() == width);
    }

    /// 向量切片之间的赋值
    template <typename type>
    void op_assign_slice(type& data, ::std::vector<bool>& bits, ::fuzz::reader& input)
    {
        const auto [left, right]{make_range(input, capacity<type>)};
        const auto width{left - right + 1};
        ::verilator_utils::vector_slice<type> destination{data, left, right};

        // 源切片与目标切片等宽
        const auto source_right{static_cast<::std::size_t>(input.range(0, capacity<type> - width))};
        const auto source_value{input.u64() & (width == 64 ? ~::std::uint64_t{} : (::std::uint64_t{1} << width) - 1u)};
        ::verilator_utils::vector_slice<type> source{data, source_right + width - 1, source_right};
        source = source_value;
        write_bits(bits, source_right, width, source_value);

        // 目标宽度大于源宽度时必须拒绝
        if(left + 1 < capacity<type>)
        {
            ::verilator_utils::vector_slice<type> wider{data, left + 1, right};
            ::fuzz::check(wider.width() == width + 1);
            ::fuzz::check(rejected([&] { wider = source; }));
            check_storage(data, bits, "rejected slice assign slice");
        }

        destination = source;
        write_bits(bits, right, width, source_value);
        check_storage(data, bits, "slice assign slice");
        // 源与目标重叠时，写入目标会同时改变源的当前值，此时不能再比较两者
        if(right + width <= source_right || source_right + width <= right) { ::fuzz::check(destination == source); }
    }

    /// 切片的dump与格式打包
    template <typename type>
    void op_dump(type& data, ::std::vector<bool>& bits, ::fuzz::reader& input)
    {
        const auto [left, right]{make_range(input, capacity<type>)};
        const auto width{left - right + 1};
        const auto format{input.boolean() ? ::verilator_utils::bin : ::verilator_utils::hex};
        const ::verilator_utils::vector_slice<type> slice{data, left, right, format};

        if constexpr(::verilator_utils::is_vl_wide<type>)
        {
            const auto dumped{slice.template dump<type>()};
            ::fuzz::check(dumped.width() == width);
            ::fuzz::check(to_bits(dumped.value()) == to_bits(static_cast<type>(slice)));
            ::fuzz::check(dumped.to_string() == slice.to_string());
        }
        else
        {
            const auto dumped{slice.template dump<::std::uint64_t>()};
            ::fuzz::check(dumped.width() == width);
            ::fuzz::check(dumped.value() == static_cast<::std::uint64_t>(slice));
            ::fuzz::check(dumped.to_string() == slice.to_string());
        }

        // 打包格式只保留宽度与格式，重建的切片从最低位开始
        const auto packed_format{slice.dump_format()};
        ::fuzz::check(packed_format.first == width);
        const ::verilator_utils::vector_slice<type> restored{data, packed_format};
        ::fuzz::check(restored.width() == width);
        check_slice(restored, bits, 0, "slice from packed format");
    }

    /// 各种数据格式下的切片读取
    template <typename type>
    void op_slice_format(type& data, ::std::vector<bool>& bits, ::fuzz::reader& input)
    {
        constexpr auto bounded_capacity{::std::min(capacity<type>, ::std::size_t{64zu})};
        // 保证宽度不超过64位，使所有格式都能参与
        const auto [left, right]{make_range(input, bounded_capacity)};
        const auto width{left - right + 1};
        const auto packed{pack_bits(bits, right, width)};
        const ::verilator_utils::vector_slice<type> probe{data, left, right};

        switch(input.u8() % 6u)
        {
            case 0:
            {
                const auto slice{probe.convert(::verilator_utils::data_format::hex)};
                ::fuzz::check(::std::get<::std::uint64_t>(slice.to_underlying()) == packed);
                ::fuzz::check(slice.to_string() == ::std::format("0x{:0{}x}", packed, (width + 3zu) / 4zu));
                break;
            }
            case 1:
            {
                const auto slice{probe.convert(::verilator_utils::bin)};
                ::fuzz::check(::std::get<::std::uint64_t>(slice.to_underlying()) == packed);
                ::fuzz::check(slice.to_string() == ::std::format("0b{:0{}b}", packed, width));
                break;
            }
            case 2:
            {
                const auto slice{probe.convert(::verilator_utils::dec_unsigned)};
                ::fuzz::check(::std::get<::std::uint64_t>(slice.to_underlying()) == packed);
                ::fuzz::check(slice.to_string() == ::std::format("{}", packed));
                // 非枚举格式没有取值范围的限制，任意值都有效
                ::fuzz::check(slice.is_valid());
                break;
            }
            case 3:
            {
                if(width < 2) { break; }
                const auto slice{probe.convert(::verilator_utils::dec_signed)};
                const auto signed_value{::verilator_utils::data_format::dec_signed_t::to_underlying(packed, width)};
                ::fuzz::check(::std::get<::std::int64_t>(slice.to_underlying()) == signed_value);
                ::fuzz::check(slice.to_string() == ::std::format("{}", signed_value));
                break;
            }
            case 4:
            {
                if(width != 1) { break; }
                const auto slice{probe.convert(::verilator_utils::boolean)};
                ::fuzz::check(::std::get<bool>(slice.to_underlying()) == (packed != 0));
                ::fuzz::check(slice.to_string() == (packed != 0 ? "true" : "false"));
                break;
            }
            default:
            {
                ::std::vector<::std::string> enum_string{};
                const auto size{static_cast<::std::size_t>(input.range(1, 5)) * 2zu};
                for(::std::size_t i{}; i != size; ++i) { enum_string.push_back(input.short_string(6)); }
                const auto min_width{size <= 1 ? 1zu : ::std::bit_width(size - 1)};
                if(width < min_width)
                {
                    ::fuzz::check(rejected([&] { (void)probe.convert(::verilator_utils::fsm_enum(enum_string)); }));
                    break;
                }
                const auto expected_name{packed < size ? enum_string[packed] : ::std::string{}};
                const auto slice{probe.convert(::verilator_utils::fsm_enum(::std::move(enum_string)))};
                ::fuzz::check(slice.is_valid() == (packed < size));
                if(packed < size) { ::fuzz::check(slice.to_string() == expected_name); }
                break;
            }
        }
    }

    /// 构造低位为指定值的存储数据
    template <typename type>
    [[nodiscard]] type make_storage(::std::uint64_t value)
    {
        if constexpr(::verilator_utils::is_vl_wide<type>)
        {
            type data{};
            data.m_storage[0] = static_cast<::EData>(value);
            if constexpr(type::Words > 1) { data.m_storage[1] = static_cast<::EData>(value >> 32zu); }
            return data;
        }
        else
        {
            return static_cast<type>(value);
        }
    }

    /// 切片的三路比较
    template <typename type>
    void op_compare(type& data, const ::std::vector<bool>& bits, ::fuzz::reader& input)
    {
        constexpr auto max_width{::std::min(capacity<type>, ::std::size_t{64zu})};
        const auto [left, right]{make_range(input, max_width)};
        const auto width{left - right + 1};
        const auto packed{pack_bits(bits, right, width)};
        // 十六进制与二进制格式不支持三路比较，只支持相等比较
        const auto selector{input.u8() % 3u};
        auto format{::verilator_utils::data_format::format{::verilator_utils::dec_unsigned}};
        if(selector == 1u) { format = ::verilator_utils::hex; }
        if(selector == 2u) { format = ::verilator_utils::bin; }

        const ::verilator_utils::vector_slice<type> slice{data, left, right, format};
        auto same_data{make_storage<type>(packed)};
        const ::verilator_utils::vector_slice<type> same{same_data, width - 1, 0, format};

        if(selector != 0u)
        {
            ::fuzz::check(rejected([&] { (void)(slice <=> same); }));
            return;
        }

        // 数据相同的两个切片必须等价
        ::fuzz::check((slice <=> same) == ::std::partial_ordering::equivalent);
        // 与格式包装器比较
        const ::verilator_utils::format_wrapper<::std::uint64_t> wrapper{packed, 64zu, ::verilator_utils::dec_unsigned};
        ::fuzz::check((slice <=> wrapper) == ::std::partial_ordering::equivalent);
        // 位切片之间的三路比较
        if(width == 1zu)
        {
            const ::verilator_utils::bit_slice<type> lhs{data, left, ::verilator_utils::dec_unsigned};
            const ::verilator_utils::bit_slice<type> rhs{same_data, 0, ::verilator_utils::dec_unsigned};
            ::fuzz::check((lhs <=> rhs) == ::std::partial_ordering::equivalent);
            // 与格式包装器比较时要求包装器宽度为1
            const ::verilator_utils::format_wrapper<::std::uint64_t> bit_wrapper{packed & 1zu,
                                                                                 1zu,
                                                                                 ::verilator_utils::dec_unsigned};
            ::fuzz::check((lhs <=> bit_wrapper) == ::std::partial_ordering::equivalent);
        }
    }

    /// 固定宽度格式下的嵌套切片
    template <typename type>
    void op_nested_fixed_width(type& data, ::fuzz::reader& input)
    {
        // 定点数格式以double为底层表示，宽度上限为double的有效位数
        constexpr auto max_width{::std::min(capacity<type>, ::std::size_t{53zu})};
        const auto width{static_cast<::std::size_t>(input.range(2, max_width))};
        const auto integer_bit{static_cast<::std::uint8_t>(input.range(1, width - 1))};
        const auto format{::verilator_utils::unsigned_fixed_point(integer_bit,
                                                                  static_cast<::std::uint8_t>(width - integer_bit),
                                                                  input.boolean())};
        const ::verilator_utils::vector_slice<type> slice{data, width - 1, 0, format};
        ::fuzz::check(slice.width() == width);

        // 固定宽度格式下，不提供新格式的窄切片必须被拒绝
        const auto right{static_cast<::std::size_t>(input.range(0, width - 2))};
        ::fuzz::check(rejected([&] { (void)(slice[width - 2, right]); }));
        // 提供可变宽度格式后可以创建不同宽度的切片
        const auto narrower{slice[width - 2, right, ::verilator_utils::dec_unsigned]};
        ::fuzz::check(narrower.width() == width - 1 - right);
        ::fuzz::check(::std::holds_alternative<::verilator_utils::data_format::dec_unsigned_t>(narrower.format()));
    }

    /// 移位寄存器的差分测试
    template <typename type>
    void op_shift_register(::fuzz::reader& input)
    {
        constexpr auto max_width{::std::min(capacity<type>, ::std::size_t{64zu})};
        const auto depth{static_cast<::std::size_t>(input.range(0, 4))};
        const auto width{static_cast<::std::size_t>(input.range(1, max_width))};
        // 十六进制与二进制格式对所有存储类型都有效，宽数据类型也只支持这两种格式
        const auto format{input.boolean() ? ::verilator_utils::data_format::format{::verilator_utils::bin}
                                          : ::verilator_utils::data_format::format{::verilator_utils::hex}};
        const auto mask{width == 64zu ? ~::std::uint64_t{} : (::std::uint64_t{1zu} << width) - 1zu};

        ::verilator_utils::shift_register<type> reg{depth, width, format};
        // 参考模型：寄存器链与最近移出的元素
        ::std::deque<type> chain{};
        ::std::optional<type> buffered{};

        const auto steps{static_cast<::std::size_t>(input.range(1, 5))};
        for(::std::size_t step{}; step != steps; ++step)
        {
            const auto value{make_storage<type>(input.u64() & mask)};
            const bool enable{input.boolean()};
            const auto updated{reg.update(value, enable)};

            if(enable)
            {
                if(depth == 0zu) { buffered = value; }
                else
                {
                    if(chain.size() == depth)
                    {
                        buffered = chain.front();
                        chain.pop_front();
                    }
                    chain.push_back(value);
                }
            }

            ::fuzz::check(updated.has_value() == buffered.has_value());
            // buffered与updated要么同时有值要么同时为空，上面的检查已保证
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            if(updated)
            {
                ::fuzz::check(updated->width() == width);
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
                ::fuzz::check(updated->value() == *buffered);
                ::fuzz::check(updated->format().index() == format.index());
            }
        }

        // 清空后不应再返回缓冲的元素
        reg.reset();
        ::fuzz::check(!reg.update(make_storage<type>(0zu), false).has_value());
    }

    /// 未指定数据格式的切片必须被拒绝
    template <typename type>
    void op_monostate_format(type& data, ::fuzz::reader& input)
    {
        const auto index{static_cast<::std::size_t>(input.range(0, capacity<type> - 1))};
        // std::monostate表示尚未设置数据格式
        ::fuzz::check(rejected(
            [&] { const ::verilator_utils::bit_slice<type> slice{data, index, ::verilator_utils::data_format::format{}}; }));
        const auto left{static_cast<::std::size_t>(input.range(0, capacity<type> - 1))};
        const auto right{static_cast<::std::size_t>(input.range(0, left))};
        ::fuzz::check(rejected([&] {
            const ::verilator_utils::vector_slice<type> slice{data, left, right, ::verilator_utils::data_format::format{}};
        }));
    }

    /// 对指定数据类型执行一轮模糊测试
    template <typename type>
    void fuzz_type(::fuzz::reader& input)
    {
        // 初始化存储数据
        type data{};
        if constexpr(::verilator_utils::is_vl_wide<type>)
        {
            for(::std::size_t word{}; word != type::Words; ++word) { data.m_storage[word] = static_cast<::EData>(input.uint(4)); }
        }
        else
        {
            data = static_cast<type>(input.u64());
        }
        auto bits{to_bits(data)};

        const auto steps{static_cast<::std::size_t>(input.range(1, 8))};
        for(::std::size_t step{}; step != steps; ++step)
        {
            switch(input.u8() % 11u)
            {
                case 0: op_bit_slice(data, bits, input); break;
                case 1: op_slice_range(data, bits, input); break;
                case 2: op_slice_width(data, bits, input); break;
                case 3: op_assign_scalar(data, bits, input); break;
                case 4: op_assign_slice(data, bits, input); break;
                case 5: op_dump(data, bits, input); break;
                case 6: op_slice_format(data, bits, input); break;
                case 7: op_compare(data, bits, input); break;
                case 8: op_nested_fixed_width(data, input); break;
                case 9:
                    // 移位寄存器只支持格式包装器的数据类型
                    if constexpr(::verilator_utils::is_format_wrapper_data_type<type>) { op_shift_register<type>(input); }
                    break;
                default: op_monostate_format(data, input); break;
            }
        }
    }

    int do_fuzz(const ::std::uint8_t* data, ::std::size_t size)
    {
        if(size < 2) { return 0; }
        // VlWide的位容量必须等于sizeof所覆盖的位数，切片实现依赖该前提
        ::fuzz::check(sizeof(::VlWide<1>) == 4zu && sizeof(::VlWide<2>) == 8zu && sizeof(::VlWide<4>) == 16zu);

        ::fuzz::reader input{data, size};
        switch(input.u8() % 8u)
        {
            case 0: fuzz_type<::CData>(input); break;
            case 1: fuzz_type<::SData>(input); break;
            case 2: fuzz_type<::IData>(input); break;
            case 3: fuzz_type<::QData>(input); break;
            case 4: fuzz_type<::VlWide<1>>(input); break;
            case 5: fuzz_type<::VlWide<2>>(input); break;
            case 6: fuzz_type<::VlWide<3>>(input); break;
            default: fuzz_type<::VlWide<4>>(input); break;
        }
        return 0;
    }
}  // namespace

TEST_SUITE("verilator_utils/fuzz")
{
    TEST_CASE("slice") { ::fuzz::run_fuzz_test(&::do_fuzz); }
}
