---
name: verilator-utils-code-review
description: 'Use when: reviewing, auditing, or assessing pull requests and code changes in the verilator_utils C++26/C++20-module project, including src framework code, scheduler/coroutine lifetimes, Verilator wrappers and context, hand-written RTL integration tests such as test/rtl_edge_detector.cpp, and agent-generated unit tests such as test/wrapper.cpp. Produces prioritized, evidence-based findings and checks test quality, timing semantics, coverage, and false-positive risks.'
argument-hint: 'Provide the diff, files, commit, or review scope'
---

# verilator_utils 代码审查

## 适用场景

- 审查 `src/`、`test/`、`rtl/` 下的改动。
- 审查一个 pull request、commit、工作区 diff 或指定的框架文件。
- 审计协程调度、Verilator 数据包装、DUT 上下文所有权、波形记录、覆盖率或模块导出。
- 判断测试是否有语义价值，而不是"能编译"或"提高了行覆盖率"。
- 区分手写 RTL 集成测试与批量生成的单元测试，并对二者采用不同的标准。

xmake 目标、选项与命令细节由 `.agents/skills/xmake/` 中的 xmake skills 提供，本 skill 不重复。

## 审查目标

找出被审查改动**引入或暴露**的具体缺陷。优先级依次为正确性、生命周期安全、仿真语义、公共 API 契约、可移植性、回归保护。不要把风格偏好、推测性的重构方案、以及与本改动无关的历史问题写成 finding。

## 必须执行的审查流程

1. 从被指定的文件或 diff 明确审查边界。读取足够的相邻声明、实现、测试与构建规则，理解改动的真实行为。
2. 识别受影响的契约：公共模块 API、所有权/生命周期、调度阶段、仿真时间、位宽范围与格式、波形记录、构建注册、测试期望。
3. 沿调用方与消费方追踪每条改动路径。涉及导出符号时，先搜出所有使用点，再断言兼容性或生命周期问题。
4. 依据目标、DUT 依赖、结构与意图，把被改动的测试分类为"手写 RTL 集成测试"或"生成的单元测试"；不要凭作者身份推断质量。
5. 在可行时用最小范围的测试或构建配置验证可疑行为。怀疑是生命周期、未定义行为或迭代器失效时，启用 sanitizer 或标准库加固。
6. 只报告可执行的 finding：能复现的触发条件、具体影响、以及被改动代码中的精确证据。若不存在这样的 finding，就明确说明，并总结剩余的验证缺口。

## Finding 的成立标准

一个 finding 必须同时满足：

- 指出对**被改动的契约**而言错误、不安全、不兼容、不稳定或缺少测试的行为。
- 说明触发它的输入、状态、调度顺序、平台或配置。
- 描述可观察的影响，而不是只复述一条规则。
- 指向最小范围的相关改动行；相邻的未改动代码只用于解释因果链。
- 给出修复方向，且不要求顺带做无关重构。

优先级：

- `P0`：无条件 catastrophic 影响，例如大面积数据损坏或发布产物不可用。
- `P1`：高影响正确性、内存安全、死锁、系统性仿真错误或常见构建失败。
- `P2`：在合理的边界情况、配置、数据宽度或调度路径上真实存在的缺陷。
- `P3`：范围有限但具体的可维护性或测试可信度问题，会掩盖未来的回归。

除非仓库规则使其成为正确性问题，否则不要报告格式、命名、注释语言、断言写法偏好或假想的误用。

## 框架代码审查

### C++ 模块与公共 API

