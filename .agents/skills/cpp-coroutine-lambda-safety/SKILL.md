---
name: cpp-coroutine-lambda-safety
description: 'Use when: writing, refactoring, or reviewing C++ coroutine lambdas, especially lambdas returning task/async_task or using co_await/co_return. Covers explicit object parameters, safe closure lifetime, capture lifetime boundaries, coroutine awaiter callback lifetimes, and validation of scheduler-driven tasks.'
argument-hint: 'Describe the coroutine lambda or task code to review/refactor'
---

# C++ 协程 Lambda 安全

## 适用场景

- 编写或重构捕获列表非空的协程 lambda（`[&]`、`[=]`、`[foo]`）。
- 审查返回 `::verilator_utils::task<T>` / `::verilator_utils::async_task` 的 lambda。
- 排查疑似由协程生命周期引起的 sanitizer 报告（use-after-free、stack-use-after-scope）。
- 编写 `wait_event` 回调、调度器任务、spawn 子任务的测试。

## 核心规则

- 协程帧保存的是**协程状态**；lambda 的捕获保存在**闭包对象**里，二者生命周期彼此独立。
- 普通协程 lambda 的 `operator()` 通过隐式 `this` 指针访问闭包，闭包本身不会被复制进协程帧。
- 闭包是否安全，只取决于一个问题：**协程最后一次被恢复/销毁时，这个闭包对象还活着吗？**
- 需要把闭包搬进协程帧时，使用按值显式对象形参 `this auto`；不需要时按普通捕获写即可，不要无条件加 `this auto`。
- 复制闭包只复制捕获的**值**和**引用成员**，不会延长被引用对象的寿命。无论是否写 `this auto`，每个被引用的对象都必须比协程活得更久。
- 返回类型必须是协程任务类型（或含 `co_await`/`co_return`/`co_yield`），否则 lambda 不是协程，闭包按普通函数对象处理。

## 决策流程

1. 找出函数体中含 `co_await`、`co_return`、`co_yield` 的 lambda，或返回类型为协程任务类型的 lambda。
2. 判断 lambda 对象的存活范围：它是否覆盖协程的每一个挂起点与调度器驱动的每一次恢复？
3. 若覆盖（例如命名后存入外层作用域的 `const auto`，或被本测试用例的局部变量持有直到 `loop_until_finish()` 返回）→ 按普通捕获写，无需 `this auto`。
4. 若不覆盖（临时对象、立即调用后即析构、被 `co_await` 的闭包可能比调用者活得更久）→ 用 `(this auto, ...)` 把闭包按值放进协程帧。
5. 不要保留仅为"搬运捕获变量"而存在的形参：真正的每次调用输入才放在 `this auto` 之后。
6. 逐个调用点同步更新签名。
7. 非协程的嵌套谓词、回调按它们自己的生命周期单独判断，不要一概改写。

## 当前仓库中的典型写法

### 局部持有闭包：直接用 `[&]`

调度器与框架单元测试中的主流写法。闭包被命名的局部变量持有，`add_task` 之后同步执行 `loop_until_finish()`，因此闭包必然活过整个协程：

```cpp
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
```

这里 `[&]` 是正确且被推荐的：`scheduler`、`fixture` 都是本 `TEST_CASE` 的局部对象，测试结束前协程一定已经执行完毕。

### 立即调用：显式 `this auto` 把闭包搬进协程帧

lambda 立即被调用（临时闭包）时，必须自己把闭包搬进协程帧：

```cpp
auto child{[&](this auto) -> ::verilator_utils::task<int> {
    co_await ::verilator_utils::wait_time(2_ps);
    return value;
}()};
```

注意：`[&](this auto)` 复制的是**含引用的闭包**，它消除的是"闭包指针悬空"，而不是"被引用对象悬空"。`value` 仍必须在协程结束前存活。

### 可复用协程 lambda：每次调用输入放在 `this auto` 之后

