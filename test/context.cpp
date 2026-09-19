#include <verilated_config.h>  // VERILATOR_PRODUCT、VERILATOR_VERSION
#include <doctest_macros.hpp>
import unit_test;

namespace
{
    /**
     * @brief 统计功能测试使用的桩DUT
     *
     * dut_context以(VerilatedContext*, 名称)构造DUT，并假定DUT会把自身注册到上下文：
     * Verilator生成的模型会在构造函数中调用VerilatedContext::addModel，
     * 未注册时上下文不会启动挂钟/CPU计时，threadsInModels也会一直为0
     */
    struct stats_dut : ::fake_dut
    {
        explicit stats_dut(::VerilatedContext* context, const char* /* unused */) : ::fake_dut{*context}
        { context->addModel(this); }

        /// Verilator生成的模型提供的收尾函数，由dut_context析构时调用
        void final() {}
    };

    /// 不使用波形记录的DUT上下文类型
    using stats_context = ::verilator_utils::dut_context<stats_dut, void>;

    /// 速度字符串中单位后缀相对秒的十进制指数
    constexpr ::std::array speed_unit_table{
        ::std::tuple{"s"sv,  0  },
        ::std::tuple{"ms"sv, -3 },
        ::std::tuple{"us"sv, -6 },
        ::std::tuple{"ns"sv, -9 },
        ::std::tuple{"ps"sv, -12},
        ::std::tuple{"fs"sv, -15},
    };

    /**
     * @brief 速度字符串的解析结果
     *
     */
    struct parsed_speed
    {
        /// 缩放后的速度数值
        double value{};
        /// 单位后缀，不包含"/s"
        ::std::string_view suffix{};
        /// 单位相对秒的十进制指数
        int exponent{};
    };

    /**
     * @brief 解析"1.234us/s"形式的速度字符串
     *
     * @param speed_str 待解析的字符串
     * @param result 解析结果，解析失败时不被修改
     * @return 是否解析成功
     */
    [[nodiscard]] bool parse_speed(::std::string_view speed_str, parsed_speed& result)
    {
        constexpr static ::std::string_view suffix_marker{"/s"sv};
        constexpr static ::std::size_t fraction_digits{3zu};
        if(!speed_str.ends_with(suffix_marker)) { return false; }
        speed_str.remove_suffix(suffix_marker.size());
        // 数值部分固定保留3位小数，小数点后第4个字符即为单位后缀的起点
        const auto dot{speed_str.rfind('.')};
        if(dot == ::std::string_view::npos || dot + fraction_digits + 1zu >= speed_str.size()) { return false; }
        const auto suffix_start{dot + fraction_digits + 1zu};
        const auto* const number_end{&speed_str[suffix_start]};
        double value{};
        const auto conversion{::std::from_chars(speed_str.data(), number_end, value)};
        // 只用具名枚举值判断失败：errc{}会被clang-tidy误报为无效的枚举默认初始化
        if(conversion.ec == ::std::errc::invalid_argument || conversion.ec == ::std::errc::result_out_of_range ||
           conversion.ptr != number_end)
        {
            return false;
        }
        const auto suffix{speed_str.substr(suffix_start)};
        const auto* const found{
            ::std::ranges::find(speed_unit_table, suffix, [](const auto& item) { return ::std::get<0>(item); })};
        if(found == speed_unit_table.end()) { return false; }
        result = parsed_speed{value, suffix, ::std::get<1>(*found)};
        return true;
    }

    /**
     * @brief 解析速度字符串，解析失败时终止当前用例
     *
     * @param speed_str 待解析的字符串
     * @return 解析结果
     */
    [[nodiscard]] parsed_speed require_parsed_speed(::std::string_view speed_str)
    {
        CAPTURE(speed_str);
        parsed_speed parsed{};
        REQUIRE_MESSAGE(parse_speed(speed_str, parsed), "速度字符串应当为\"数值+单位+/s\"的形式");
        return parsed;
    }

    /**
     * @brief 校验速度字符串与未缩放的仿真速度一致
     *
     * speed_str中的数值是速度按IEEE时间单位缩放并保留3位小数的结果，
     * 这里把数值换算回上下文的时间单位后与speed比较，允许3位小数带来的相对误差
     *
     * @param time_unit_exponent 上下文时间单位相对秒的十进制指数
     * @param stats 待校验的统计信息
     */
    void check_speed_string(int time_unit_exponent, const ::verilator_utils::dut_context_stats& stats)
    {
        const auto scaled{require_parsed_speed(stats.speed_str)};
        const auto expected{scaled.value * ::std::pow(10.0, static_cast<double>(scaled.exponent - time_unit_exponent))};
        CHECK_LE(::std::abs(expected - stats.speed), 1e-3 * expected);
        // 缩放后的数值除端点单位外都应当落在[1, 1e3]内
        if(scaled.suffix != "s"sv) { CHECK_LE(scaled.value, 1e3); }
        if(scaled.suffix != "fs"sv) { CHECK_GE(scaled.value, 1.0); }
    }

