#include <verilator_fwd.hpp>
#include <doctest_macros.hpp>
import verilator_utils.full;
#include <unit_test_rtl_async_fifo_verilator.h>
#include <verilator_bwd.hpp>

TEST_SUITE("async_fifo")
{
    using namespace verilator_utils;
    using dut_t = unit_test_rtl_async_fifo_verilator;
    using dut_context_t = dut_context<dut_t, VerilatedFstC>;

    struct port_t
    {
        bit_slice<CData> rst;
        bit_slice<CData> i_clk;
        bit_slice<CData> i_valid;
        bit_slice<CData> i_ready;
        vector_slice<CData> i_data;
        bit_slice<CData> o_clk;
        bit_slice<CData> o_valid;
        bit_slice<CData> o_ready;
        vector_slice<CData> o_data;
        constexpr static auto width{8zu};
        constexpr static auto depth{8zu};

        explicit port_t(dut_t& dut) :
            rst{dut.rst}, i_clk{dut.i_clk}, i_valid{dut.i_valid, boolean}, i_ready{dut.i_ready, boolean},
            i_data{dut.i_data, width}, o_clk{dut.o_clk}, o_valid{dut.o_valid, boolean}, o_ready{dut.o_ready, boolean},
            o_data{dut.o_data, width}
        {
        }
    };

    struct reference_module
    {
        using wrapper_t = format_wrapper<std::uint64_t>;

        explicit reference_module(port_t& port) noexcept : port{port} {}

    private:
        constexpr static auto address_mask{port_t::depth - 1zu};
        constexpr static auto index_mask{(port_t::depth << 1zu) - 1zu};
        constexpr static auto index_width{std::bit_width(index_mask)};
        constexpr static wrapper_t default_index{0, index_width};
        std::array<std::uint8_t, port_t::depth> ram{};
        ::std::uint64_t synced_i_index_r{};
        ::std::uint64_t i_index_r{};
        ::std::uint64_t synced_o_index_r{};
        ::std::uint64_t o_index_r{};
        port_t& port;
        std::optional<wrapper_t> o_data_r{};
        bool o_valid_r{};

    public:
        [[nodiscard]] bool i_ready() const noexcept
        {
            constexpr static auto mask{port_t::depth};
            return (synced_o_index_r ^ mask) != i_index_r;
        }

        [[nodiscard]] bool o_valid() const noexcept { return o_valid_r; }

        [[nodiscard]] const wrapper_t& o_data() const
        {
            return o_data_r.value();  // NOLINT(bugprone-unchecked-optional-access)
        }

        [[nodiscard]] task<void> eval()
        {
            select_clock clk{};
            clk.add_clock(port.i_clk);
            clk.add_clock(port.o_clk);
            shift_register<std::uint64_t> i_delay_line{1, index_width, hex};
            shift_register<std::uint64_t> o_delay_line{1, index_width, hex};

            while(true)
            {
                auto triggered{co_await clk};
                auto posedge_i_clk{triggered[0]};
                auto posedge_o_clk{triggered[1]};

                // 组合逻辑
                auto i_enable{i_ready() && port.i_valid == 1};
                auto i_index_d{i_enable ? i_index_r + 1 & index_mask : i_index_r};
                auto&& i_ram_ref{ram[i_index_r & address_mask]};
                auto o_valid_d{synced_i_index_r != o_index_r};
                auto o_enable{o_valid_d && port.o_ready == 1};
                auto o_index_d{o_enable ? o_index_r + 1 & index_mask : o_index_r};
                wrapper_t o_data_d{ram[o_index_r & address_mask], port_t::width};

                // 时序逻辑
                synced_o_index_r = o_delay_line.update(o_index_r, posedge_i_clk).value_or(default_index).value();
                synced_i_index_r = i_delay_line.update(i_index_r, posedge_o_clk).value_or(default_index).value();
                if(posedge_i_clk)
                {
                    i_index_r = i_index_d;
                    if(i_enable) { i_ram_ref = port.i_data; }
                }
                if(posedge_o_clk)
                {
                    o_valid_r = o_valid_d;
                    o_index_r = o_index_d;
                    if(o_enable) { o_data_r = o_data_d; }
                }
            }
        }
    };

    constexpr auto iters{port_t::depth * 2 * 3};

    task<void> do_write(port_t & port, reference_module & ref)
    {
        port.i_valid = 0;
        co_await wait_reset_finish(port.rst);
        const auto do_verify{[&] { CHECK_EQ(port.i_ready, ref.i_ready()); }};
        for(auto i{0zu}; i != iters;)
        {
            co_await wait_stimulate(port.i_clk);
            if(ref.i_ready())
            {
                port.i_data = i++;
                port.i_valid = 1;
            }
            co_await verify_at(port.i_clk, do_verify);

            if(i == iters / 3)
            {
                co_await wait_stimulate(port.i_clk);
                port.i_valid = 0;
                while(port.o_valid == 1) { co_await verify_at(port.i_clk, do_verify); }
            }
        }

        co_await wait_stimulate(port.i_clk);
        port.i_valid = 0;
    }

    task<void> do_read(port_t & port, reference_module & ref)
    {
        port.o_ready = 0;
        co_await wait_reset_finish(port.rst);

        for(auto i{0zu}; i != iters;)
        {
            co_await wait_stimulate(port.o_clk);
            port.o_ready = 1;
            co_await verify_at(port.o_clk, [&] {
                CHECK_EQ(port.o_valid, ref.o_valid());
                if(ref.o_valid())
                {
                    CHECK_EQ(port.o_data, ref.o_data());
                    ++i;
                }
            });

            if(i == iters / 3 * 2)
            {
                co_await wait_stimulate(port.o_clk);
                port.o_ready = 0;
                while(port.i_ready == 1)
                {
                    co_await verify_at(port.i_clk, [&] {
                        CHECK_EQ(port.o_valid, ref.o_valid());
                        if(ref.o_valid()) { CHECK_EQ(port.o_data, ref.o_data()); }
                    });
                }
            }
        }

        co_await wait_stimulate(port.o_clk);
        port.o_ready = 0;
    }

    task<void> do_verify(port_t & port)
    {
        reference_module ref{port};
        co_await add_task(ref.eval());
        auto pool{co_await get_spawn_pool()};
        pool.add_task(do_write(port, ref));
        pool.add_task(do_read(port, ref));
        co_await pool.join_all();

        co_await wait_verify(port.i_clk);
        co_await wait_verify(port.o_clk);
        co_await eval_finish();
    }

    constexpr auto slow_period{26_ns};
    constexpr auto fast_period{14_ns};
    constexpr auto rst_period{2_ns * 7zu * 13zu};

    TEST_CASE("write_slow_read_fast")
    {
        dut_context_t ctx{true, verilator_time_unit::ns, verilator_time_unit::ns};
        port_t port{ctx.get_dut()};

        ctx.add_task(generate_async_reset(port.rst, rst_period));
        ctx.add_task(generate_clock(port.i_clk, slow_period));
        ctx.add_task(generate_clock(port.o_clk, fast_period));
        ctx.add_task(do_verify(port));
        ctx.loop_until_finish(5_us);
    }

    TEST_CASE("write_fast_read_slow")
    {
        dut_context_t ctx{true, verilator_time_unit::ns, verilator_time_unit::ns};
        port_t port{ctx.get_dut()};

        ctx.add_task(generate_async_reset(port.rst, rst_period));
        ctx.add_task(generate_clock(port.i_clk, fast_period));
        ctx.add_task(generate_clock(port.o_clk, slow_period));
        ctx.add_task(do_verify(port));
        ctx.loop_until_finish(5_us);
    }
}