```cpp
const auto make_waiter{
    [&](this auto, int task_id) -> ::verilator_utils::task<void> {
        co_await ::verilator_utils::wait_event([&event_ready] { return event_ready; });
        resumed_tasks.push_back(task_id);
    },
};

auto first{make_waiter(1)};
auto second{make_waiter(2)};
```

### 无捕获：`[]` 或 `[](this auto)`

```cpp
auto child{[] -> ::verilator_utils::task<void> { co_return; }()};
```

无捕获闭包是空类型，不会产生悬空访问；是否写 `this auto` 只是习惯问题。若后续要加捕获，再按上面的规则重新判断。

### `wait_event` 回调：捕获必须比事件等待更久

`wait_event(callback)` 会把回调存进事件等待队列，并在**每次电路评估后**反复调用，直到返回 `true`：

```cpp
co_await ::verilator_utils::wait_event([&event_ready] { return event_ready; });  // 需要 event_ready 活过整段等待
```

- 回调被复制进等待体；`[&]` 捕获的是引用，`[=]` 捕获的是值。若被捕获的局部变量会在等待结束前离开作用域，必须改成按值捕获或把状态提升到外层作用域。
- 需要跨多次调用维护状态的谓词应使用 `mutable`（框架的 `wait_posedge`/`wait_negedge`/`wait_alledge` 就是这么实现的）。

## 易错点

- `[&](this auto)`：修复闭包悬空，不修复被引用对象悬空。
- `[=](this auto)`：先把捕获按值复制进闭包、再把闭包复制进协程帧；确认复制语义（尤其是 `std::unique_ptr` 之类不可复制类型）符合预期。
- `this auto&` / `this auto&&`：仍然引用原闭包，**不能**用来把闭包搬进协程帧。
- `[&]` 且无 `this auto`：必须能证明原闭包活到协程销毁为止（命名局部变量 + 同步驱动调度器是有效证明）。
- 悬空引用的其他常见来源与捕获列表无关：任务对象 `task<T>` 被提前析构、`async_task`/spawn 池的所有者先于协程销毁、调度器/DUT 上下文先于队列中的协程销毁、`bit_slice`/`vector_slice` 背后的 Verilator 变量先于切片销毁。
- 语料提示：本仓库的 `.clang-tidy` 显式关闭了 `cppcoreguidelines-avoid-capturing-lambda-coroutines` 与 `cppcoreguidelines-avoid-reference-coroutine-parameters`，因此"捕获 lambda 协程一律报错"不是本项目的规则；判断依据只能是生命周期，不是 lint 偏好。

## 审查清单

- 每个协程 lambda 要么把闭包搬进协程帧（按值 `this auto`，包括 `[](this auto, ...)`），要么能证明原闭包的生命周期长于该协程。
- 每个被引用捕获的对象（调度器、DUT 上下文、任务所有者、输出容器、标志位）都活过协程的所有挂起点。
- `this auto` 之后的形参是真正的每次调用输入，而不是被反复搬运的局部变量。
- 所有调用点与新的形参列表一致。
- `async_task` / `spawn_pool` / 任务所有者对象活过等待它们的协程；`join_all()` 之前不要销毁池。
- 事件回调、awaiter 内部回调和协程 lambda 分别判断，不混用结论。

## 交付前检查

1. 在改动过的文件里重新搜索协程 lambda，确认每个都符合上面的生命周期判定（`co_await`、`co_return`、`co_yield`、`-> ...::task<`）。
2. 编译诊断检查：删掉形参后是否还有调用点未更新。
3. 运行测试（目标、选项与运行方式见 `.agents/skills/xmake/` 中的 xmake skills；运行任何 xmake 命令前先 `source .venv/bin/activate`，无需逐条判断命令是否依赖 Python）。注意 clang-tidy 覆盖不到生命周期缺陷，它只能保证 `clang-analyzer-cplusplus.*` 等静态检查项没有新告警，交付前的静态检查与格式化流程见 `verilator-utils-pre-delivery-check` skill。
4. 若 sanitizer 仍报告 use-after-free / stack-use-after-scope，先排查协程句柄的所有权、`task`/`async_task` 的析构顺序和队列中的挂起协程，不要默认是捕获列表的问题。
