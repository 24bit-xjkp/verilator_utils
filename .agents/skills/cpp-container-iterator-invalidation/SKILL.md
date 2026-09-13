---
name: cpp-container-iterator-invalidation
description: 'Use when: writing, refactoring, reviewing, or debugging C++ loops that mutate std::vector or other containers through erase, pop_back, push_back, emplace_back, insert, clear, swap-removal, or callbacks. Covers iterator/reference invalidation, cached end iterators, standard-library hardening assertions, safe index-based traversal, and regression tests.'
argument-hint: 'Describe the container mutation loop or iterator assertion to review'
---

# C++ 容器迭代器失效

## 适用场景

- 诊断标准库加固/调试模式下出现的 singular、past-the-end、incompatible、invalid iterator 断言失败。
- 审查在遍历容器的同时修改该容器的循环（erase、pop_back、push_back、emplace_back、insert、clear、swap 删除）。
- 实现"用尾部元素覆盖 + `pop_back()`"的无序删除。
- 修复只在 release 构建或某个标准库实现下"看起来正常"的可疑代码。
- 审查遍历过程中被调用的回调（包括协程恢复点），因为回调可能重入地修改同一个容器。

## 核心模型

- 迭代器、指针、引用只在容器操作的不失效保证下才有效；操作一旦失效，比较、自增、解引用、相减**全部**是未定义行为。
- 尾后迭代器（`end()`）同样参与失效规则；它**不是**永久稳定的。
- 重新计算 `end()` 只能修复被缓存的尾后迭代器，**不能**修复同时失效的当前迭代器。
- 失效后的迭代器在 release 构建中往往仍指向同一地址，于是错误行为"碰巧可用"；调试模式才暴露它。
- 不同标准库实现（libstdc++ / libc++）与不同加固等级的诊断覆盖范围不同。可移植性来自遵守标准的不失效规则，而不是来自"在我这台机器上没崩"。

## `std::vector` 的关键规则

- `pop_back()`：使被删除元素的迭代器/引用以及尾后迭代器失效。
- `erase(pos)`：使 `pos` 及其之后的所有迭代器/引用失效，包括旧的 `end()`。
- `erase(first, last)`：返回新的有效迭代器；用它接替，不要自增已失效的迭代器。
- `push_back()`、`emplace_back()`、`insert()`：一旦发生扩容，**所有**迭代器/指针/引用失效；即使没有扩容，插入点及其之后的迭代器/引用以及旧 `end()` 仍然失效。
- `clear()`：使所有元素迭代器/指针/引用失效。
- 对已存在元素赋值本身不改变结构，不会失效迭代器；但紧随其后的结构性修改仍然会。
- `push_back`/`emplace_back` 也可能因扩容而使**元素的引用**失效——持有 `auto&& ref{item}` 后再追加是典型陷阱。

其他容器按同样的方式推理（例如 `std::unordered_map::erase(iter)` 只失效被删除元素，`erase` 返回下一个迭代器）。

## 诊断步骤

1. 列出容器派生出的所有句柄：迭代器、引用、指针、`std::span`、缓存的 `end()`、由 `data()` 得到的裸指针。
2. 列出所有可能改变结构的操作，包括藏在被调用函数、回调、协程恢复点里的操作。
3. 在每个修改点按上表逐个套用失效规则，判断哪些句柄仍然有效。
4. 检查修改点**之后**的每一次使用，尤其不要漏掉循环条件和自增表达式——它们经常隐式使用旧句柄。
5. 单独覆盖边界路径：首元素、中间元素、末元素、单元素容器、连续删除、删除全部元素。
6. 把"在某个标准库下能跑通"当作无效证据：改用带迭代器调试或加固的配置复现。

## 安全遍历写法

### 顺序保持删除：接住 `erase()` 的返回值

```cpp
for(auto iter{items.begin()}; iter != items.end();)
{
    if(should_remove(*iter))
    {
        iter = items.erase(iter);
    }
    else
    {
        ++iter;
    }
}
```

### 无序删除：用索引，不要用迭代器

```cpp
for(::std::size_t index{}; index != items.size();)
{
    if(should_remove(items[index]))
    {
        items[index] = ::std::move(items.back());
        items.pop_back();
    }
    else
    {
        ++index;
    }
}
```

- 删除后**不要**自增 `index`：刚被搬到当前位置的元素还没有检查过。
- `index + 1 == items.size()` 时是自赋值，对某些元素类型并不安全；如有这种可能，先分支跳过赋值或改用类型自有的安全操作。
- 不要在 `pop_back()` 之后继续使用之前取得的元素引用——它可能指向刚被删除的尾部元素。

### 追加元素时保持索引

遍历中向同一容器追加元素时，用索引而不是迭代器，把"新增部分"当作后续处理：

