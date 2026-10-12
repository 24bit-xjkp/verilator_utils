#include <doctest_macros.hpp>
import fuzz_support;

namespace
{
    /**
     * @brief 框架工具的操作码
     */
    enum class op_t : ::std::uint8_t
    {
        /// femtosecond_t的构造与算术
        femtosecond,
        /// int8_t的width_cast
        signed_width_cast_int8,
        /// int16_t的width_cast
        signed_width_cast_int16,
        /// int32_t的width_cast
        signed_width_cast_int32,
        /// int64_t的width_cast
        signed_width_cast_int64,
        /// uint8_t的width_cast
        unsigned_width_cast_uint8,
        /// uint16_t的width_cast
        unsigned_width_cast_uint16,
        /// uint32_t的width_cast
        unsigned_width_cast_uint32,
        /// uint64_t的width_cast
        unsigned_width_cast_uint64,
        /// DUT上下文统计信息的格式化
        dut_context_stats,
        /// 操作码总数
        total_ops,
    };

    /// 执行一段期望被框架校验拒绝的操作
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

    /// 测试femtosecond_t的构造与算术
    void fuzz_femtosecond(::fuzz::reader& input)
    {
        constexpr auto max{::std::numeric_limits<::std::uint64_t>::max()};
        const auto lhs_rep{input.u64()};
        const auto rhs_rep{input.u64()};
        const ::verilator_utils::femtosecond_t lhs{lhs_rep};
        const ::verilator_utils::femtosecond_t rhs{rhs_rep};

        ::fuzz::check(static_cast<::std::uint64_t>(lhs) == lhs_rep);
        ::fuzz::check((lhs <=> rhs) == (lhs_rep <=> rhs_rep));
        // 与自身比较必须相等
        ::fuzz::check((lhs <=> ::verilator_utils::femtosecond_t{lhs_rep}) == ::std::strong_ordering::equal);

        // 加法：仅在不超过uint64上限时成功
        if(lhs_rep > max - rhs_rep)
        {
            ::fuzz::check(::rejected([&] { (void)(lhs + rhs); }));
        }
        else
        {
            ::fuzz::check(static_cast<::std::uint64_t>(lhs + rhs) == lhs_rep + rhs_rep);
        }

        // 减法：仅在结果非负时成功
        if(lhs_rep < rhs_rep)
        {
            ::fuzz::check(::rejected([&] { (void)(lhs - rhs); }));
        }
        else
        {
            ::fuzz::check(static_cast<::std::uint64_t>(lhs - rhs) == lhs_rep - rhs_rep);
        }

        // 整数乘法：仅在不超过uint64上限时成功
        if(lhs_rep != 0 && rhs_rep > max / lhs_rep)
        {
            ::fuzz::check(::rejected([&] { (void)(lhs * rhs_rep); }));
        }
        else
        {
            ::fuzz::check(static_cast<::std::uint64_t>(lhs * rhs_rep) == lhs_rep * rhs_rep);
        }

        // 整数除法：0作为除数必须被拒绝
        if(rhs_rep == 0)
        {
            ::fuzz::check(::rejected([&] { (void)(lhs / rhs_rep); }));
        }
        else
        {
            ::fuzz::check(static_cast<::std::uint64_t>(lhs / rhs_rep) == lhs_rep / rhs_rep);
        }

        // 浮点乘法：乘数非负，结果必须小于uint64上限
        const auto double_rhs{::std::bit_cast<double>(input.u64())};
        const auto product{static_cast<double>(lhs_rep) * double_rhs};
        if(double_rhs >= 0. && product < static_cast<double>(max))
        {
            ::fuzz::check(static_cast<::std::uint64_t>(lhs * double_rhs) == static_cast<::std::uint64_t>(::std::round(product)));
        }
        else
        {
            ::fuzz::check(::rejected([&] { (void)(lhs * double_rhs); }));
        }

        // 浮点除法：除数必须为正，结果必须小于uint64上限
        const auto quotient{static_cast<double>(lhs_rep) / double_rhs};
        if(double_rhs > 0. && quotient < static_cast<double>(max))
        {
            ::fuzz::check(static_cast<::std::uint64_t>(lhs / double_rhs) == static_cast<::std::uint64_t>(::std::round(quotient)));
        }
        else
        {
            ::fuzz::check(::rejected([&] { (void)(lhs / double_rhs); }));
        }

        // 浮点构造：必须非负且小于uint64上限
        if(double_rhs >= 0. && double_rhs < static_cast<double>(max))
        {
            ::fuzz::check(static_cast<::std::uint64_t>(::verilator_utils::femtosecond_t{double_rhs}) ==
                          static_cast<::std::uint64_t>(::std::round(double_rhs)));
        }
        else
        {
            ::fuzz::check(::rejected([&] { (void)::verilator_utils::femtosecond_t{double_rhs}; }));
        }
    }

