# AGENTS.md

本文件面向在本仓库（`verilator_utils`）中工作的 AI 代理与开发者，仅定义项目最基本信息与规则。涉及具体领域时请先加载 `.agents/skills/` 下对应 skill。

## 项目概览

`verilator_utils` 是一个基于 **C++26** 的 Verilator 工具库，为 Verilator C++ 测试激励提供现代化接口与工具：

- 核心代码以 **C++20 模块** 组织（`src/*.cppm`）
- 使用 **C++ 协程** 表述测试任务与仿真调度
- 使用 **doctest**（C++20 模块形式）作为单元测试框架，RTL 集成测试与框架单元测试共用同一套断言体系
- 提供 DUT 上下文封装、波形记录（VCD/FST/SAIF）、信号包装（`bit_slice` / `vector_slice` / `format_wrapper`）等
- 配套 Python 包 **`verilator_utils_rtl`**（`rtl/verilator_utils_rtl/`）：生成 SystemVerilog 测试源文件与激励数据，并绘制可视化图表

> 项目仍处于早期开发阶段，接口快速演进，**以源码注释为准**。

## 目录结构与任务优先级

```text
├── src/      # 框架核心代码（C++20 模块：assert / context / scheduler / task / utils / wrapper / verilator / internal）
├── test/     # C++ 测试：框架单元测试与基于 rtl/ 的 RTL 集成测试；verilator_utils_rtl/ 为 Python 单元测试
├── rtl/      # 集成测试用的示例 SystemVerilog 模块（经 Verilator 编译后作为 DUT）；verilator_utils_rtl/ 为 Python 包
├── script/   # xmake 包定义与构建脚本（Lua），以及 Python 依赖扫描脚本
├── pyproject.toml # Python 工程与 uv 配置
└── xmake.lua # 顶层构建脚本
```

**任务优先级**：以功能实现、缺陷修复、补充测试或生成文档为主；未明确指定时，优先修改 `src/` 与 `test/`，必要时更新 `rtl/`。

## 环境与构建

依赖：xmake、支持 C++20 模块与 C++26 的编译器、Verilator；FST 波形支持需要 zlib / lz4（可选）。

测试需要 Python 环境（生成 RTL 测试源文件与可视化图表），由 **uv** 管理：

```bash
uv sync --all-extra                              # 创建/更新虚拟环境 .venv
source .venv/bin/activate                        # 推荐：先激活虚拟环境，再运行 xmake 命令
```

xmake 的目标注册、命令与配置项细节由专门的 xmake skill 提供。

## 基本代码约定

- 框架单元测试通过 `import unit_test;` 引入公共测试环境（由 `test/common.cpp` 定义，含不同 Verilator 版本内部接口的适配），
  不再直接 `import verilator_utils.full;`
- doctest main 位于 `src/main.cpp`；测试文件 `#include <doctest_macros.hpp>` 即可，**不得再定义另一个 doctest main**
- doctest 套件命名：框架单元测试为 `verilator_utils/<basename>`，RTL 集成测试为对应模块名
- 全局命名使用 `::`；导入使用 C++ 模块（`import verilator_utils; import std;`）
- 在模块接口文件（`src/*.cppm`）中，`using namespace` 必须保持在块作用域或TU-local作用域（测试等内部文件不受此限）
- 框架运行时校验不得依赖 doctest 断言（`REQUIRE_*` 等），以免把生产行为耦合到测试框架
- 源码编辑结束后运行 `git diff --check`
- 报告验证结果时注明实际使用的目标与配置，不要仅凭单一配置断言广泛的移植性或完整回归覆盖
- 若skills与项目和环境的实际情况存在差异，请在报告结果时提及
