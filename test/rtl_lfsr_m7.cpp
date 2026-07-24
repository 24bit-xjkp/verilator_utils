module;
#include <doctest_fwd.hpp>
module unit_test_rtl.lfsr_m7;
import verilator_utils;

extern "C++"
{
#include <doctest.h>
}

TEST_SUITE("lfsr_m7")
{
    using namespace verilator_utils;
    using namespace verilator_utils::literals;
    using namespace verilator_utils::data_format;
    using namespace std::string_literals;
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

        inline port_t(dut_t& dut) :
            clk{dut.clk}, rst{dut.rst}, enable{dut.enable}, initial_value{dut.initial_value, 7, dec_unsigned},
            lfsr_feedback{dut.lfsr_feedback, 0, fsm_enum({"fibonacci"s, "galois"s})}, result{dut.result}
        {
        }
    };

    TEST_CASE("lfsr_m7")
    {
        constexpr static auto initial_value_table{
            []
            {
                std::array<std::uint64_t, 7> result{};
                for(auto&& [ref, shift]: views::zip(result, views::iota(0zu))) { ref = 1zu << shift; }
                return result;
            }(),
        };

        dut_context_t dut_context{true, verilator_time_unit::ns, verilator_time_unit::ns};
        auto&& [_, dut, _, _]{dut_context};
        port_t port{dut};
        dut.eval();

        format_wrapper lfsr_feedback{
            std::to_underlying(GENERATE(lfsr_feedback_t::fibonacci, lfsr_feedback_t::galois)),
            port.lfsr_feedback.width(),
            port.lfsr_feedback.format(),
        };
        CAPTURE(lfsr_feedback);
        port.lfsr_feedback = lfsr_feedback;
        dut_context.set_base_name(std::format("lfsr_m7_{}", lfsr_feedback));

        const auto do_verify{
            [&](this auto) -> task
            {
                auto width{port.initial_value.width()};
                auto period{(1zu << port.initial_value.width()) - 1zu};
                auto&& scoreboard{port.lfsr_feedback == std::to_underlying(lfsr_feedback_t::fibonacci) ? fibonacci_lfsr_generator
                                                                                                       : galois_lfsr_generator};

                for(auto unwrapped_initial_value: initial_value_table)
                {
                    format_wrapper initial_value{
                        unwrapped_initial_value,
                        port.initial_value.width(),
                        port.initial_value.format(),
                    };
                    CAPTURE(initial_value);
                    co_await wait_stimulus(port.clk);
                    port.initial_value = initial_value;
                    port.enable = true;

                    co_await generate_reset(port.rst, port.clk);

                    // 验证模型的周期性
                    for(std::uint64_t ground_truth_value: scoreboard(width, 0, initial_value.value()) | views::take(period * 2))
                    {
                        format_wrapper ground_truth{ground_truth_value, port.result.width(), port.result.format()};
                        co_await wait_verify(port.clk);
                        auto eval_time{co_await get_time_in_string()};
                        CAPTURE(eval_time);
                        CHECK_EQ(ground_truth, port.result);
                    }

                    // 验证失能后模型输出不变
                    format_wrapper current_result{static_cast<std::uint64_t>(port.result),
                                                  port.result.width(),
                                                  port.result.format()};
                    co_await wait_stimulus(port.clk);
                    port.enable = false;
                    for(auto i{0zu}; i != 3; ++i)
                    {
                        co_await wait_verify(port.clk);
                        auto eval_time{co_await get_time_in_string()};
                        CAPTURE(eval_time);
                        CHECK_EQ(current_result, port.result);
                    }
                }

                co_await wait_stimulus(port.clk);
                co_await eval_finish();
            },
        };

        dut_context.add_task(generate_clock(port.clk, 2_ns));
        dut_context.add_task(do_verify());
        dut_context.loop_until_finish();
    }
}
