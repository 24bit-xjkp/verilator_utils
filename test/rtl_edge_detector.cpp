module;
#include <doctest_fwd.hpp>
module unit_test_rtl.edge_detector;
import verilator_utils;

extern "C++"
{
#include <doctest.h>
}

TEST_SUITE("edge_detector")
{
    using namespace ::verilator_utils;
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

        inline port_t(dut_t& dut) :
            clk{dut.clk, 0}, rst{dut.rst, 0}, signal{dut.signal, 0}, rising{dut.rising, 0}, falling{dut.falling, 0},
            both{dut.both, 0}
        {
        }
    };

    TEST_CASE("edge_detector")
    {
        dut_context_t dut_context{verilator_time_unit::ns, verilator_time_unit::ps_10};
        auto&& [context, dut, scheduler, _]{dut_context};
        port_t port{dut};

        constexpr static auto period{1_ns};
        constexpr static auto pipeline{3zu};
        // 流水线级数为3，倒数第二级表示当前信号
        constexpr static auto delay{pipeline - 1};
        scheduler.add_task(generate_clock(port.clk, period));
        scheduler.add_task(generate_reset(port.rst, port.clk));

        constexpr static auto verify{
            [](port_t& port, bool rising, bool falling) static -> task
            {
                co_await wait_verify(port.clk, delay);

                auto&& scheduler{co_await get_scheduler()};
                INFO(::std::format("At {}", scheduler.time_in_string()));
                CHECK_EQ(port.rising, rising);
                CHECK_EQ(port.falling, falling);
                CHECK_EQ(port.both, rising || falling);
            },
        };
        constexpr static auto stimulate{
            [](port_t& port) static -> task
            {
                port.signal = 0;
                // 等待复位完成
                co_await wait_reset_finish(port.rst);
                auto verify_tasks{co_await get_spawn_pool()};
                const auto do_verify{[&port, &verify_tasks](bool rising, bool falling)
                                     { verify_tasks.add_task(verify(port, rising, falling)); }};

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
                co_await wait_stimulus(port.clk);
                port.signal = 1;
                do_verify(true, false);
                co_await wait_stimulus(port.clk);
                port.signal = 0;
                do_verify(false, true);

                // 上升沿产生同步输入信号
                co_await wait_stimulus(port.clk, 1, true);
                port.signal = 1;
                do_verify(true, false);
                co_await wait_stimulus(port.clk, 1, true);
                port.signal = 0;
                do_verify(false, true);

                // 无输入信号
                co_await wait_stimulus(port.clk, 1, true);
                do_verify(false, false);

                co_await verify_tasks.join_all();
                // 避免波形被截断
                co_await wait_stimulus(port.clk, 2);
                co_await eval_finish();
            },
        };
        scheduler.add_task(stimulate(port));

        dut_context.loop_until_finish();
    }
}
