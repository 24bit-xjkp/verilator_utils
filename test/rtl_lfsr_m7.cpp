#include <verilator_fwd.hpp>
#include "doctest_macros.hpp"
import verilator_utils.full;
#include <unit_test_rtl_lfsr_m7_verilator.h>
#include <verilator_bwd.hpp>

TEST_SUITE("lfsr_m7")
{
    using namespace verilator_utils;
    using namespace std::literals;
    namespace views = std::views;
    using dut_t = unit_test_rtl_lfsr_m7_verilator;
    using dut_context_t = dut_context<dut_t, VerilatedFstC>;

    enum class lfsr_feedback_t : std::uint64_t
    {
        fibonacci,
        galois
    };

    struct port_t
    {
        bit_slice<CData> clk;
        bit_slice<CData> rst;
        bit_slice<CData> enable;
        vector_slice<CData> initial_value;
        bit_slice<CData> lfsr_feedback;
        bit_slice<CData> result;
        constexpr static auto lfsr_width{7zu};

        inline explicit port_t(dut_t& dut) :
            clk{dut.clk}, rst{dut.rst}, enable{dut.enable}, initial_value{dut.initial_value, lfsr_width, dec_unsigned},
            lfsr_feedback{dut.lfsr_feedback, fsm_enum({"fibonacci"s, "galois"s})}, result{dut.result}
        {
        }
    };

    constexpr dut_context_option option{.coverage = true, .time_precision = verilator_time_unit::ns};

    TEST_CASE("lfsr_m7")
    {
        constexpr static auto initial_value_table{
            [] {
                std::array<std::uint64_t, 7> result{};
                for(auto&& [ref, shift]: views::zip(result, views::iota(0zu))) { ref = 1zu << shift; }
                return result;
            }(),
        };

        dut_context_t ctx{option};
        port_t port{ctx.get_dut()};

        format_wrapper lfsr_feedback{
            std::to_underlying(GENERATE(lfsr_feedback_t::fibonacci, lfsr_feedback_t::galois)),
            port.lfsr_feedback.width(),
            port.lfsr_feedback.format(),
        };
        CAPTURE(lfsr_feedback);
        port.lfsr_feedback = lfsr_feedback;
        ctx.set_base_name(std::format("lfsr_m7_{}"sv, lfsr_feedback));

        const auto do_verify{
            [&] -> task<void> {
                constexpr static auto period{(1zu << port.lfsr_width) - 1zu};
                auto&& ref{port.lfsr_feedback == std::to_underlying(lfsr_feedback_t::fibonacci) ? fibonacci_lfsr_generator
                                                                                                : galois_lfsr_generator};

                for(auto unwrapped_initial_value: initial_value_table)
                {
                    format_wrapper initial_value{unwrapped_initial_value, port.initial_value.dump_format()};
                    CAPTURE(initial_value);
                    co_await wait_stimulate(port.clk);
                    port.initial_value = initial_value;
                    port.enable = true;

                    co_await generate_reset(port.rst, port.clk);

                    // 验证模型的周期性
                    for(std::uint64_t unwrapped_result: ref(port.lfsr_width, 0, initial_value.value()) | views::take(period * 2))
                    {
                        co_await verify_at(port.clk, [&] {
                            format_wrapper result{unwrapped_result, port.result.dump_format()};
                            CHECK_EQ(result, port.result);
                        });
                    }

                    // 验证失能后模型输出不变
                    auto current_result{port.result.dump()};
                    co_await wait_stimulate(port.clk);
                    port.enable = false;
                    for(auto _: views::iota(0zu, 4zu))
                    {
                        co_await verify_at(port.clk, [&] { CHECK_EQ(current_result, port.result); });
                    }
                }

                co_await wait_stimulate(port.clk);
                co_await eval_finish();
            },
        };

        ctx.add_task(generate_clock(port.clk, 2_ns));
        ctx.add_task(do_verify());
        ctx.loop_until_finish();
    }
}