```cpp
// src/scheduler.cppm：就绪队列每轮评估都会被协程恢复操作重入地扩充
bool ready_queue_eval()
{
    bool any_coroutine_run{!ready_queue.empty()};
    auto i{0zu};
    try
    {
        for(; i != ready_queue.size(); ++i) { resume_coroutine(ready_queue[i]); }
    }
    catch(...)
    {
        // 已消费的前缀需要清理，未消费的部分留给上层处理
        auto begin{ready_queue.begin()};
        ready_queue.erase(begin, begin + static_cast<::std::ptrdiff_t>(i) + 1);
        throw;
    }
    ready_queue.clear();
    return any_coroutine_run;
}
```

- 循环条件每轮重新读取 `size()`，因此新追加的元素也会被执行。
- 捕获异常后显式删除已消费前缀，避免重复恢复。

### 分离"选择"与"结构性修改"

当删除顺序、移动代价、异常行为与回调副作用都允许时，优先用算法代替手写循环：

```cpp
auto removed_count{::std::erase_if(items, should_remove)};
static_cast<void>(removed_count);  // 不需要计数时也要显式消费，避免 -Wunused-variable
```

`::std::erase` / `::std::erase_if` 对 `std::vector`、`std::list`、关联容器均有重载；对自定义容器先确认是否存在重载。注意 `erase_if` 返回删除个数，不是迭代器。

### 批量删除前缀

```cpp
// src/assert.cppm：丢弃回溯中位于断言宏之上的帧
trace.frames.erase(trace.frames.begin(), erase_begin);
```

区间删除返回的迭代器就是新的 `begin()`，不必也不应再使用 `erase_begin`。

## 常见错误写法

```cpp
for(auto iter{items.begin()}, end{items.end()}; iter != end;)
{
    if(should_remove(*iter))
    {
        *iter = items.back();
        items.pop_back();
        end = items.end();  // 只修复了尾后迭代器
    }
    else
    {
        ++iter;
    }
}
```

- 重算 `end` 只处理了旧尾后迭代器的失效。
- 当 `iter` 指向的正是被删除的尾部元素时，`pop_back()` 同时使 `iter` 失效。
- 下一轮循环条件就在比较一个已失效（singular）的迭代器，属于未定义行为。
- 在 release 实现里两者可能恰好是同一地址，于是循环"看起来"正常结束。

## 回调与重入

- 除非 API 契约明确禁止，否则一律假设回调会修改正在被遍历的容器。
- 协程恢复点就是回调：在仿真调度器里，恢复一个协程可能使它立即注册新任务、触发事件、唤醒其他协程，从而在遍历中途改变队列（见 `register_ready()` / `event::notify_one()`）。
- 回调中追加元素可能触发扩容，使调用方仍然持有的元素引用失效。
- 回调中删除元素可能使调用方的迭代器失效，即使调用方自己没有可见的修改动作。
- 优先选择：延迟修改（先收集，后统一处理）、待处理队列、稳定 ID（索引/句柄）而不是裸迭代器，或换用具备所需不失效保证的容器。
- 对"边遍历边删除"的队列，可以用游标 + 水位线（`head_index` + erase watermark）把批量删除摊还到 O(1)，但必须保证游标语义清晰、水位线判断与删除范围一致。

## 回归测试清单

- 删除唯一的元素：这是同时使当前迭代器与旧 `end()` 失效的最短路径。
- 从多元素容器中删除最后一个元素。
- 连续两个元素都需要删除，验证"补位"元素会被重新检查。
- 不删除任何元素、以及删除所有元素。
- 断言**语义结果**而不只是"没有崩溃"：期望的元素各被处理且仅被处理一次、补位元素没有被跳过、最终容量/长度正确。
- 至少在一个能主动诊断迭代器误用的配置下运行（libstdc++ 的 `_GLIBCXX_DEBUG`/`_GLIBCXX_ASSERTIONS` 或 libc++ hardening），并在 `AGENTS.md` 允许的条件下记录实际使用的工具链与配置。

## 交付前检查

交付前执行 `verilator-utils-pre-delivery-check` skill（激活 `.venv` → 生成编译数据库 → clang-tidy → clang-format），本 skill 不重复其中内容。与本 skill 相关的额外要求：

- 静态分析抓不到全部迭代器误用，最终判据仍是"在带迭代器调试/加固的配置下跑通并断言语义结果"。
- 格式化会就地改写文件，之后要确认没有语义变化（尤其不要把 `iter = items.erase(iter)` 之类的关键语句改错），并保持 `git diff --check` 通过。

## 审查清单

- 修改容器之后，没有任何失效的迭代器、指针、引用、span 或缓存的 `end()` 被使用。
- 基于迭代器的删除循环用 `erase()` 的返回值接替。
- 无序删除使用索引或能在 `pop_back()` 后存活的句柄。
- 补位元素在 swap 删除后会被重新检查。
- 末元素与单元素路径被显式验证过。
- 持有元素引用期间没有执行可能扩容的操作。
- 测试在能主动诊断迭代器误用的配置下运行过。
