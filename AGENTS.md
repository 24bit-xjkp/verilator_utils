# AGENTS.md

本文件面向在本仓库（`verilator_utils`）中工作的 AI 代理与开发者，仅定义项目最基本信息与规则。涉及具体领域时请先加载 `.agents/skills/` 下对应 skill。

## 项目概览

`verilator_utils` 是一个基于 **C++26** 的 Verilator 工具库，为 Verilator C++ 测试激励提供现代化接口与工具：

- 核心代码以 **C++20 模块** 组织（`src/*.cppm`）
- 使用 **C++ 协程** 表述测试任务与仿真调度
- 使用 **doctest**（C++20 模块形式）作为单元测试框架，RTL 集成测试与框架单元测试共用同一套断言体系
- 提供 DUT 上下文封装、波形记录（VCD/FST/SAIF）、信号包装（`bit_slice` / `vector_slice` / `format_wrapper`）等

> 项目仍处于早期开发阶段，接口快速演进，**以源码注释为准**。

## 目录结构与任务优先级

```text
├── src/      # 框架核心代码（C++20 模块：assert / context / scheduler / task / utils / wrapper / verilator / internal）
├── test/     # 测试代码：框架单元测试（doctest）与基于 rtl/ 的 RTL 集成测试
├── rtl/      # 集成测试用的示例 SystemVerilog 模块（经 Verilator 编译后作为 DUT）
├── script/   # xmake 包定义（如以 C++20 模块形式使用的 doctest）
└── xmake.lua # 顶层构建脚本
```

**任务优先级**：以功能实现、缺陷修复、补充测试或生成文档为主；未明确指定时，优先修改 `src/` 与 `test/`，必要时更新 `rtl/`。

## 构建与测试

依赖：xmake、支持 C++20 模块与 C++26 的编译器、Verilator；FST 波形支持需要 zlib / lz4（可选）。

```bash
xmake                                   # 默认 release 构建
xmake test                              # 运行测试
xmake run unit_test                     # 直接运行单元测试程序
```

支持 `debug` / `release` / `releasedbg` 三种模式；可选配置：`use_sanitizer`、`use_std_harden`、`use_lto`、`trace_support_fst`。

测试目标按测试文件 basename 注册，doctest 套件命名为 `verilator_utils/<basename>`：

```text
test/utils.cpp  →  xmake test unit_test/utils   （套件 verilator_utils/utils）
RTL 集成测试    →  xmake test unit_test_rtl_<名称>/rtl
```

基本规则：

- 运行单个测试：`xmake test unit_test/<basename>`，详细输出加 `-v`
- 修改 C++ 模块（BMI/目标文件可能过期）后重建再测：`xmake test -r unit_test/<basename>`
- 源码编辑结束后运行 `git diff --check`
- 报告验证结果时注明实际使用的目标与配置，不要仅凭单一配置断言广泛的移植性或完整回归覆盖

## 基本代码约定

- `test/main.cpp` 定义 `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`；测试文件 `#include <doctest.h>` 即可，**不得再定义另一个 doctest main**
- 全局命名使用 `::`；导入使用 C++ 模块（`import verilator_utils; import std;`）
- 在模块接口文件（`src/*.cppm`）中，`using namespace` 必须保持在块作用域或TU-local作用域（测试等内部文件不受此限）
- 框架运行时校验不得依赖 doctest 断言（`REQUIRE_*` 等），以免把生产行为耦合到测试框架
