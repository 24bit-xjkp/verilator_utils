---
name: verilator-utils-unit-testing
description: 'Use when: writing, refactoring, or reviewing unit tests for the verilator_utils C++ module project. Covers the unit_test common module, doctest assertion style, scheduler/coroutine and RTL integration test patterns, Verilator data edge cases, and the required pre-delivery static-analysis and formatting commands.'
argument-hint: 'Describe the component or behavior to test'
---

# verilator_utils 单元测试

## 适用场景

- 在 `test/` 下新增 C++ 测试。
- 重构现有 doctest 测试。
- 为工具函数、调度器行为、信号包装或基于 Verilator 的 RTL 辅助功能补回归测试。
- 判断某个断言是否真的能失败、某个边界是否被覆盖。

构建目标注册、编译选项、如何运行某个测试等 xmake 细节由 `.agents/skills/xmake/` 中的 xmake skills 提供。运行任何 xmake 命令前先 `source .venv/bin/activate`（见 `AGENTS.md`），这样无需逐条判断命令是否依赖 Python。本 skill 只保留 C++ 测试代码本身的约定；交付前的静态检查与格式化流程见 `verilator-utils-pre-delivery-check` skill。

## 测试文件基本结构

### 框架单元测试（`test/<basename>.cpp`）

```cpp
#include <doctest_macros.hpp>
#include <assert_macros.hpp>   // 仅在需要 VU_CHECK 时包含
import unit_test;

TEST_SUITE("verilator_utils/<basename>")
{
    TEST_CASE("precise English name")
    {
        CHECK_EQ(actual, expected);
    }
}
```

- `import unit_test;` 是公共测试环境的唯一入口，由 `test/common.cpp` 定义；它 `export import verilator_utils.full;` 并再导出 `::verilator_utils`、`::verilator_utils::verilator`、`::std::string_view_literals` 三个命名空间，还提供 `fake_dut`、`verilator_version` 等适配不同 Verilator 版本的工具。
- **不要**再直接 `import verilator_utils.full;`，也**不要**包含/定义另一个 doctest main：唯一的 doctest 入口是 `src/main.cpp`（目标 `verilator_utils_main`）。
- 只需 `#include <doctest_macros.hpp>`（宏与断言），不要 `#include <doctest.h>`。
- `test/common.cpp` 已把 `::std::string_view_literals` 再导出，因此 `"..."sv` 在测试文件中直接可用。
- 套件名约定：框架单元测试为 `verilator_utils/<basename>`（与文件名一致），RTL 集成测试用对应模块名（如 `TEST_SUITE("edge_detector")`）。
- 测试文件属于内部代码，命名空间作用域的 `using namespace` 是允许的；模块接口文件（`src/*.cppm`）不受此豁免。

### RTL 集成测试（`test/rtl_<name>.cpp`）

```cpp
#include <verilator_fwd.hpp>
#include <doctest_macros.hpp>
import verilator_utils.full;
#include <npy.h>                                            // 需要读取激励数据时
#include <unit_test_rtl_<name>_verilator.h>                 // Verilator 生成的模型
#include <verilator_bwd.hpp>                                // 必须在生成头之后

TEST_SUITE("<name>")
{
    using namespace verilator_utils;
    using dut_t = unit_test_rtl_<name>_verilator;
    using dut_context_t = dut_context<dut_t, VERILATOR_TRACER>;
}
```

- `verilator_fwd.hpp` 必须在 Verilator 头之前、`verilator_bwd.hpp` 必须在之后，否则会撞上 Verilator 的 tracer 宏。
- `VERILATOR_TRACER` 由构建脚本按波形选项注入（`VerilatedFstC` 或 `VerilatedVcdC`），测试代码不要写死。

## 常用夹具与写法

### 调度器测试夹具

```cpp
namespace
{
    struct scheduler_fixture
    {
        ::VerilatedContext context{};
        ::fake_dut dut{context};

        explicit scheduler_fixture(::std::int32_t time_unit = -9, ::std::int32_t time_precision = -12)
        {
            context.timeunit(time_unit);
            context.timeprecision(time_precision);
        }

        [[nodiscard]] ::verilator_utils::eval_scheduler make_scheduler() noexcept
        { return ::verilator_utils::eval_scheduler{dut}; }
    };
}
```

- `eval_scheduler` 构造时缓存时间单位与精度，所以必须先配置 `VerilatedContext` 再创建调度器。
- 夹具接收负指数（`-9` 表示 ns）而不是枚举，便于同一个用例覆盖多组单位/精度。