    /**
     * @brief 统计功能测试夹具
     *
     * 只驱动任务队列而不结束仿真，因此同一个夹具可以多次推进仿真时间
     */
    struct stats_fixture
    {
        stats_context ctx;
        bool started{};

        explicit stats_fixture(::verilator_utils::dut_context_option option = {}) : ctx{option} {}

        /**
         * @brief 推进仿真指定时长，返回推进结束时的统计信息
         *
         * @param duration 推进时长
         * @return 统计信息
         */
        [[nodiscard]] ::verilator_utils::dut_context_stats advance(::verilator_utils::femtosecond_t duration)
        {
            const auto wait_task{[&] -> ::verilator_utils::task<void> { co_await ::verilator_utils::wait_time(duration); }};
            ctx.add_task(wait_task());
            // initial_eval只允许执行一次，此后直接驱动仿真循环
            if(!started)
            {
                ctx.initial_eval();
                started = true;
            }
            while(!ctx.get_scheduler().empty() && !ctx.get_scheduler().is_finish()) { ctx.loop_once(); }
            return ctx.get_stats();
        }
    };

    using namespace ::verilator_utils::verilator;
}  // namespace

TEST_SUITE("verilator_utils/context")
{
    using namespace ::verilator_utils::literals;

    TEST_CASE("stats report the simulation time in the configured time unit")
    {
        // 时间单位、时间精度、推进时长、预期仿真时间与预期仿真时间字符串
        constexpr static ::std::array cases{
            ::std::tuple{::verilator_utils::verilator_time_unit::ns,
                         ::verilator_utils::verilator_time_unit::ps,
                         250_ns, 250.0,
                         "250ns"sv},
            ::std::tuple{::verilator_utils::verilator_time_unit::us,
                         ::verilator_utils::verilator_time_unit::ns,
                         1_us,   1.0,
                         "1us"sv  },
            ::std::tuple{::verilator_utils::verilator_time_unit::ps,
                         ::verilator_utils::verilator_time_unit::fs,
                         3_ps,   3.0,
                         "3ps"sv  },
            ::std::tuple{::verilator_utils::verilator_time_unit::fs_10,
                         ::verilator_utils::verilator_time_unit::fs,
                         20_fs,  2.0,
                         "20fs"sv },
        };

        for(const auto& [time_unit, time_precision, duration, expected_simtime, expected_string]: cases)
        {
            CAPTURE(time_unit);
            CAPTURE(time_precision);
            stats_fixture fixture{
                {.time_unit = time_unit, .time_precision = time_precision}
            };
            const auto stats{fixture.advance(duration)};

            CHECK_EQ(stats.simtime, expected_simtime);
            CHECK_EQ(stats.simtime_str, expected_string);
            check_speed_string(fixture.ctx.get_context().timeunit(), stats);
        }
    }

    TEST_CASE("speed string matches the unscaled speed at every scale")
    {
        stats_fixture fixture{};
        const auto time_unit_exponent{fixture.ctx.get_context().timeunit()};

        // 逐步推进仿真时间，速度依次落在ns/us/ms/s量级上
        check_speed_string(time_unit_exponent, fixture.advance(1_ps));
        check_speed_string(time_unit_exponent, fixture.advance(1_ns));
        check_speed_string(time_unit_exponent, fixture.advance(1_us));
        check_speed_string(time_unit_exponent, fixture.advance(1_ms));
    }

    TEST_CASE("speed string scales up when the simulation is faster than the time unit")
    {
        stats_fixture fixture{};
        const auto stats{fixture.advance(1_ms)};
        const auto parsed{require_parsed_speed(stats.speed_str)};

        // 1ms的仿真时间远大于上下文的挂钟时间，速度必须缩放ns以外的单位
        CHECK_NE(parsed.suffix, "ns"sv);
        check_speed_string(fixture.ctx.get_context().timeunit(), stats);
    }

    TEST_CASE("speed string scales down when the simulation is slower than the time unit")
    {
        // 以us为时间单位只推进1ps时，原始速度小于1us/s，必须缩放到更小的时间单位
        stats_fixture fixture{{.time_unit = ::verilator_utils::verilator_time_unit::us}};
        const auto stats{fixture.advance(1_ps)};
        const auto parsed{require_parsed_speed(stats.speed_str)};

        CHECK_LT(stats.speed, 1.0);
        CHECK_NE(parsed.suffix, "us"sv);
        CHECK_GE(parsed.value, 1.0);
        check_speed_string(fixture.ctx.get_context().timeunit(), stats);
    }

    TEST_CASE("stats report a zero speed in femtoseconds per second")
    {
        stats_fixture fixture{};
        const auto stats{fixture.ctx.get_stats()};

        CHECK_EQ(stats.simtime, 0.0);
        CHECK_EQ(stats.simtime_str, "0ns"sv);
        CHECK_EQ(stats.speed, 0.0);
        // 未推进仿真时速度为0，只能缩放到最小的时间单位fs
        CHECK_EQ(stats.speed_str, "0.000fs/s"sv);
        check_speed_string(fixture.ctx.get_context().timeunit(), stats);
    }

    TEST_CASE("stats report the model thread count and the process memory peak")
    {
        stats_fixture fixture{};
        const auto stats{fixture.advance(1_ns)};

        // fake_dut声明自身使用1个线程，且上下文中只注册了一个模型
        CHECK_EQ(stats.threads, 1zu);
        CHECK_EQ(stats.threads, static_cast<::std::size_t>(fixture.ctx.get_context().threadsInModels()));

        ::std::uint64_t peak_before{};
        ::std::uint64_t current{};
        ::VlOs::memUsageBytes(peak_before, current);
        const auto measured{fixture.ctx.get_stats()};
        ::std::uint64_t peak_after{};
        ::VlOs::memUsageBytes(peak_after, current);

        // 内存峰值为单调不减的进程峰值，单位为MB
        CHECK_GT(measured.memory_peak, 0.0);
        CHECK_GE(measured.memory_peak, static_cast<double>(peak_before) / 1024.0 / 1024.0);
        CHECK_LE(measured.memory_peak, static_cast<double>(peak_after) / 1024.0 / 1024.0);
    }

    TEST_CASE("stats report wall time, cpu time and the unscaled speed")
    {
        stats_fixture fixture{};
        const auto stats{fixture.advance(1_us)};

        // 挂钟与CPU计时在模型注册时启动
        CHECK_GT(stats.walltime, 0.0);
        CHECK_GE(stats.cputime, 0.0);
        // speed为未缩放的仿真速度，单位为dut时间单位每秒
        CHECK_EQ(stats.speed, stats.simtime / stats.walltime);
    }

    TEST_CASE("get_stats leaves the simulation state untouched")
    {
        stats_fixture fixture{};
        CHECK_EQ(fixture.advance(7_ns).simtime_str, "7ns"sv);

        const auto time_before{fixture.ctx.get_context().time()};
        const auto stage_before{fixture.ctx.get_scheduler().get_eval_stage()};
        const auto first{fixture.ctx.get_stats()};
        const auto second{fixture.ctx.get_stats()};

        CHECK_EQ(fixture.ctx.get_context().time(), time_before);
        CHECK_EQ(fixture.ctx.get_scheduler().get_eval_stage(), stage_before);
        CHECK_EQ(first.simtime_str, "7ns"sv);
        CHECK_EQ(first.simtime, second.simtime);
        CHECK_EQ(first.simtime_str, second.simtime_str);
        CHECK_EQ(first.threads, second.threads);
    }

    TEST_CASE("stats formatter prints every field in three lines")
    {
        stats_fixture fixture{};
        const auto stats{fixture.advance(250_ns)};
        const auto text{::std::format("{}", stats)};

        CHECK_EQ(stats.stimulator, ::std::format("{} {}", VERILATOR_PRODUCT, VERILATOR_VERSION));
        CHECK(text.starts_with(::std::format("- 仿真器: {}\n", stats.stimulator)));
        CHECK(text.contains(::std::format("- 仿真时间: {} 挂钟时间: {:.3f}s 仿真速度: {}\n",
                                          stats.simtime_str,
                                          stats.walltime,
                                          stats.speed_str)));
        CHECK(text.ends_with(::std::format("- CPU时间: {:.3f}s 并发线程数: {} 内存峰值: {:.3f}MB",
                                           stats.cputime,
                                           stats.threads,
                                           stats.memory_peak)));
        CHECK_EQ(static_cast<::std::size_t>(::std::ranges::count(text, '\n')), 2zu);
    }

    TEST_CASE("stats formatter rejects format specifiers")
    {
        stats_fixture fixture{};
        const auto stats{fixture.advance(1_ns)};

        // 使用vformat绕过编译期的格式串检查，以校验运行期的解析行为
        const auto format_with_specifier{[&](const ::verilator_utils::dut_context_stats& value) {
            return ::std::vformat("{:>20}", ::std::make_format_args(value));
        }};
        CHECK_THROWS_AS(format_with_specifier(stats), ::std::format_error);
    }

    TEST_CASE("stats string maker prefixes the formatted text with a newline")
    {
        stats_fixture fixture{};
        const auto stats{fixture.advance(1_ns)};
        const auto converted{::doctest::StringMaker<::verilator_utils::dut_context_stats>::convert(stats)};

        CHECK_EQ(::std::string{converted.c_str()}, ::std::format("\n{}", stats));
    }
}
