#include <verilator_fwd.hpp>
#include "doctest_macros.hpp"
import verilator_utils.full;
#include <unit_test_rtl_sequence_detector_verilator.h>
#include <verilator_bwd.hpp>

TEST_SUITE("sequence_detector")
{
    using namespace verilator_utils;
    namespace views = std::views;
    using dut_t = unit_test_rtl_sequence_detector_verilator;
    using dut_context_t = dut_context<dut_t, VERILATOR_TRACER>;

    struct port_t
    {
        bit_slice<CData> clk;
        bit_slice<CData> enable;
        bit_slice<CData> bit_stream;
        bit_slice<CData> result;

        inline explicit port_t(dut_t& dut) :
            clk{dut.clk}, enable{dut.enable}, bit_stream{dut.bit_stream}, result{dut.result, boolean}
        {
        }
    };

    struct reference_module
    {
        constexpr inline static auto sequence_to_detect{0b11001010zu};
        std::uint8_t status{};

        inline bool operator() (bool input)
        {
            status = static_cast<std::size_t>(status) << 1zu | static_cast<std::size_t>(input);
            return status == sequence_to_detect;
        }
    };

    constexpr dut_context_option option{.coverage = true, .time_precision = verilator_time_unit::ns};

    TEST_CASE("sequence_detector")
    {
        dut_context_t ctx{option};
        port_t port{ctx.get_dut()};

        ctx.add_task(generate_clock(port.clk, 2_ns));
        const auto do_verify{
            [&] -> task<void> {
                constexpr static auto period{(1zu << 8zu) - 1zu};
                port.enable = 0;
                reference_module ref{};
                for(auto input_bit: galois_lfsr_generator(8) | views::take(period * 2))
                {
                    co_await wait_stimulate(port.clk);
                    port.enable = 1;
                    port.bit_stream = input_bit;
                    co_await verify_at(port.clk, [&] {
                        auto ground_truth{ref(input_bit)};
                        CHECK_EQ(port.result, ground_truth);
                    });
                }

                for(auto i{0zu}; i != 8zu; ++i)
                {
                    co_await wait_stimulate(port.clk);
                    port.enable = 0;
                    port.bit_stream = ref.sequence_to_detect >> (7zu - i) & 1zu;
                    co_await verify_at(port.clk, [&] { CHECK_EQ(port.result, false); });
                }

                co_await wait_stimulate(port.clk);
                co_await eval_finish();
            },
        };
        ctx.add_task(do_verify());
        ctx.loop_until_finish();
    }
}
