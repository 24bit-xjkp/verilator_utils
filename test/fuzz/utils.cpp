#include <doctest_macros.hpp>
import fuzz_support;

namespace
{
    using namespace ::verilator_utils;

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

        // 加法：仅在不超过uint64上限时成功
        if(lhs_rep > max - rhs_rep)
        {
            ::fuzz::check(rejected([&] { (void)(lhs + rhs); }));
        }
        else
        {
            ::fuzz::check(static_cast<::std::uint64_t>(lhs + rhs) == lhs_rep + rhs_rep);
        }

        // 减法：仅在结果非负时成功
        if(lhs_rep < rhs_rep)
        {
            ::fuzz::check(rejected([&] { (void)(lhs - rhs); }));
        }
        else
        {
            ::fuzz::check(static_cast<::std::uint64_t>(lhs - rhs) == lhs_rep - rhs_rep);
        }

        // 整数乘法：仅在不超过uint64上限时成功
        if(lhs_rep != 0 && rhs_rep > max / lhs_rep)
        {
            ::fuzz::check(rejected([&] { (void)(lhs * rhs_rep); }));
        }
        else
        {
            ::fuzz::check(static_cast<::std::uint64_t>(lhs * rhs_rep) == lhs_rep * rhs_rep);
        }

        // 整数除法：0作为除数必须被拒绝
        if(rhs_rep == 0)
        {
            ::fuzz::check(rejected([&] { (void)(lhs / rhs_rep); }));
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
            ::fuzz::check(rejected([&] { (void)(lhs * double_rhs); }));
        }

        // 浮点除法：除数必须为正，结果必须小于uint64上限
        const auto quotient{static_cast<double>(lhs_rep) / double_rhs};
        if(double_rhs > 0. && quotient < static_cast<double>(max))
        {
            ::fuzz::check(static_cast<::std::uint64_t>(lhs / double_rhs) == static_cast<::std::uint64_t>(::std::round(quotient)));
        }
        else
        {
            ::fuzz::check(rejected([&] { (void)(lhs / double_rhs); }));
        }

        // 浮点构造：必须非负且小于uint64上限
        if(double_rhs >= 0. && double_rhs < static_cast<double>(max))
        {
            ::fuzz::check(static_cast<::std::uint64_t>(::verilator_utils::femtosecond_t{double_rhs}) ==
                          static_cast<::std::uint64_t>(::std::round(double_rhs)));
        }
        else
        {
            ::fuzz::check(rejected([&] { (void)::verilator_utils::femtosecond_t{double_rhs}; }));
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
            ::fuzz::check(rejected([&] { (void)::verilator_utils::width_cast(value, width); }));
            return;
        }

        const auto ext_value{static_cast<::std::int64_t>(value)};
        const auto shift{64zu - width};
        // 与width_cast使用相同的算术右移边界
        const auto min{::std::numeric_limits<::std::int64_t>::min() >> shift};  // NOLINT(bugprone-signed-bitwise)
        const auto max{::std::numeric_limits<::std::int64_t>::max() >> shift};  // NOLINT(bugprone-signed-bitwise)
        if(ext_value < min || ext_value > max)
        {
            ::fuzz::check(rejected([&] { (void)::verilator_utils::width_cast(value, width); }));
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
            ::fuzz::check(rejected([&] { (void)::verilator_utils::width_cast(value, width); }));
            return;
        }

        const auto ext_value{static_cast<::std::uint64_t>(value)};
        if(width < 64 && (ext_value >> width) != 0)
        {
            ::fuzz::check(rejected([&] { (void)::verilator_utils::width_cast(value, width); }));
            return;
        }
        const auto mask{width == 64 ? ::std::numeric_limits<::std::uint64_t>::max() : (::std::uint64_t{1} << width) - 1u};
        ::fuzz::check(::verilator_utils::width_cast(value, width) == (ext_value & mask));
    }

    int do_fuzz(const ::std::uint8_t* data, ::std::size_t size)
    {
        if(size < 2) { return 0; }
        ::fuzz::reader input{data, size};
        switch(input.u8() % 8u)
        {
            case 0: fuzz_femtosecond(input); break;
            case 1: fuzz_signed_width_cast<::std::int8_t>(input); break;
            case 2: fuzz_signed_width_cast<::std::int16_t>(input); break;
            case 3: fuzz_signed_width_cast<::std::int32_t>(input); break;
            case 4: fuzz_signed_width_cast<::std::int64_t>(input); break;
            case 5: fuzz_unsigned_width_cast<::std::uint8_t>(input); break;
            case 6: fuzz_unsigned_width_cast<::std::uint16_t>(input); break;
            case 7: fuzz_unsigned_width_cast<::std::uint32_t>(input); break;
            default: fuzz_unsigned_width_cast<::std::uint64_t>(input); break;
        }
        return 0;
    }
}  // namespace

TEST_SUITE("verilator_utils/fuzz")
{
    TEST_CASE("utils") { ::fuzz::run_fuzz_test(&::do_fuzz); }
}