    /// 测试width_cast的有符号版本
    template <::std::signed_integral type>
    void fuzz_signed_width_cast(::fuzz::reader& input)
    {
        const auto value{static_cast<type>(input.u64())};
        const auto width{static_cast<::std::size_t>(input.range(0, 70))};

        if(width < 2 || width > 64)
        {
            ::fuzz::check(::rejected([&] { (void)::verilator_utils::width_cast(value, width); }));
            return;
        }

        const auto ext_value{static_cast<::std::int64_t>(value)};
        const auto shift{64zu - width};
        // 与width_cast使用相同的算术右移边界
        const auto min{::std::numeric_limits<::std::int64_t>::min() >> shift};  // NOLINT(bugprone-signed-bitwise)
        const auto max{::std::numeric_limits<::std::int64_t>::max() >> shift};  // NOLINT(bugprone-signed-bitwise)
        if(ext_value < min || ext_value > max)
        {
            ::fuzz::check(::rejected([&] { (void)::verilator_utils::width_cast(value, width); }));
            return;
        }
        ::fuzz::check(::verilator_utils::width_cast(value, width) ==
                      (static_cast<::std::uint64_t>(ext_value) & (::std::numeric_limits<::std::uint64_t>::max() >> shift)));
    }

    /// 测试width_cast的无符号版本
    template <::std::unsigned_integral type>
    void fuzz_unsigned_width_cast(::fuzz::reader& input)
    {
        const auto value{static_cast<type>(input.u64())};
        const auto width{static_cast<::std::size_t>(input.range(0, 70))};

        if(width < 1 || width > 64)
        {
            ::fuzz::check(::rejected([&] { (void)::verilator_utils::width_cast(value, width); }));
            return;
        }

        const auto ext_value{static_cast<::std::uint64_t>(value)};
        if(width < 64 && (ext_value >> width) != 0)
        {
            ::fuzz::check(::rejected([&] { (void)::verilator_utils::width_cast(value, width); }));
            return;
        }
        const auto mask{width == 64 ? ::std::numeric_limits<::std::uint64_t>::max() : (::std::uint64_t{1} << width) - 1u};
        ::fuzz::check(::verilator_utils::width_cast(value, width) == (ext_value & mask));
    }

    /// 测试DUT上下文统计信息的格式化
    void fuzz_dut_context_stats(::fuzz::reader& input)
    {
        const ::verilator_utils::dut_context_stats stats{.simtime_str = ::std::format("{}fs", input.u64()),
                                                         .speed_str =
                                                             ::std::format("{:.3f}ns/s", static_cast<double>(input.u64())),
                                                         .threads = static_cast<::std::size_t>(input.u64()),
                                                         .simtime = static_cast<double>(input.u64()),
                                                         .walltime = static_cast<double>(input.u64()) / 1000.,
                                                         .cputime = static_cast<double>(input.u64()) / 1000.,
                                                         .speed = static_cast<double>(input.u64()),
                                                         .memory_peak = static_cast<double>(input.u64()) / 1024.};

        // 统计信息的格式化：parse与format两条路径
        ::fuzz::check(!::std::format("{}", stats).empty());
        // doctest的值转换接口
        ::fuzz::check(::doctest::StringMaker<::verilator_utils::dut_context_stats>::convert(stats).size() != 0zu);
        // 不支持的格式符必须被格式化器拒绝
        try
        {
            (void)::std::vformat("{:x}", ::std::make_format_args(stats));
            ::fuzz::fail("invalid format specifier was accepted");
        }
        catch(const ::std::format_error& error)
        {
            ::fuzz::check(!::std::string_view{error.what()}.empty());
        }
    }

    int do_fuzz(const ::std::uint8_t* data, ::std::size_t size)
    {
        if(size < 2) { return 0; }
        // 解码器的边界行为：按序读取与耗尽后的越界读取
        ::fuzz::reader boundary{data, size};
        const ::std::span<const ::std::uint8_t> bytes{data, size};
        for(auto i{0zu}; i != size; ++i) { ::fuzz::check(boundary.u8() == bytes[i]); }
        ::fuzz::check(boundary.empty());
        ::fuzz::check(boundary.u8() == 0u);
        ::fuzz::check(boundary.uint(9) == 0u);

        ::fuzz::reader input{data, size};
        switch(::fuzz::read_opcode<op_t>(input))
        {
            case op_t::femtosecond: ::fuzz_femtosecond(input); break;
            case op_t::signed_width_cast_int8: ::fuzz_signed_width_cast<::std::int8_t>(input); break;
            case op_t::signed_width_cast_int16: ::fuzz_signed_width_cast<::std::int16_t>(input); break;
            case op_t::signed_width_cast_int32: ::fuzz_signed_width_cast<::std::int32_t>(input); break;
            case op_t::signed_width_cast_int64: ::fuzz_signed_width_cast<::std::int64_t>(input); break;
            case op_t::unsigned_width_cast_uint8: ::fuzz_unsigned_width_cast<::std::uint8_t>(input); break;
            case op_t::unsigned_width_cast_uint16: ::fuzz_unsigned_width_cast<::std::uint16_t>(input); break;
            case op_t::unsigned_width_cast_uint32: ::fuzz_unsigned_width_cast<::std::uint32_t>(input); break;
            case op_t::unsigned_width_cast_uint64: ::fuzz_unsigned_width_cast<::std::uint64_t>(input); break;
            case op_t::dut_context_stats: ::fuzz_dut_context_stats(input); break;
            case op_t::total_ops: ::std::unreachable();  // 取模映射不会产生该取值
        }
        return 0;
    }
}  // namespace

TEST_SUITE("verilator_utils/fuzz")
{
    TEST_CASE("utils") { ::fuzz::run_fuzz_test(&::do_fuzz); }
}
