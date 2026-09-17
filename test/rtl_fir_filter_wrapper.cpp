#include <verilator_fwd.hpp>
#include <doctest_macros.hpp>
import verilator_utils.full;
#include <npy.h>
#include <unit_test_rtl_fir_filter_wrapper_verilator.h>
#include <verilator_bwd.hpp>

TEST_SUITE("fir_filter")
{
    using namespace verilator_utils;
    using dut_t = unit_test_rtl_fir_filter_wrapper_verilator;
    using dut_context_t = dut_context<dut_t, VerilatedFstC>;

    struct port_t
    {
        constexpr static auto filter_num{4zu};
        bit_slice<CData> clk;
        bit_slice<CData> rst;
        bit_slice<CData> i_valid;
        vector_slice<SData> in;
        unpacked_array<SData, filter_num> out;
        unpacked_array<CData, filter_num> o_valid;

        using data_t = std::int16_t;
        constexpr static auto width{16zu};
        constexpr static packed_format data_format{width, dec_signed};
        /// 误差容限
        constexpr static approx tol{1z, 5e-3};
        /// 实际并行度
        constexpr static std::array parallel{4zu * 2, 5zu * 2, 6zu, 8zu};
        /// 滤波器点数
        constexpr static auto taps{31zu};
        constexpr static auto min_parallel{std::ranges::min(parallel)};
        /// 计算周期数
        constexpr static auto cycles{(taps + min_parallel - 1) / min_parallel};

        explicit port_t(dut_t& dut) :
            clk{dut.clk}, rst{dut.rst}, i_valid{dut.i_valid, boolean}, in{dut.in, data_format}, out{dut.out, data_format},
            o_valid{dut.o_valid, 1, boolean}
        {
        }
    };

    TEST_CASE("fir_filter")
    {
        dut_context_t ctx{
            {.coverage = true, .time_precision = verilator_time_unit::ns}
        };
        port_t port{ctx.get_dut()};
        npy::npzfilereader reader{ctx.get_binary_path().parent_path() / "fir_filter.npz"sv};
        using tensor_t = npy::tensor<port_t::data_t>;
        auto origin_signal{reader.read<tensor_t>("x.npy")};
        auto filtered_signal{reader.read<tensor_t>("y.npy")};
        auto shifted_filtered_signal{reader.read<tensor_t>("shifted_y.npy")};

        ctx.add_task(generate_clock(port.clk, 2_ns));
        ctx.add_task(generate_reset(port.rst, port.clk));
        const auto do_stimulate{[&] -> task<void> {
            co_await wait_reset_finish(port.rst);
            for(std::int16_t input: origin_signal)
            {
                co_await wait_stimulate(port.clk);
                port.i_valid = 1;
                port.in = width_cast(input, port_t::width);
                for(auto _: std::views::iota(0zu, port_t::cycles - 1))
                {
                    co_await wait_stimulate(port.clk);
                    port.i_valid = 0;
                }
            }
        }};
        ctx.add_task(do_stimulate());

        const auto verify_a_port{[&](std::size_t filter_index) -> task<void> {
            co_await wait_reset_finish(port.rst);
            auto&& ref{filter_index >= 2 ? shifted_filtered_signal : filtered_signal};
            for(std::int64_t output: ref)
            {
                // o_valid是一个脉冲信号，将其作为检查的触发源
                co_await verify_at(port.o_valid[filter_index][0], [&] {
                    CAPTURE(filter_index);
                    CHECK_EQ(port_t::tol(output), port.out[filter_index].to_underlying<std::int64_t>());
                });
            }
        }};
        const auto do_verify{[&] -> task<void> {
            auto pool{co_await get_spawn_pool()};
            for(auto i: std::views::iota(0zu, port_t::filter_num)) { pool.add_task(verify_a_port(i)); }
            co_await pool.join_all();

            co_await wait_stimulate(port.clk);
            co_await eval_finish();
        }};
        ctx.add_task(do_verify());

        ctx.loop_until_finish(200_us);
    }
}