### 协程任务测试

```cpp
TEST_CASE("wait resumes at the requested simulation time")
{
    scheduler_fixture fixture{};
    auto scheduler{fixture.make_scheduler()};

    const auto&& task{
        [&] -> ::verilator_utils::task<void> {
            co_await ::verilator_utils::wait_time(5_ps);
            CHECK_EQ(scheduler.time_in_time_precision(), 5u);
        },
    };

    scheduler.add_task(task());
    scheduler.loop_until_finish();
}
```

- 闭包由本 `TEST_CASE` 的局部变量持有、并同步驱动调度器，因此 `[&]` 是安全且常用的；闭包是临时对象（如立即调用）时才需要 `(this auto)` 把闭包搬进协程帧。判断依据见 `cpp-coroutine-lambda-safety` skill。
- 必须真正驱动调度器（`add_task` 之后 `loop_until_finish()`）。只创建任务而不驱动时，协程体一次都没有执行，测试会在"零断言"的情况下虚假通过。
- 断言写在协程体内时，doctest 会把失败记录在当前测试上下文；需要观察协程内的完成状态时，把结果写到外层变量再在测试主体断言，或用 `CAPTURE` 附加上下文。

### RTL 集成测试：激励与验证分离

```cpp
constexpr static auto period{1_ns};
ctx.add_task(generate_clock(port.clk, period));

const auto verify{
    [&](bool rising, bool falling) -> task<void> {
        return verify_at(port.clk, [=, &port] { CHECK_EQ(port.rising, rising); }, delay);
    },
};
const auto stimulate{
    [&] -> task<void> {
        co_await generate_reset(port.rst, port.clk);
        auto verify_tasks{co_await get_spawn_pool()};                    // 子任务池：join_all 会抛出汇总异常
        const auto do_verify{[&](bool rising, bool falling) { verify_tasks.add_task(verify(rising, falling)); }};

        co_await wait_stimulate(port.clk);                               // 下降沿、评估前加激励
        port.signal = 1;
        do_verify(true, false);

        co_await verify_tasks.join_all();                                // 必须等待所有校验完成
        co_await wait_stimulate(port.clk, 2);                            // 避免波形被截断
        co_await eval_finish();
    },
};
ctx.add_task(stimulate());

ctx.loop_until_finish();
```

- 时序语义要点：`wait_verify` 默认在**上升沿、电路评估完成后**返回（适合采样检查）；`wait_stimulate` 默认在**下降沿、电路评估前**返回（适合驱动激励），传入 `edge_enum::rising` 时自动改为评估后。
- `generate_clock` 会无限产生时钟，必须由 `eval_finish()` 结束仿真；`dut_context` 析构时会 `final()`，并在启用覆盖率且已进入仿真循环时写出覆盖率文件。
- 每个 spawn 的校验任务都要 join；`join_all()` 抛出的 `spawn_pool::join_all_exception` 汇总了所有子任务异常，不要吞掉。

### 其他常用激励辅助（`::verilator_utils` 命名空间）

| 名称 | 用途 |
| --- | --- |
| `generate_clock(clk, period, delay = 0_fs, duty_ratio = 0.5)` | 持续产生时钟；必须由 `eval_finish()` 结束仿真 |
| `generate_reset(reset, clk, cycle = 3, active_high = true)` | 同步复位，持续 `cycle` 个下降沿 |
| `generate_async_reset(reset, duration, active_high = true)` | 持续 `duration` 时间的异步复位 |
| `wait_reset_finish(rst, active_high = true)` | 等待复位信号无效 |
| `wait_time` / `wait_event` / `wait_posedge` / `wait_negedge` / `wait_alledge` | 时间、事件与边沿等待 |
| `wait_eval_stage` / `verify_at` / `wait_verify` / `wait_stimulate` | 评估阶段与激励/验证时机 |
| `max_eval_time(duration)` | 仿真超时保护：到时后标记调度器错误、结束仿真并抛出 `eval_timeout_exception` |
| `eval_finish()` / `get_time_in_string()` / `get_time_in_time_precision()` / `get_scheduler()` / `stacktrace()` | 结束仿真、读取时间、读取调度器、协程栈回溯（均为可等待体） |

需要精确签名时以 `src/task.cppm` 的注释为准。

## doctest 断言风格