- 确认目标 API 通过正确的导出模块或分区可达，且实现细节没有被意外导出。
- 检查模块归属（module purview）、全局模块片段（`module;`）中的头文件包含、`extern "C++"` 包含与依赖导入是否存在 ODR 或编译器可移植性风险。
- 把 API 兼容性当作刻意决策：删除的拷贝/移动操作、收紧的约束、返回类型与引用类别、异常行为、所有权转移都可能破坏现有测试或下游使用者。
- 对照受支持的 Verilator 标量与宽数据类型验证 concept，包括契约有意严格时的 cv/引用行为。
- 在模块接口文件（`src/*.cppm`）中，`using namespace` 必须保持在块作用域或 TU-local 作用域；命名空间作用域的 `using namespace` 仅保留给测试等内部文件。接口文件中允许的例外只有两处：对模块自身公共 inline 命名空间的 `export using namespace ::verilator_utils::data_format::interface;` 再导出，以及启用全文件 `"..."sv` 字面量的 TU 级 `namespace { using namespace ::std::string_view_literals; }`。
- 优先使用 `::std::string_view` 字面量（`"..."sv`）作为 `::std::format`/`::std::format_to` 的格式串、`check{}` 消息与波形文件名。

### 字符串格式化与 string_view 字面量

- 所有格式串与断言消息都使用 `"..."sv`；逐个检查 `::std::format`/`::std::format_to` 调用与 `check{}` 消息，缺少 `sv` 后缀会实体化临时对象，破坏该约定。
- 每个模块接口文件用一处 TU-local 的 `namespace { using namespace ::std::string_view_literals; }` 启用字面量运算符，位置在模块声明与导入之后。由于 using 指令使被提名名字出现在同时包含该指令与 `::std::string_view_literals` 的最近外围命名空间（即模块的全局作用域），`"..."sv` 在整个 TU 中可用——包括类作用域成员初始化器与嵌套命名空间中的特化。该指令位于匿名命名空间内，且 using 指令不会跨越模块导入，因此保持 TU-local，绝不导出给导入方。不要再写逐函数的 `using namespace ::std::string_view_literals;`。
- 内部格式化辅助函数的消息参数保持 `::std::string_view`，仅在 API 要求实体化时（如 `::std::format_error`）用 `::std::string{message}` 转换。
- 格式化结果传给 C 风格 API（如 `tracer->open`）时，用 `sv` 字面量格式化并传临时对象的 `.data()`。

### 前置条件与错误行为

- 需要避免的是 **`REQUIRE*` 系列**及任何会终止进程的 doctest 断言：框架的运行时数据校验不得用它们替代语言级契约，否则会把生产行为耦合到测试框架。`CHECK_*`、`CAPTURE` 等非致命设施属于仓库既有的 doctest 集成方式，不作为缺陷。
- 与 doctest 的**集成点**是既有设计，不应作为缺陷上报：`dut_context` 通过 doctest 上下文取得当前测试名、用 `ContextScope` 记录随机种子，并提供 `binary_path()` 等辅助；`verify_at()` 用 `CAPTURE(eval_time)` 把仿真时刻附加到断言上下文（`src/task.cppm`）；协程栈回溯提供 `::doctest::StringMaker`/formatter 特化；`src/main.cpp` 是唯一的 doctest 入口与 `setAsDefaultForAssertsOutOfTestCases()` 调用点。审查新增代码时，判断标准是新校验逻辑是否用 `REQUIRE*` 代替了语言级契约。
- 优先使用语言级契约：异常、约束（concept/`requires`）、或已文档化的前置条件与未定义行为，与该 API 的既有方向一致。
- 在校验之前检查算术：下溢、溢出、非法移位、零宽度、反向区间、越界字访问。
- 确认 `noexcept` 函数不会走到可能抛异常的校验、分配、格式化、回调或协程异常路径。
- 框架运行时校验的唯一入口是 `src/assert.cppm` 中的可调用对象 `check`（`src/*.cppm` 里写 `::verilator_utils::check{}`），底层实现是 `::verilator_utils::detail::check`；`test/common.cpp` 为便于单元测试把它引入 `::verilator_utils` 并导出，因此测试里用 `check{}`。
- 审查这类校验时确认调用形式是 `check{}(condition, ...)` 而不是 `check(condition, ...)`：条件与消息是 `operator()` 的实参，构造函数只接受 `::std::source_location`。`location` 只在构造时由默认实参捕获，复用同一个检查器对象会报出构造处位置。
- `check{}` 必须同时支持常量求值与运行时：常量求值语境失败抛 `::verilator_utils::constexpr_assertion_error`（使常量求值失败并产生编译错误），运行时失败抛 `::verilator_utils::assertion_error`，并由 `message()`/`location()`/`trace()` 暴露消息、位置与栈回溯。新增校验若绕开这条契约（例如在 `constexpr` 函数里改用会阻断常量求值的写法），需要指出。

