#include <verilator_fwd.hpp>
#include <doctest_fwd.hpp>
import std;
import std.compat;
import verilator_utils;

extern "C++"
{
#include <unit_test_rtl_counter_verilator.h>
#include <doctest.h>
#include <verilator_bwd.hpp>
}

TEST_SUITE("counter")
{
    using namespace verilator_utils;
    using namespace verilator_utils::data_format;
    using namespace verilator_utils::literals;
    using dut_t = unit_test_rtl_counter_verilator;
    using dut_context_t = dut_context<dut_t, VerilatedFstC>;
    namespace views = std::views;

    struct port_t
    {
        bit_slice<CData> clk;
        bit_slice<CData> rst;
        vector_slice<CData> count;
        bit_slice<CData> overflow;

        inline port_t(dut_t& dut) :
            clk{dut.clk}, rst{dut.rst}, count{dut.count, 4, dec_unsigned}, overflow{dut.overflow, 0, boolean}
        {
        }
    };

    TEST_CASE("counter")
    {
        dut_context_t dut_context{true, verilator_time_unit::ns, verilator_time_unit::ns};
        auto&& [_, dut, _, _]{dut_context};
        port_t port{dut};

        dut_context.add_task(generate_clock(port.clk, 2_ns));
        const auto do_verify{
            [&](this auto) -> task
            {
                constexpr static auto period{16zu};
                co_await generate_reset(port.rst, port.clk);
                for(auto&& ground_truth: views::iota(1zu) |
                                             views::transform(
                                                 [](std::uint64_t value)
                                                 {
                                                     constexpr static auto mask{(1zu << 4zu) - 1zu};
                                                     auto counter{value & mask};
                                                     return std::pair{counter, value >> 4zu != 0 && counter == 0};
                                                 }) |
                                             views::take(period * 3))
                {
                    co_await wait_verify(port.clk);
                    format_wrapper count{ground_truth.first, 4, dec_unsigned};
                    format_wrapper overflow{ground_truth.second, 1, boolean};
                    auto eval_time{co_await get_time_in_string()};
                    CAPTURE(eval_time);
                    CHECK_EQ(port.count, count);
                    CHECK_EQ(port.overflow, overflow);
                }

                co_await wait_verify(port.clk);
                co_await eval_finish();
            },
        };
        dut_context.add_task(do_verify());
        dut_context.loop_until_finish();
    }
}
