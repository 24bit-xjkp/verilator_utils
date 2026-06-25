#include <unit_test_rtl_edge_detector_verilator.h>
#include <doctest.h>
import verilator_utils;
import std;

TEST_SUITE("edge_detector")
{
    using namespace ::verilator_utils;
    using namespace ::verilator_utils::literals;
    using dut_t = ::unit_test_rtl_edge_detector_verilator;
    using dut_context_t = dut_context<dut_t, ::VerilatedFstC>;
    using enum eval_scheduler::eval_stage_enum;

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
        // 流水线级数为3，取最后2个抽头进行边沿检测，延迟为1
        constexpr static auto delay{pipeline - 2};
        scheduler.add_task(generate_clock(scheduler, port.clk, period));
        scheduler.add_task(generate_reset(scheduler, port.rst, port.clk));

        constexpr static auto verify{[](eval_scheduler& scheduler, port_t& port, bool rising, bool falling) static -> task
                                     {
                                         auto handle{co_await task::get_handle_t{}};
                                         // 等一个上升沿才能被采样到
                                         co_await wait_for_verify(scheduler, port.clk, delay + 1);

                                         INFO(::std::format("At {}", scheduler.time_in_string()));
                                         CHECK_EQ(port.rising, rising);
                                         CHECK_EQ(port.falling, falling);
                                         CHECK_EQ(port.both, rising || falling);
                                     }};
        constexpr static auto stimulate{
            [](eval_scheduler& scheduler, port_t& port) static -> task
            {
                port.signal = 0;
                // 等待复位完成
                co_await wait_until_reset_finish(scheduler, port.rst);
                ::std::vector<async_task> verify_tasks{};
                auto do_verify{[&scheduler, &port, &verify_tasks](bool rising, bool falling)
                               { verify_tasks.emplace_back(scheduler, verify(scheduler, port, rising, falling)); }};

                // 产生异步输入信号
                co_await scheduler.wait_time(period / 4zu);
                port.signal = 1;
                do_verify(true, false);
                co_await scheduler.wait_time(period / 2zu);
                port.signal = 0;
                do_verify(false, true);

                // 等待激励被采样
                co_await scheduler.wait_posedge(port.clk);

                // 下降沿产生同步输入信号
                co_await scheduler.wait_negedge(port.clk);
                port.signal = 1;
                do_verify(true, false);
                co_await scheduler.wait_negedge(port.clk);
                port.signal = 0;
                do_verify(false, true);

                // 上升沿产生同步输入信号
                co_await scheduler.wait_posedge(port.clk, 1);
                co_await wait_for_eval_stage(scheduler, after_dut_eval);
                port.signal = 1;
                do_verify(true, false);
                co_await scheduler.wait_posedge(port.clk);
                co_await wait_for_eval_stage(scheduler, after_dut_eval);
                port.signal = 0;
                do_verify(false, true);

                // 无输入信号
                co_await scheduler.wait_posedge(port.clk, 1);
                co_await wait_for_eval_stage(scheduler, after_dut_eval);
                do_verify(false, false);


                co_await task_join(scheduler, verify_tasks);
                // 避免波形被截断
                co_await wait_for_stimulus(scheduler, port.clk, 1);
                scheduler.finish();
            }};
        scheduler.add_task(stimulate(scheduler, port));

        dut_context.loop_until_empty();
    }
}
