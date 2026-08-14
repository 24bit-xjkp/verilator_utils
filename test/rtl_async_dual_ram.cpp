#include <verilator_fwd.hpp>
#include "doctest_macros.hpp"
import verilator_utils.full;
#include <unit_test_rtl_async_dual_ram_verilator.h>
#include <verilator_bwd.hpp>

TEST_SUITE("dual_ram")
{
    using namespace verilator_utils;
    namespace views = std::views;
    namespace ranges = std::ranges;
    using dut_t = unit_test_rtl_async_dual_ram_verilator;
    using dut_context_t = dut_context<dut_t, VerilatedFstC>;

    struct port_t
    {
        bit_slice<CData> read_clk;
        bit_slice<CData> read_enable;
        vector_slice<CData> read_addr;
        vector_slice<CData> read_data;
        bit_slice<CData> write_clk;
        bit_slice<CData> write_enable;
        vector_slice<CData> write_addr;
        vector_slice<CData> write_data;

        constexpr inline static auto data_width{8zu};
        constexpr inline static auto addr_width{3zu};

        inline explicit port_t(dut_t& dut) :
            read_clk{dut.read_clk}, read_enable{dut.read_enable, 0, boolean}, read_addr{dut.read_addr, addr_width},
            read_data{dut.read_data, data_width}, write_clk{dut.write_clk}, write_enable{dut.write_enable, 0, boolean},
            write_addr{dut.write_addr, addr_width}, write_data{dut.write_data, data_width}
        {
        }
    };

    TEST_CASE("dual_ram")
    {
        dut_context_t ctx{true, verilator_time_unit::ns, verilator_time_unit::ns};
        port_t port{ctx.get_dut()};
        using pair_t = std::pair<std::uint8_t, std::uint8_t>;
        mailbox<std::uint8_t> queue{};
        constexpr static auto ram_depth{1zu << port.addr_width};
        auto ram{views::repeat(format_wrapper<std::uint64_t>{0, port.data_width}, ram_depth) | ranges::to<std::vector>()};
        constexpr static auto epochs{8zu};
        std::array<pair_t, ram_depth * epochs> operation_list{};

        std::mt19937 engine{ctx.get_seed()};
        for(auto&& [i, pair]: views::enumerate(operation_list))
        {
            auto&& [addr, data]{pair};
            addr = i % ram_depth;
            data = i;
        }
        ranges::shuffle(operation_list, engine);

        constexpr static auto write_clk_period{14_ns};
        constexpr static auto write_clk_delay{0_ns};
        constexpr static auto read_clk_period{26_ns};
        constexpr static auto read_clk_delay{9_ns};

        ctx.add_task(generate_clock(port.write_clk, write_clk_period, write_clk_delay));
        ctx.add_task(generate_clock(port.read_clk, read_clk_period, read_clk_delay));

        const auto do_write{
            [&] -> task<void> {
                constexpr static auto enable_iters{(epochs - 1) * ram_depth};
                // 测试写入使能情况
                for(auto&& [addr, data]: operation_list | views::take(enable_iters))
                {
                    co_await wait_stimulate(port.write_clk);
                    port.write_enable = true;
                    port.write_addr = addr;
                    port.write_data = data;

                    co_await wait_verify(port.write_clk);
                    ram[addr] = data;
                    co_await queue.put(addr);
                }

                // 测试写入失能情况
                for(auto&& [addr, data]: operation_list | views::drop(enable_iters))
                {
                    co_await wait_stimulate(port.write_clk);
                    port.write_enable = false;
                    port.write_addr = addr;
                    port.write_data = data;
                }
            },
        };

        const auto do_read{
            [&] -> task<void> {
                co_await wait_event([&] { return queue.num() != 0; });
                while(queue.num() != 0)
                {
                    co_await wait_stimulate(port.read_clk);
                    auto&& addr{co_await queue.get()};
                    port.read_enable = true;
                    port.read_addr = addr;
                    co_await verify_at(port.read_clk, [&] {
                        CAPTURE(addr);
                        CHECK_EQ(port.read_data, ram[addr]);
                    });
                }
            },
        };

        const auto do_verify{
            [&] -> task<void> {
                auto tasks{co_await get_spawn_pool()};
                tasks.add_task(do_write());
                tasks.add_task(do_read());
                co_await tasks.join_all();

                // 测试内存内容是否符合预期
                for(auto&& [addr, data]: views::enumerate(ram))
                {
                    co_await wait_stimulate(port.read_clk);
                    port.read_enable = true;
                    port.read_addr = addr;
                    co_await verify_at(port.read_clk, [&] { CHECK_EQ(port.read_data, data); });
                }

                auto previous_data{port.read_data.dump()};
                co_await wait_stimulate(port.read_clk);
                port.read_enable = false;
                port.read_addr = (port.read_addr + 1) % ram_depth;
                co_await verify_at(port.read_clk, [&] { CHECK_EQ(port.read_data, previous_data); });

                co_await wait_stimulate(port.read_clk);
                co_await eval_finish();
            },
        };
        ctx.add_task(do_verify());
        ctx.loop_until_finish();
    }

    TEST_CASE("context random seed management")
    {
        dut_context_t ctx{true, verilator_time_unit::ns, verilator_time_unit::ns};

        CHECK_EQ(ctx.get_seed(), static_cast<std::size_t>(ctx.get_context().randSeed()));

        const auto seed{ctx.get_seed()};
        CHECK_EQ(ctx.get_seed(), seed);

        ctx.get_context().randSeed(42);
        CHECK_EQ(ctx.get_seed(), 42zu);
        CHECK_EQ(ctx.get_seed(), static_cast<std::size_t>(ctx.get_context().randSeed()));
    }
}
