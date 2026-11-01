module;
export module verilator_utils:lfsr;
import :utils;

namespace
{
    using namespace ::std::string_view_literals;
}

namespace verilator_utils::detail
{
    /// 3~64位LFSR m序列反馈系数表
    constexpr ::std::array lfsr_feedback_mask_table{
        //  3:  x^3 + x^2 + 1
        (1zu << 2zu) | (1zu << 0zu),
        //  4:  x^4 + x^3 + 1
        (1zu << 3zu) | (1zu << 0zu),
        //  5:  x^5 + x^3 + 1
        (1zu << 3zu) | (1zu << 0zu),
        //  6:  x^6 + x^5 + 1
        (1zu << 5zu) | (1zu << 0zu),
        //  7:  x^7 + x^6 + 1
        (1zu << 6zu) | (1zu << 0zu),
        //  8:  x^8 + x^6 + x^5 + x^4 + 1
        (1zu << 6zu) | (1zu << 5zu) | (1zu << 4zu) | (1zu << 0zu),
        //  9:  x^9 + x^5 + 1
        (1zu << 5zu) | (1zu << 0zu),
        // 10:  x^10 + x^7 + 1
        (1zu << 7zu) | (1zu << 0zu),
        // 11:  x^11 + x^9 + 1
        (1zu << 9zu) | (1zu << 0zu),
        // 12:  x^12 + x^11 + x^10 + x^4 + 1
        (1zu << 11zu) | (1zu << 10zu) | (1zu << 4zu) | (1zu << 0zu),
        // 13:  x^13 + x^12 + x^11 + x^8 + 1
        (1zu << 12zu) | (1zu << 11zu) | (1zu << 8zu) | (1zu << 0zu),
        // 14:  x^14 + x^13 + x^12 + x^2 + 1
        (1zu << 13zu) | (1zu << 12zu) | (1zu << 2zu) | (1zu << 0zu),
        // 15:  x^15 + x^14 + 1
        (1zu << 14zu) | (1zu << 0zu),
        // 16:  x^16 + x^14 + x^13 + x^11 + 1
        (1zu << 14zu) | (1zu << 13zu) | (1zu << 11zu) | (1zu << 0zu),
        // 17:  x^17 + x^14 + 1
        (1zu << 14zu) | (1zu << 0zu),
        // 18:  x^18 + x^11 + 1
        (1zu << 11zu) | (1zu << 0zu),
        // 19:  x^19 + x^18 + x^17 + x^14 + 1
        (1zu << 18zu) | (1zu << 17zu) | (1zu << 14zu) | (1zu << 0zu),
        // 20:  x^20 + x^17 + 1
        (1zu << 17zu) | (1zu << 0zu),
        // 21:  x^21 + x^19 + 1
        (1zu << 19zu) | (1zu << 0zu),
        // 22:  x^22 + x^21 + 1
        (1zu << 21zu) | (1zu << 0zu),
        // 23:  x^23 + x^18 + 1
        (1zu << 18zu) | (1zu << 0zu),
        // 24:  x^24 + x^23 + x^22 + x^17 + 1
        (1zu << 23zu) | (1zu << 22zu) | (1zu << 17zu) | (1zu << 0zu),
        // 25:  x^25 + x^22 + 1
        (1zu << 22zu) | (1zu << 0zu),
        // 26:  x^26 + x^6 + x^2 + x + 1
        (1zu << 6zu) | (1zu << 2zu) | (1zu << 1zu) | (1zu << 0zu),
        // 27:  x^27 + x^5 + x^2 + x + 1
        (1zu << 5zu) | (1zu << 2zu) | (1zu << 1zu) | (1zu << 0zu),
        // 28:  x^28 + x^25 + 1
        (1zu << 25zu) | (1zu << 0zu),
        // 29:  x^29 + x^27 + 1
        (1zu << 27zu) | (1zu << 0zu),
        // 30:  x^30 + x^6 + x^4 + x + 1
        (1zu << 6zu) | (1zu << 4zu) | (1zu << 1zu) | (1zu << 0zu),
        // 31:  x^31 + x^28 + 1
        (1zu << 28zu) | (1zu << 0zu),
        // 32:  x^32 + x^22 + x^2 + x + 1
        (1zu << 22zu) | (1zu << 2zu) | (1zu << 1zu) | (1zu << 0zu),
        // 33:  x^33 + x^20 + 1
        (1zu << 20zu) | (1zu << 0zu),
        // 34:  x^34 + x^27 + x^2 + x + 1
        (1zu << 27zu) | (1zu << 2zu) | (1zu << 1zu) | (1zu << 0zu),
        // 35:  x^35 + x^33 + 1
        (1zu << 33zu) | (1zu << 0zu),
        // 36:  x^36 + x^25 + 1
        (1zu << 25zu) | (1zu << 0zu),
        // 37:  x^37 + x^5 + x^4 + x^3 + x^2 + x + 1
        (1zu << 5zu) | (1zu << 4zu) | (1zu << 3zu) | (1zu << 2zu) | (1zu << 1zu) | (1zu << 0zu),
        // 38:  x^38 + x^6 + x^5 + x + 1
        (1zu << 6zu) | (1zu << 5zu) | (1zu << 1zu) | (1zu << 0zu),
        // 39:  x^39 + x^35 + 1
        (1zu << 35zu) | (1zu << 0zu),
        // 40:  x^40 + x^38 + x^21 + x^19 + 1
        (1zu << 38zu) | (1zu << 21zu) | (1zu << 19zu) | (1zu << 0zu),
        // 41:  x^41 + x^38 + 1
        (1zu << 38zu) | (1zu << 0zu),
        // 42:  x^42 + x^41 + x^20 + x^19 + 1
        (1zu << 41zu) | (1zu << 20zu) | (1zu << 19zu) | (1zu << 0zu),
        // 43:  x^43 + x^42 + x^38 + x^37 + 1
        (1zu << 42zu) | (1zu << 38zu) | (1zu << 37zu) | (1zu << 0zu),
        // 44:  x^44 + x^43 + x^18 + x^17 + 1
        (1zu << 43zu) | (1zu << 18zu) | (1zu << 17zu) | (1zu << 0zu),
        // 45:  x^45 + x^44 + x^42 + x^41 + 1
        (1zu << 44zu) | (1zu << 42zu) | (1zu << 41zu) | (1zu << 0zu),
        // 46:  x^46 + x^45 + x^26 + x^25 + 1
        (1zu << 45zu) | (1zu << 26zu) | (1zu << 25zu) | (1zu << 0zu),
        // 47:  x^47 + x^42 + 1
        (1zu << 42zu) | (1zu << 0zu),
        // 48:  x^48 + x^47 + x^21 + x^20 + 1
        (1zu << 47zu) | (1zu << 21zu) | (1zu << 20zu) | (1zu << 0zu),
        // 49:  x^49 + x^40 + 1
        (1zu << 40zu) | (1zu << 0zu),
        // 50:  x^50 + x^49 + x^24 + x^23 + 1
        (1zu << 49zu) | (1zu << 24zu) | (1zu << 23zu) | (1zu << 0zu),
        // 51:  x^51 + x^50 + x^36 + x^35 + 1
        (1zu << 50zu) | (1zu << 36zu) | (1zu << 35zu) | (1zu << 0zu),
        // 52:  x^52 + x^49 + 1
        (1zu << 49zu) | (1zu << 0zu),
        // 53:  x^53 + x^52 + x^38 + x^37 + 1
        (1zu << 52zu) | (1zu << 38zu) | (1zu << 37zu) | (1zu << 0zu),
        // 54:  x^54 + x^53 + x^18 + x^17 + 1
        (1zu << 53zu) | (1zu << 18zu) | (1zu << 17zu) | (1zu << 0zu),
        // 55:  x^55 + x^31 + 1
        (1zu << 31zu) | (1zu << 0zu),
        // 56:  x^56 + x^55 + x^35 + x^34 + 1
        (1zu << 55zu) | (1zu << 35zu) | (1zu << 34zu) | (1zu << 0zu),
        // 57:  x^57 + x^50 + 1
        (1zu << 50zu) | (1zu << 0zu),
        // 58:  x^58 + x^39 + 1
        (1zu << 39zu) | (1zu << 0zu),
        // 59:  x^59 + x^58 + x^38 + x^37 + 1
        (1zu << 58zu) | (1zu << 38zu) | (1zu << 37zu) | (1zu << 0zu),
        // 60:  x^60 + x^59 + 1
        (1zu << 59zu) | (1zu << 0zu),
        // 61:  x^61 + x^60 + x^46 + x^45 + 1
        (1zu << 60zu) | (1zu << 46zu) | (1zu << 45zu) | (1zu << 0zu),
        // 62:  x^62 + x^61 + x^6 + x^5 + 1
        (1zu << 61zu) | (1zu << 6zu) | (1zu << 5zu) | (1zu << 0zu),
        // 63:  x^63 + x^62 + 1
        (1zu << 62zu) | (1zu << 0zu),
        // 64:  x^64 + x^63 + x^61 + x^60 + 1
        (1zu << 63zu) | (1zu << 61zu) | (1zu << 60zu) | (1zu << 0zu),
    };