### 所有权与生命周期

- 跟踪 `VerilatedContext`、DUT、tracer、调度器、任务、协程句柄、回调与切片对象的所有权。
- `bit_slice` / `vector_slice` 内部保存引用；确保被包装的 Verilator 变量活过切片，以及每个复制该切片的回调或协程。
- 检查析构顺序：队列中的协程句柄不得比调度器、DUT、上下文、被捕获状态或任务所有者活得更久。
- 检查移动、`detach`、`join_all`、`destroy`、异常路径中的双重析构、泄漏、空句柄访问、仍挂起的子任务与丢失的异常。
- 协程 lambda 按 `cpp-coroutine-lambda-safety` skill 判定：判定依据是闭包与所有被引用对象的生命周期，而不是"必须写 `this auto`"。

### 调度器与协程语义

- 在给出期望之前先建模真实的调度阶段顺序：`initial_eval` 初始化阶段，以及 `loop_once()` 中 `eval_ready_task → before_dut_eval → on_dut_eval → after_dut_eval → eval_end` 的顺序与各阶段的可等待性。
- `ready_queue_eval` / `wait_queue_eval` / `event_queue_eval` 的调用次序与收敛循环决定了"激励写在哪一侧被采样"；`wait_verify` 与 `wait_stimulate` 默认落在不同阶段，审查 RTL 测试期望时必须先确认这一点。
- 单独检查"立即就绪"的可等待体，因为 `await_suspend()` 可能从未执行；`await_resume()` 不得假定只有挂起路径才会初始化的状态。
- 检查父子协程转换、根任务与异步任务的所有权、调度器传播、final_suspend、异常重抛与提前结束（`eval_finish()` 抛出 `eval_finish_exception` 实现协作式取消）。
- 任何回调或协程恢复都可能重入地修改调度器队列（`register_ready()`、`event::notify_one()`、`spawn_pool::add_task()`）。对遍历中删除、追加、销毁或恢复任务的循环，套用 `cpp-container-iterator-invalidation` skill。
- 检查相等截止时间、空队列、单元素队列、多个就绪任务、评估过程中新增的任务、回调中被销毁的任务。
- 确认时间换算使用配置的 Verilator 时间单位与精度，且不存在截断、溢出、非法对齐或零值格式错误。注意 `eval_scheduler` 在构造时缓存时间精度与单位，因此 `dut_context` 必须先配置 `VerilatedContext` 再创建调度器。
- 注意 `eval_scheduler` 与 `dut_context` 都是不可拷贝、不可移动的（任务持有调度器指针，`dut_context` 的随机种子记录器依赖 doctest 上下文作用域栈的 LIFO 顺序）。

### Verilator 数据包装

- 分别审查标量 `CData`、`SData`、`IData`、`QData` 与 `VlWide<N>`；标量正确不能证明宽数据正确。
- 检查宽度边界：`1`、`8`、`16`、`31/32`、`63/64`、整字、部分最高字、跨字切片。
- 在按类型宽度移位之前检查移位操作数与掩码类型；宽度 64 与字对齐区间通常需要显式处理。
- 读/写上验证相对索引与绝对索引、闭区间边界、周围位的保持、最高字掩码、源/目标宽度一致性、以及别名/自赋值。
- 格式化与比较上验证有符号解释、符号-幅值、定点、float/double 位转换、NaN 的偏序、补位与前缀。

