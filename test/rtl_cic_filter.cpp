#include <verilator_fwd.hpp>
#include <doctest_macros.hpp>
import verilator_utils.full;
#include <npy.h>
#include <unit_test_rtl_cic_filter_verilator.h>
#include <verilator_bwd.hpp>

TEST_SUITE("cic_filter")
{
    using namespace verilator_utils;
    using dut_t = unit_test_rtl_cic_filter_verilator;
    using dut_context_t = dut_context<dut_t, VerilatedFstC>;

    struct port_t
    {
        bit_slice<CData> clk;
        bit_slice<CData> rst;
        bit_slice<CData> i_valid;
        vector_slice<SData> in;
        vector_slice<SData> out;
        bit_slice<CData> o_valid;

        using data_t = std::int16_t;
        constexpr static auto width{16zu};
        constexpr static packed_format data_format{width, dec_signed};
        constexpr static auto atol{1zu};

        explicit port_t(dut_t& dut) :
            clk{dut.clk}, rst{dut.rst}, i_valid{dut.i_valid, boolean}, in{dut.in, data_format}, out{dut.out, data_format},
            o_valid{dut.o_valid, boolean}
        {
        }
    };

    TEST_CASE("cic_filter")
    {
        dut_context_t ctx{
            {.coverage = true, .time_precision = verilator_time_unit::ns}
        };
        port_t port{ctx.get_dut()};
        npy::npzfilereader reader{ctx.get_binary_path().parent_path() / "cic_filter.npz"sv};
        using tensor_t = npy::tensor<port_t::data_t>;
        auto origin_signal{reader.read<tensor_t>("x.npy")};
        auto filtered_signal{reader.read<tensor_t>("y.npy")};

        ctx.add_task(generate_clock(port.clk, 2_ns));
        ctx.add_task(generate_reset(port.rst, port.clk));
        const auto do_stimulate{[&] -> task<void> {
            co_await wait_reset_finish(port.rst);
            for(std::uint16_t input: origin_signal)
            {
                co_await wait_stimulate(port.clk);
                port.i_valid = 1;
                port.in = input;
            }
        }};
        ctx.add_task(do_stimulate());
        const auto do_verify{[&] -> task<void> {
            co_await wait_reset_finish(port.rst);
            for(std::int64_t output: filtered_signal)
            {
                // o_valid是一个脉冲信号，将其作为检查的触发源
                co_await verify_at(port.o_valid, [&] {
                    format_wrapper ref_out{output, port_t::data_format};
                    CAPTURE(ref_out);
                    CAPTURE(port.out);
                    CHECK_LE(std::abs(output - port.out.to_underlying<std::int64_t>()), port_t::atol);
                });
            }
            co_await eval_finish();
        }};
        ctx.add_task(do_verify());

        ctx.loop_until_finish(60_us);
    }
}