    /**
     * @brief 检查LFSR生成器参数是否合法
     *
     * @param width LFSR宽度
     * @param feedback_mask 反馈表达式
     * @param initial_value LFSR初始值
     */
    void check_lfsr_generator_args(::std::size_t width, ::std::uint64_t& feedback_mask, ::std::uint64_t initial_value)
    {
        ::verilator_utils::check{}(width >= 3 && width <= 64, "LFSR宽度{}超出范围[3, 64]"sv, width);
        ::verilator_utils::check{}(initial_value != 0, "初始值为0时LFSR输出恒为0"sv);
        if(width != 64)
        {
            ::verilator_utils::check{}((feedback_mask >> width) == 0, "反馈表达式宽度不应超过LFSR宽度{}"sv, width);
            ::verilator_utils::check{}((initial_value >> width) == 0, "初始值宽度不应超过LFSR宽度{}"sv, width);
        }

        if(feedback_mask == 0) { feedback_mask = ::verilator_utils::detail::lfsr_feedback_mask_table[width - 3]; }
        ::verilator_utils::check{}((feedback_mask & 1zu) != 0, "反馈表达式必须包含常数项"sv);
    }
}  // namespace verilator_utils::detail

export namespace verilator_utils
{
    /**
     * @brief 斐波那契型LFSR生成器，输出序列长度无限
     *
     * @param width LFSR宽度，取值范围为[3, 64]
     * @param feedback_mask 反馈表达式，省略最高次项但包含常数项，为0表示使用m序列对应的反馈表达式
     * @param initial_value LFSR初始值
     * @return 生成器
     */
    [[nodiscard]] ::verilator_utils::generator<bool>
        fibonacci_lfsr_generator(::std::size_t width, ::std::uint64_t feedback_mask = 0, ::std::uint64_t initial_value = 1)
    {
        ::verilator_utils::detail::check_lfsr_generator_args(width, feedback_mask, initial_value);
        ::std::uint64_t value{initial_value};
        while(true)
        {
            co_yield static_cast<bool>(value & 1zu);
            auto feedback_value{static_cast<::std::uint64_t>(::std::popcount(value & feedback_mask)) % 2};
            value = value >> 1zu | feedback_value << (width - 1);
        }
    }

    /**
     * @brief 伽罗瓦型LFSR生成器，输出序列长度无限
     *
     * @param width LFSR宽度，取值范围为[3, 64]
     * @param feedback_mask 反馈表达式，省略最高次项但包含常数项，为0表示使用m序列对应的反馈表达式
     * @param initial_value LFSR初始值
     * @return 生成器
     */
    [[nodiscard]] ::verilator_utils::generator<bool>
        galois_lfsr_generator(::std::size_t width, ::std::uint64_t feedback_mask = 0, ::std::uint64_t initial_value = 1)
    {
        ::verilator_utils::detail::check_lfsr_generator_args(width, feedback_mask, initial_value);
        ::std::uint64_t value{initial_value};
        while(true)
        {
            const auto out{value & 1zu};
            co_yield static_cast<bool>(out);
            const auto masked_broadcast_out{(0zu - out) & feedback_mask};
            value = (value ^ masked_broadcast_out) >> 1zu | out << (width - 1);
        }
    }
}  // namespace verilator_utils