- 优先使用二元断言宏而非布尔表达式：`CHECK_EQ(actual, expected)`、`CHECK_NE`、`CHECK_LT`、`CHECK_LE`、`CHECK_GT`、`CHECK_GE`。
- 只有在测试无法安全继续时才用 `REQUIRE_*`（例如后续断言会解引用可能为空的对象）：它是唯一会终止当前用例的断言系列，滥用会让一次运行只暴露第一个问题。框架的运行时校验也必须避免 `REQUIRE*`，改用 `VU_CHECK` 等语言级契约。
- 需要诊断上下文时用 `CAPTURE(value)`；领域信息更清晰时用 `CHECK_MESSAGE(condition, message)`。
- 需要字符串消息时使用 `"..."sv` 字面量：`VU_CHECK(width != 0, "数据宽度不能为0，实际为{}"sv, width);`。
- 一个逻辑组件一个 `TEST_SUITE`，行为拆分为多个名称精确的 `TEST_CASE`。
- 对编译期契约使用 `static_assert`（concept、类型别名、模块 API 形状）。
- 生成随机场景时记录种子（`VerilatedContext::randSeed()`），使失败可复现；不要依赖真实 sleep 或未指定顺序。

## 项目风格

- 全局名字使用 `::` 前缀（`::std::size_t`、`::verilator_utils::eval_scheduler`、`::CData`）。
- 导入使用 C++ 模块（`import unit_test;`、`import verilator_utils.full;`）。
- 局部常量优先花括号初始化：`auto scheduler{fixture.make_scheduler()};`；`static constexpr` 数据用 `::std::array` + 结构化绑定驱动表驱动测试。
- 时间使用字面量：`1_ns`、`5_ps`、`2.5_ns`（`::verilator_utils::literals` 已由 `unit_test` 再导出）。
- 比较或格式化字符串用 string_view：`CHECK_EQ(scheduler.time_in_string(), "0ns"sv)`、`CHECK_EQ(::std::format("{}"sv, value), "[10, 20]"sv)`。

## 边界用例清单

- 数值换算：零、最小非零值、小数截断、单位边界换算（fs/ps/ns/us/ms）以及整/小数与负指数组合。
- 算术包装：恒等用例、零操作数、截断除法/乘法、相等与序关系。
- 格式化辅助：十六进制位边界附近的宽度（`1`、`4`、`5`、`8`、`16`、`32`）以及字对齐宽度。
- Verilator 标量数据：测试类型特征或格式化时覆盖 `CData`、`SData`、`IData`、`QData`；无效输入用 `CHECK_THROWS_AS(..., ::verilator_utils::assertion_error)`。
- Verilator 宽数据：`VlWide<1>`、多字 `VlWide<N>`、部分最高字、字对齐宽度、跨字切片。
- 类型特征：包含反例（普通 C++ 类型、cv 限定变体），前提是 concept 有意拒绝它们。
- 调度器/协程：立即就绪 vs 挂起后恢复、相等截止时间、空队列、单元素队列、多任务、任务在评估过程中新增、`eval_finish()` 取消路径、`join_all()` 的异常汇总。
- RTL/时序：复位行为、复位后第一个有效沿、同步转换、异步脉冲、流水线排空、期望输出耗尽。

## 工作流

1. 先读被改组件的 API 与相邻测试，确认既有写法。
2. 在最小相关的 `test/<basename>.cpp` 中新增或修改测试；RTL 测试改对应的 `test/rtl_<name>.cpp`。
3. 使用结果显而易见的确定性取值；边界用例紧邻它所校验的正常行为。
4. 自问"哪一行实现缺陷会让这个测试失败"——如果答不上来，测试就是空洞的。
5. 运行测试（目标名、过滤、详细输出等由 `.agents/skills/xmake/` 中的 xmake skills 说明），只修与被改动测试或目标行为相关的失败。
6. 若因修改 C++ 模块导致 BMI/对象文件陈旧而构建失败，按 xmake skills 的说明重建；不要为了让测试通过而改动实现语义。

## 交付前检查

交付前执行 `verilator-utils-pre-delivery-check` skill（激活 `.venv` → 生成编译数据库 → clang-tidy → clang-format），本 skill 不重复其中内容。对测试作者而言要额外记住：

- 新测试不应引入任何 clang-tidy 告警，基线是 0 error / 0 warning。
- 格式化会改写测试文件，之后要确认没有语义变化，再重跑受影响的测试。
- 在交付说明中写明实际使用的目标与配置，不要凭单一配置断言完整的回归覆盖。
