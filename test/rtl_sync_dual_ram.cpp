#include <verilator_fwd.hpp>
#include <doctest_macros.hpp>
import verilator_utils.full;
#include <unit_test_rtl_sync_dual_ram_verilator.h>
#include <verilator_bwd.hpp>

TEST_SUITE("sync_dual_ram")
{
    using namespace verilator_utils;
    using dut_t = unit_test_rtl_sync_dual_ram_verilator;
    using dut_context_t = dut_context<dut_t, VerilatedFstC>;
    namespace views = std::views;
    namespace ranges = std::ranges;

    struct port_t
    {
        constexpr static auto data_width{8zu};
        constexpr static auto addr_width{3zu};
        constexpr static auto width{data_width};
        constexpr static auto depth{1zu << addr_width};

        bit_slice<CData> clk;
        bit_slice<CData> read_enable;
        vector_slice<CData> read_addr;
        vector_slice<CData> read_data_read_first;
        vector_slice<CData> read_data_write_first;
        vector_slice<CData> read_data_no_change;
        bit_slice<CData> write_enable;
        vector_slice<CData> write_addr;
        vector_slice<CData> write_data;

        explicit port_t(dut_t& dut) :
            clk{dut.clk}, read_enable{dut.read_enable, boolean}, read_addr{dut.read_addr, addr_width},
            read_data_read_first{dut.read_data_read_first, width}, read_data_write_first{dut.read_data_write_first, width},
            read_data_no_change{dut.read_data_no_change, width}, write_enable{dut.write_enable, boolean},
            write_addr{dut.write_addr, addr_width}, write_data{dut.write_data, width}
        {
        }
    };

    struct reference_module
    {
        port_t& port;
        using format_t = format_wrapper<std::uint64_t>;
        std::array<CData, port_t::depth> ram{};
        format_t read_data_read_first{0, port_t::width};
        format_t read_data_write_first{0, port_t::width};
        format_t read_data_no_change{0, port_t::width};

        [[nodiscard]] task<void> eval()
        {
            while(true)
            {
                co_await wait_posedge(port.clk);
                if(port.read_enable == 1)
                {
                    read_data_read_first = ram[port.read_addr];
                    bool read_write_simultaneously{port.write_enable == 1 && port.read_addr == port.write_addr};
                    read_data_write_first = read_write_simultaneously ? port.write_data : ram[port.read_addr];
                    if(!read_write_simultaneously) { read_data_no_change = ram[port.read_addr]; }
                }
                if(port.write_enable == 1) { ram[port.write_addr] = port.write_data; }
            }
        }
    };

    TEST_CASE("sync_dual_ram")
    {
        dut_context_t ctx{true, verilator_time_unit::ns, verilator_time_unit::ns};
        port_t port{ctx.get_dut()};
        reference_module ref{port};

        constexpr static auto epochs{8zu};
        constexpr static auto iters{port_t::depth * epochs};
        std::mt19937_64 rng{ctx.get_seed()};

        ctx.add_task(generate_clock(port.clk, 2_ns));
        ctx.add_task(ref.eval());

        const auto read_and_verify{
            [&] {
                CHECK_EQ(port.read_data_read_first, ref.read_data_read_first);
                CHECK_EQ(port.read_data_write_first, ref.read_data_write_first);
                CHECK_EQ(port.read_data_no_change, ref.read_data_no_change);
            },
        };
        const auto do_sync_write{
            [&] -> task<void> {
                for(auto i: views::iota(0zu, port_t::depth))
                {
                    co_await wait_stimulate(port.clk);
                    port.write_enable = 1;
                    port.write_addr = i;
                    port.write_data = i;
                }
                co_await wait_stimulate(port.clk);
                port.write_enable = 0;
            },
        };
        const auto do_sync_read{
            [&] -> task<void> {
                port.read_enable = 0;
                co_await wait_event([&] { return port.write_enable == 1; });
                // 读落后写1拍以避免读到未初始化的值
                co_await wait_verify(port.clk);
                for(auto i: views::iota(0zu, port_t::depth))
                {
                    co_await wait_stimulate(port.clk);
                    port.read_enable = 1;
                    port.read_addr = i;

                    co_await verify_at(port.clk, read_and_verify);
                }
                co_await wait_stimulate(port.clk);
                port.read_enable = 0;
            },
        };

        constexpr static auto gen{views::iota(0zu, iters) |
                                  views::transform([](std::size_t i) { return std::pair{i % (port_t::depth - 1zu), i}; })};
        constexpr static auto suffered_epochs{epochs - 2};
        const auto do_random_write{
            [&] -> task<void> {
                auto operation_list{gen | ranges::to<std::vector>()};
                ranges::shuffle(operation_list | views::take(port_t::depth * suffered_epochs), rng);
                for(auto [addr, data]: operation_list)
                {
                    co_await wait_stimulate(port.clk);
                    port.write_enable = 1;
                    port.write_addr = addr;
                    port.write_data = data;
                }
                co_await wait_stimulate(port.clk);
                port.write_enable = 0;
            },
        };
        const auto do_random_read{
            [&] -> task<void> {
                auto addr_list{gen | views::keys | ranges::to<std::vector>()};
                ranges::shuffle(addr_list | views::take(port_t::depth * suffered_epochs), rng);
                for(auto addr: addr_list)
                {
                    co_await wait_stimulate(port.clk);
                    port.read_enable = 1;
                    port.read_addr = addr;
                    co_await verify_at(port.clk, read_and_verify);
                }
                co_await wait_stimulate(port.clk);
                port.read_enable = 0;
            },
        };

        const auto do_read_write_without_enable{[&] -> task<void> {
            for(auto [addr, data]: gen | views::take(port_t::depth))
            {
                co_await wait_stimulate(port.clk);
                port.write_enable = 0;
                port.write_addr = addr;
                port.write_data = data;
                port.read_enable = 1;
                port.read_addr = addr;
                co_await verify_at(port.clk, read_and_verify);
            }

            auto read_first_dump{port.read_data_read_first.dump()};
            auto write_first_dump{port.read_data_write_first.dump()};
            auto no_change_dump{port.read_data_no_change.dump()};
            for(auto addr: gen | views::keys | views::take(port_t::depth))
            {
                co_await wait_stimulate(port.clk);
                port.read_enable = 0;
                port.read_addr = addr;
                co_await verify_at(port.clk, [&] {
                    CHECK_EQ(port.read_data_read_first, read_first_dump);
                    CHECK_EQ(port.read_data_write_first, write_first_dump);
                    CHECK_EQ(port.read_data_no_change, no_change_dump);
                });
            }
        }};

        const auto do_verify{
            [&] -> task<void> {
                auto pool{co_await get_spawn_pool()};
                pool.add_task(do_sync_write());
                pool.add_task(do_sync_read());
                co_await pool.join_all();

                pool.add_task(do_random_write());
                pool.add_task(do_random_read());
                co_await pool.join_all();

                co_await do_read_write_without_enable();
                co_await eval_finish();
            },
        };
        ctx.add_task(do_verify());

        ctx.loop_until_finish(1_us);
    }
}
