#include <verilator_fwd.hpp>
#include <doctest_macros.hpp>
import verilator_utils.full;
#include <unit_test_rtl_counter_verilator.h>
#include <verilator_bwd.hpp>

TEST_SUITE("counter")
{
    using namespace verilator_utils;
    using dut_t = unit_test_rtl_counter_verilator;
    using dut_context_t = dut_context<dut_t, VerilatedFstC>;
    namespace views = std::views;

    struct port_t
    {
        bit_slice<CData> clk;
        bit_slice<CData> rst;
        bit_slice<CData> enable;
        vector_slice<CData> count;
        bit_slice<CData> overflow;

        inline explicit port_t(dut_t& dut) :
            clk{dut.clk}, rst{dut.rst}, enable{dut.enable, boolean}, count{dut.count, 4, dec_unsigned},
            overflow{dut.overflow, boolean}
        {
        }
    };

    constexpr dut_context_option option{.coverage = true, .time_precision = verilator_time_unit::ns};

    TEST_CASE("counter")
    {
        dut_context_t ctx{option};
        port_t port{ctx.get_dut()};

        ctx.add_task(generate_clock(port.clk, 2_ns));
        const auto do_verify{
            [&] -> task<void> {
                constexpr static auto period{16zu};
                port.enable = false;
                co_await generate_reset(port.rst, port.clk);
                port.enable = true;
                const auto expected{views::iota(1zu) | views::transform([&](std::uint64_t value) {
                                        constexpr static auto mask{(1zu << 4zu) - 1zu};
                                        auto count{value & mask};
                                        auto overflow{value >> 4zu != 0 && count == 0};
                                        return std::pair{
                                            format_wrapper{count,    port.count.dump_format()   },
                                            format_wrapper{overflow, port.overflow.dump_format()},
                                        };
                                    }) |
                                    views::take(period * 3)};

                for(auto&& [count, overflow]: expected)
                {
                    co_await verify_at(port.clk, [&] {
                        CHECK_EQ(port.count, count);
                        CHECK_EQ(port.overflow, overflow);
                    });
                }

                auto previous_count{port.count.dump()};
                auto previous_overflow{port.overflow.dump<bool>()};
                for(auto _: views::iota(0zu, period))
                {
                    co_await wait_stimulate(port.clk);
                    port.enable = false;
                    co_await verify_at(port.clk, [&] {
                        CHECK_EQ(port.count, previous_count);
                        CHECK_EQ(port.overflow, previous_overflow);
                    });
                }

                co_await wait_stimulate(port.clk);
                co_await eval_finish();
            },
        };
        ctx.add_task(do_verify());
        ctx.loop_until_finish();
    }
}
