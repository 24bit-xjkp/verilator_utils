#include <verilator_fwd.hpp>
#include <doctest_fwd.hpp>
import std;
import std.compat;
import verilator_utils;

extern "C++"
{
#include <unit_test_rtl_edge_detector_verilator.h>
#include <doctest.h>
#include <verilator_bwd.hpp>
}

TEST_SUITE("edge_detector")
{
    using namespace ::verilator_utils;
    using namespace ::verilator_utils::data_format;
    using namespace ::verilator_utils::literals;
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
            clk{dut.clk}, rst{dut.rst}, signal{dut.signal}, rising{dut.rising, 0, boolean}, falling{dut.falling, 0, boolean},
            both{dut.both, 0, boolean}
        {
        }
    };

    TEST_CASE("edge_detector")
    {
        dut_context_t dut_context{true, verilator_time_unit::ns, verilator_time_unit::ps_10};
        auto&& [_, dut, _, _]{dut_context};
        port_t port{dut};

        constexpr static auto period{1_ns};
        constexpr static auto pipeline{3zu};
        // 流水线级数为3，倒数第二级表示当前信号
        constexpr static auto delay{pipeline - 1};
        dut_context.add_task(generate_clock(port.clk, period));
        dut_context.add_task(generate_reset(port.rst, port.clk));

        const auto verify{
            [&](this auto, bool rising, bool falling) -> task<void>
            {
                co_await wait_verify(port.clk, delay);

                auto eval_time{co_await get_time_in_string()};
                CAPTURE(eval_time);
                CHECK_EQ(port.rising, rising);
                CHECK_EQ(port.falling, falling);
                CHECK_EQ(port.both, rising || falling);
            },
        };
        const auto stimulate{
            [&](this auto) -> task<void>
            {
                port.signal = 0;
                // 等待复位完成
                co_await wait_reset_finish(port.rst);
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
                co_await wait_stimulate(port.clk, 1, true);
                port.signal = 1;
                do_verify(true, false);
                co_await wait_stimulate(port.clk, 1, true);
                port.signal = 0;
                do_verify(false, true);

                // 无输入信号
                co_await wait_stimulate(port.clk, 1, true);
                do_verify(false, false);

                co_await verify_tasks.join_all();
                // 避免波形被截断
                co_await wait_stimulate(port.clk, 2);
                co_await eval_finish();
            },
        };
        dut_context.add_task(stimulate());

        dut_context.loop_until_finish();
    }
}
