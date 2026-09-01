#include <verilator_fwd.hpp>
#include <doctest_macros.hpp>
import verilator_utils.full;
#include <unit_test_rtl_sync_fifo_verilator.h>
#include <verilator_bwd.hpp>

TEST_SUITE("sync_fifo")
{
    using namespace verilator_utils;
    using dut_t = unit_test_rtl_sync_fifo_verilator;
    using dut_context_t = dut_context<dut_t, VerilatedFstC>;

    struct port_t
    {
        bit_slice<CData> clk;
        bit_slice<CData> rst;
        bit_slice<CData> i_valid;
        bit_slice<CData> i_ready;
        vector_slice<CData> i_data;
        bit_slice<CData> o_valid;
        bit_slice<CData> o_ready;
        vector_slice<CData> o_data;

        constexpr static auto data_width{8zu};
        constexpr static auto addr_width{3zu};
        constexpr static auto depth{1zu << addr_width};

        explicit port_t(dut_t& dut) :
            clk{dut.clk}, rst{dut.rst}, i_valid{dut.i_valid, boolean}, i_ready{dut.i_ready, boolean},
            i_data{dut.i_data, data_width}, o_valid{dut.o_valid, boolean}, o_ready{dut.o_ready, boolean},
            o_data{dut.o_data, data_width}
        {
        }
    };

    struct reference_module
    {
        explicit reference_module(port_t& port) : port{port} {}

        [[nodiscard]] auto i_ready() const { return format_wrapper{!full(), port.i_ready.dump_format()}; }

        [[nodiscard]] auto o_valid() const { return format_wrapper{o_valid_r, port.o_valid.dump_format()}; }

        [[nodiscard]] auto o_data() const { return format_wrapper{o_data_r, port.o_data.dump_format()}; }

        [[nodiscard]] task<void> eval()
        {
            co_await wait_reset_finish(port.rst);
            while(true)
            {
                co_await wait_posedge(port.clk);

                // 组合逻辑
                auto write_enable{!full() && port.i_valid == 1};
                auto read_enable{!empty() && port.o_ready == 1};
                auto o_data_d{read_enable ? co_await fifo.peek() : o_data_r};
                auto o_valid_d{!empty()};

                // 时序逻辑
                if(write_enable) { co_await fifo.put(port.i_data); }
                if(read_enable) { co_await fifo.get(); }
                o_data_r = o_data_d;
                o_valid_r = o_valid_d;
            }
        }

    private:
        port_t& port;
        std::uint64_t o_data_r{};
        mailbox<CData> fifo{port_t::depth};
        bool o_valid_r{};

        [[nodiscard]] bool full() const noexcept { return fifo.num() == port_t::depth; }

        [[nodiscard]] bool empty() const noexcept { return fifo.num() == 0; }
    };

    TEST_CASE("sync_fifo")
    {
        dut_context_t ctx{
            {.coverage = true, .time_precision = verilator_time_unit::ns}
        };
        port_t port{ctx.get_dut()};
        reference_module ref{port};
        auto seed{ctx.get_seed()};
        std::mt19937_64 engin{seed};
        std::uniform_int_distribution dist{0zu, 1zu};

        ctx.add_task(generate_clock(port.clk, 2_ns));
        ctx.add_task(generate_reset(port.rst, port.clk));
        ctx.add_task(ref.eval());

        const auto do_verify{[&] -> task<void> {
            co_await wait_reset_finish(port.rst);
            CData cnt{};
            for(auto _: std::views::iota(0zu, port_t::depth * 500zu))
            {
                co_await wait_stimulate(port.clk);
                auto i_valid{dist(engin)};
                auto o_ready{dist(engin)};
                port.i_valid = i_valid;
                port.i_data = (cnt += i_valid);
                port.o_ready = o_ready;

                co_await verify_at(port.clk, [&] {
                    CHECK_EQ(port.i_ready, ref.i_ready());
                    CHECK_EQ(port.o_valid, ref.o_valid());
                    if(port.o_valid == 1 && o_ready == 1) { CHECK_EQ(port.o_data, ref.o_data()); }
                });
            }

            co_await wait_stimulate(port.clk);
            co_await eval_finish();
        }};
        ctx.add_task(do_verify());

        ctx.loop_until_finish(10_us);
    }
}
