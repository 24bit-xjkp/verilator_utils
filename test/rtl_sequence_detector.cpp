#include <verilator_fwd.hpp>
#include <doctest_fwd.hpp>
import std;
import std.compat;
import verilator_utils;

extern "C++"
{
#include <unit_test_rtl_sequence_detector_verilator.h>
#include <doctest.h>
#include <verilator_bwd.hpp>
}

TEST_SUITE("sequence_detector")
{
    using namespace verilator_utils;
    using namespace verilator_utils::literals;
    using namespace verilator_utils::data_format;
    namespace views = std::views;
    using dut_t = unit_test_rtl_sequence_detector_verilator;
    using dut_context_t = dut_context<dut_t, VerilatedFstC>;

    struct port_t
    {
        bit_slice<CData> clk;
        bit_slice<CData> enable;
        bit_slice<CData> bit_stream;
        bit_slice<CData> result;

        inline explicit port_t(dut_t& dut) :
            clk{dut.clk}, enable{dut.enable}, bit_stream{dut.bit_stream}, result{dut.result, 0, boolean}
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

    TEST_CASE("sequence_detector")
    {
        dut_context_t dut_context{true, verilator_time_unit::ns, verilator_time_unit::ns};
        auto&& [_, dut, _, _]{dut_context};
        port_t port{dut};

        dut_context.add_task(generate_clock(port.clk, 2_ns));
        const auto do_verify{
            [&](this auto) -> task<void>
            {
                constexpr static auto period{(1zu << 8zu) - 1zu};
                port.enable = 0;
                reference_module ref{};
                for(auto input_bit: galois_lfsr_generator(8) | views::take(period * 2))
                {
                    co_await wait_stimulate(port.clk);
                    port.enable = 1;
                    port.bit_stream = input_bit;
                    co_await wait_verify(port.clk);
                    auto ground_truth{ref(input_bit)};
                    auto eval_time{co_await get_time_in_string()};
                    CAPTURE(eval_time);
                    CHECK_EQ(port.result, ground_truth);
                }

                for(auto i{0zu}; i != 8zu; ++i)
                {
                    co_await wait_stimulate(port.clk);
                    port.enable = 0;
                    port.bit_stream = ref.sequence_to_detect >> (7zu - i) & 1zu;
                    co_await wait_verify(port.clk);
                    auto eval_time{co_await get_time_in_string()};
                    CAPTURE(eval_time);
                    CHECK_EQ(port.result, false);
                }

                co_await wait_stimulate(port.clk);
                co_await eval_finish();
            },
        };
        dut_context.add_task(do_verify());
        dut_context.loop_until_finish();
    }
}