### DUT 上下文、波形记录与集成

- 验证上下文、DUT、调度器、tracer 的构造/析构顺序，以及 `final()`、波形 dump、覆盖率写出与结束路径恰好在预期时机发生。
- 把跨 DSO 的 Verilator RTTI 与 tracer 创建视为平台敏感项；确保运行时对象在 ABI 兼容的链接单元里创建与销毁。
- 检查波形选项、生成模型依赖、编译器运行时选择、PIC/静态/共享边界以及所需压缩库。
- 区分"陈旧的 C++ 模块 BMI/对象文件"导致的失败与真实源码缺陷；先用最小范围重新构建或重新配置，再提出无关的代码改动。

## 手写 RTL 集成测试

典型例子是 `test/rtl_*.cpp`，配合 `rtl/*.sv` 与 Verilator 生成的模型（如 `unit_test_rtl_edge_detector_verilator`）。当前主流写法：

```cpp
#include <verilator_fwd.hpp>
#include <doctest_macros.hpp>
import verilator_utils.full;
#include <unit_test_rtl_edge_detector_verilator.h>
#include <verilator_bwd.hpp>

TEST_SUITE("edge_detector")
{
    using namespace verilator_utils;
    using dut_t = unit_test_rtl_edge_detector_verilator;
    using dut_context_t = dut_context<dut_t, VERILATOR_TRACER>;
    // port_t 用 bit_slice/vector_slice 包装端口，并指定数据格式
}
```

把它们当作**可执行的时序规格**来审查：

- 期望值必须由 RTL 延迟、时钟极性、复位语义、采样沿、非阻塞赋值行为与调度阶段顺序推导出来。
- 检查采样沿两侧的异步激励、同步激励、复位后的第一个有效沿、稳定/无输入周期、以及流水线排空。
- 每个被 spawn 的验证任务都必须被持有并 join（`spawn_pool()` + `join_all()`）；测试不能在仍有校验挂起时结束。
- `eval_finish()` 只能在所有期望输出都观察到、且必要的波形排空周期结束后调用；`generate_clock` 会持续产生时钟，必须显式结束仿真。
- 局部端口包装、DUT/上下文对象、spawn 池与被引用状态必须活过所有协程任务。
- 优先表达明确的时序意图，而不是随手加延迟。波形看着对，但断言采样在错误的周期/阶段，仍然是不合格的测试。
- 复位辅助与时钟生成器必须与 RTL 的有效电平、边沿敏感方式一致。
- 评估在 Verilator 的 `--x-assign`、`--x-initial`、波形与覆盖率选项下的确定性；不要把偶然的随机初值编码成期望。
- 要求对语义输出与完成条件做断言，而不是只断言"没有超时"或"仿真正常退出"。

审查 RTL 集成测试改动时，先看对应的 `rtl/*.sv`、生成的模型目标与辅助函数语义，再断定某个期望值错误。

## 生成的单元测试

典型例子是覆盖面很广的组件测试，如 `test/wrapper.cpp`、`test/task.cpp`、`test/scheduler.cpp`。按"可信度与契约覆盖"审查：

- 独立重算期望常量、掩码、符号解释、格式化字符串、字序与仿真时间戳。生成的期望值本身不是正确性证据。
- 找出复制粘贴造成的缺口：重复的类型分支、遗漏的 `SData`/`IData`/`QData`/`VlWide`、反复断言同一条路径、与内容不符的测试名、构造后从未被真正校验的变量。
- 确认每个测试**能为目标缺陷而失败**：断言必须观察操作之后对外可见的状态，包括未受影响位的保持、任务完成/异常状态。
- 优先断言公共 API 的行为，而不是在测试里重写一遍实现算法；镜像实现会把同一个缺陷一起复制过去并通过。
- 实现在哪里分支，就在哪里要求边界矩阵：标量 vs 宽数据、单字 vs 多字、对齐 vs 跨字、零/最小/最大宽度、有符号 vs 无符号、立即就绪 vs 挂起后恢复、单个 vs 多个排队任务。
- 无效输入测试要对照 API 真实的错误契约；不接受依赖框架内部断言宏作为运行时校验的测试。
- 协程单元测试：确认协程 lambda 的闭包与所有被引用对象都活过执行、任务所有者活过等待、异常被重抛或断言、并且调度器被驱动到足以证明完成（`add_task` 后调用 `loop_until_finish()`）。
- 契约只保证集合成员时，拒绝依赖未指定顺序的断言；反之，调度顺序属于契约时要求精确顺序。
- 测试隔离：不得泄漏全局 Verilator 状态、不得依赖陈旧的波形/覆盖率假设、不得有顺序依赖、真实 sleep 或没有确定性种子与不变量的随机值。

