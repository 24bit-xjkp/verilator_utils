# AGENTS.md

本文件面向在本仓库（`verilator_utils`）中工作的 AI 代理与开发者，仅定义项目最基本信息与规则。涉及具体领域时请**先加载** `.agents/skills/` 下对应 skill 再动手：工具用法以 skill 优先，网络搜索与读源码只是备选（见下文"skill 优先"）。

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
uv sync --all-extras                              # 创建/更新虚拟环境 .venv
source .venv/bin/activate                        # 推荐：先激活虚拟环境，再运行 xmake 命令
```

xmake 的目标注册、命令与配置项细节由专门的 xmake skill 提供（见下文"skill 优先"）。
本项目目前**只能使用 clang 工具链**构建与测试：gcc 在编译带有复杂模块的代码时会产生编译器内部错误（ICE）。
**不要尝试用 gcc 配置、构建或做"可移植性验证"**；只有用户明确要求排查 gcc 问题时才例外。

## skill 优先：工具用法以 skill 为准

`.agents/skills/` 是"工具与领域怎么用"的权威出处。需要知道某个工具的命令、选项、配置项或工作流时，按顺序取用三类来源：**skill 优先，网络搜索与读源码是备选**。

- **首选 skill**：加载本领域对应的 skill 并按其说明执行。本仓库常用领域与对应 skill：

| 领域 | 先加载 |
| --- | --- |
| xmake 的命令、目标、选项、测试注册、插件 / addon | `.agents/skills/` 下对应主题的 skill（`xmake-*`，如 `xmake-commands`、`xmake-targets`、`xmake-tests`、`xmake-scripting`；包与仓库管理为 `xrepo-cli` / `xrepo-env`） |
| C++ 单元测试与 RTL 集成测试写法 | `verilator-utils-unit-testing` |
| 交付前静态检查与格式化 | `verilator-utils-pre-delivery-check` |
| 代码审查 | `verilator-utils-code-review` |
| Python 包与生成器脚本 | `verilator-utils-python` |
| 协程 lambda 生命周期 | `cpp-coroutine-lambda-safety` |
| 容器迭代器失效 | `cpp-container-iterator-invalidation` |

- **备选一：网络搜索**。skill 未覆盖，或 skill 与项目/环境的实际行为不一致时，先查官方文档、issue 与社区讨论；就地可用的官方帮助（如 `xmake <command> --help`）属于同一类的快速形式。
- **备选二：读源码**。网络搜索仍不明确时，再阅读该工具的源码或内部实现。读源码只用于理解本仓库自身的实现，或排查工具自身的行为异常。
- 不要跳过 skill 直接进入备选方法：skill 已写明的命令与选项，不必为"再确认一下"去通读源码或满网搜索。
- 通过备选方法得到的结论，在报告中注明来源（文档链接，或源码文件与行号），并考虑把结论补进 skill。
- skill 与实际行为不一致时，以实际行为为准，并在报告中说明差异。

## 运行约定：走工具入口，不直接执行产物

- 运行测试、示例、基准与代码生成脚本时，使用工具链提供的运行入口：本项目为 `xmake run`、`xmake test`；同类场景如 `cargo run` / `cargo test`、`uv run`、`ctest`。
- **不要**直接执行构建产物的路径（如 `build/` 下的可执行文件）。入口会先构建目标，并应用该目标或用例声明的运行参数与环境（xmake 的 `runargs` / `runenvs`、包库路径与 rpath），运行目录就是目标产物目录（生成的数据也在那里）；绕过入口会丢掉这些配置，跑出来的不是项目约定的那次运行，结果不能作为验证证据。
- 需要看详细输出、传框架参数或挂调试器时，同样用入口表达（如 `xmake test -v`、`xmake run <target> <参数>`），而不是换成产物路径。
- 只有入口无法表达需求时（例如用外部调试器直接附加进程、精确复现一次裸执行）才直接调用产物，并在报告中写明原因与命令。

### 先看仓库封装的自定义任务（`script/task.lua`）

**在直接调用外部工具之前，先确认仓库是否已经把它封装成任务**：本项目集中在 `script/task.lua`，通过 `xmake <task>` 调用。

- 项目封装的任务负责查找工具、拼装参数、决定工作目录与产物路径；直接调用底层工具容易漏掉这些约定。
- 同类场景同理：先找项目封装的任务或脚本入口，再考虑直接调用外部工具；确需直接调用时，在报告中写明原因与命令。
- 任务清单与选项以 `script/task.lua` 的 `task(...)` / `set_menu` 为准，`xmake <task> --help` 可打印用法。

## 文档与 skill 写作：不写死单一工具，按场景类别陈述规则

编写或修改本文件与 `.agents/skills/` 下的 skill 时：

- 规则按**场景类别**陈述（构建系统入口、包管理器、测试框架、代码生成、静态检查……），当前使用的具体工具与命令只作为该类别的**示例 / 当前实现**，不要把某一条命令写成规则本身。
- 目标是"换一个同类工具后规则仍然成立"：写"通过构建系统的运行入口运行目标（本项目当前为 `xmake run` / `xmake test`）"，而不是"运行目标必须用 `xmake run`"。
- 只有当规则确实只对该工具成立时，才把工具名写进规则，并标明这是该工具的专属约定。
- 给出命令时说明它属于哪一类入口、为什么需要它，而不是只留一串命令。

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
