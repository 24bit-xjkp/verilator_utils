#include <verilator_fwd.hpp>
#include "doctest_macros.hpp"
import verilator_utils.full;
#include <unit_test_rtl_edge_detector_verilator.h>
#include <verilator_bwd.hpp>

TEST_SUITE("edge_detector")
{
    using namespace ::verilator_utils;
    using dut_t = ::unit_test_rtl_edge_detector_verilator;
    using dut_context_t = dut_context<dut_t, ::VerilatedFstC>;

    struct port_t
    {
        bit_slice<::CData> clk;
        bit_slice<::CData> rst;
        bit_slice<::CData> signal;
        bit_slice<::CData> rising;
        bit_slice<::CData> falling;
        bit_slice<::CData> both;

        inline explicit port_t(dut_t& dut) :
            clk{dut.clk}, rst{dut.rst}, signal{dut.signal}, rising{dut.rising, boolean}, falling{dut.falling, boolean},
            both{dut.both, boolean}
        {
        }
    };

    TEST_CASE("edge_detector")
    {
        dut_context_t ctx{true, verilator_time_unit::ns, verilator_time_unit::ps_10};
        port_t port{ctx.get_dut()};

        constexpr static auto period{1_ns};
        constexpr static auto pipeline{3zu};
        // 流水线级数为3，倒数第二级表示当前信号
        constexpr static auto delay{pipeline - 1};
        ctx.add_task(generate_clock(port.clk, period));

        const auto verify{
            [&](bool rising, bool falling) -> task<void> {
                return verify_at(
                    port.clk,
                    [=, &port] {
                        CHECK_EQ(port.rising, rising);
                        CHECK_EQ(port.falling, falling);
                        CHECK_EQ(port.both, rising || falling);
                    },
                    delay);
            },
        };
        const auto stimulate{
            [&] -> task<void> {
                port.signal = 0;
                co_await generate_reset(port.rst, port.clk);
                auto verify_tasks{co_await get_spawn_pool()};
                const auto do_verify{[&](bool rising, bool falling) { verify_tasks.add_task(verify(rising, falling)); }};

                // 产生异步输入信号
                co_await wait_time(period / 4zu);
                port.signal = 1;
                do_verify(true, false);
                co_await wait_time(period / 2zu);
                port.signal = 0;
                do_verify(false, true);

                // 等待激励被采样
                co_await wait_posedge(port.clk);

                // 下降沿产生同步输入信号
                co_await wait_stimulate(port.clk);
                port.signal = 1;
                do_verify(true, false);
                co_await wait_stimulate(port.clk);
                port.signal = 0;
                do_verify(false, true);

                // 上升沿产生同步输入信号
                co_await wait_stimulate(port.clk, 1, edge_enum::rising);
                port.signal = 1;
                do_verify(true, false);
                co_await wait_stimulate(port.clk, 1, edge_enum::rising);
                port.signal = 0;
                do_verify(false, true);

                // 无输入信号
                co_await wait_stimulate(port.clk, 1, edge_enum::rising);
                do_verify(false, false);

                co_await verify_tasks.join_all();
                // 避免波形被截断
                co_await wait_stimulate(port.clk, 2);
                co_await eval_finish();
            },
        };
        ctx.add_task(stimulate());

        ctx.loop_until_finish();
    }
}