测试数量多不能替代"能被变异杀死"的检查。抽查代表性测试时问：**哪一行实现缺陷会让这个测试失败？这条断言真的会失败吗？**

## 测试这些测试

当新增或可疑的测试显得空洞时，在范围允许的前提下使用以下手段之一：

1. 临时扰动对应的实现分支，确认最小范围的测试失败，然后恢复实现。
2. 用独立的手算、RTL 时序表或更简单的参考表示复核期望结果。
3. 只运行指定的目标并打开详细输出，确认预期的用例数量/套件确实被执行。
4. 额外覆盖相邻边界，以命中另一条实现分支。

不要把故意引入的扰动留在最终改动里。

## 交付前检查

执行 `verilator-utils-pre-delivery-check` skill（激活 `.venv` → 生成编译数据库 → clang-tidy → clang-format），本 skill 不重复其中内容。审查时需要额外注意：

- 先 clang-tidy 后 format，两者都要跑；只读审查也应执行，因为新告警可能正是本次改动引入的缺陷。
- 格式化会就地改写文件，改出来的内容必须复核为纯格式化（无参数默认值、无语义变化），并保持 `git diff --check` 通过。
- 构建目标、配置项、测试运行方式与模块相关选项由 `.agents/skills/xmake/` 中的 xmake skills 说明。
- 不要凭单一编译器/运行时配置断言广泛的移植性或完整回归覆盖。明确写出实际验证过的目标与配置。

## 审查输出

先列 finding，按优先级、再按源码位置排序。每条 finding 包含：

- 以 `[P0]`–`[P3]` 开头的简短标题。
- 最小可用的文件/行号范围。
- 触发条件、因果链，以及用户可见或测试可见的影响。
- 当修复方向不明显时，给出一条聚焦的修复方向。

在 finding 之后，可选列出：

- 实际运行过的验证命令及其结果。
- 剩余风险或未覆盖的配置。

如果没有任何可执行的 finding，就明确说明未发现具体缺陷；不要为凑数而编造低价值意见。

## 最终检查清单

- 审查边界与被改动的契约已明确。
- 涉及公共模块与下游使用时，已检查相关使用点。
- 协程、回调与被引用对象的生命周期在所有挂起点上有效。
- 调度器期望与实际阶段顺序和队列修改顺序一致。
- 已考虑标量、宽数据、边界宽度与跨字包装路径。
- 生产数据校验不依赖 `REQUIRE*` 等致命 doctest 断言（与 doctest 的既有集成点除外），且新校验通过 `check{}(condition, ...)` 调用、`source_location` 在构造处捕获。
- 手写 RTL 测试证明了采样、复位、延迟、join 与结束语义。
- 生成的测试有独立核实的期望值，且没有明显的复制粘贴覆盖缺口。
- 格式串与断言消息使用 `"..."sv`，并由每个接口文件顶部那一处 `namespace { using namespace ::std::string_view_literals; }` 启用。
- 命名空间作用域的 `using namespace` 只出现在内部文件（测试）或两种获准的接口文件写法中。
- finding 具体、有优先级、指向被改动代码，且不含纯风格噪音。
