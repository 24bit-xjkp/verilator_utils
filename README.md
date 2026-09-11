# verilator_utils

基于 doctest 与 Verilator 的 Verilog/SystemVerilog 测试激励框架。

## 简介

`verilator_utils` 是一个基于 C++26 的 Verilator 工具库，旨在为 Verilator C++ 测试激励提供一套现代化的接口与实用工具。
项目以 C++20 模块组织核心代码，使用 C++ 协程表述测试任务与仿真调度，使用 doctest（以 C++20 模块形式接入）作为单元测试框架，
使 RTL 的 C++ 测试激励与普通单元测试共用统一的断言、报告与命令行体验。

项目还提供 Python 包 `verilator_utils_rtl`：由 Python 脚本生成测试所需的 SystemVerilog 源文件与激励数据，并对仿真结果进行可视化。因此**运行测试需要可用的 Python 环境**。

> 项目仍处于早期开发阶段，接口与功能在快速演进中，请以源码注释为准。

## 特性

- 基于协程的仿真调度器：任务挂起/恢复、仿真时间推进、时钟边沿等待、仿真超时与协作式取消
- 集成 doctest 单元测试框架，RTL 集成测试与框架单元测试使用同一套断言体系
- DUT 上下文封装：统一管理 `VerilatedContext`、DUT 实例与调度器，支持 VCD / FST / SAIF 波形记录
- 覆盖率收集（配合 Verilator `--coverage`），断言失败时自动记录随机种子便于回归复现
- 信号包装与数据格式：`bit_slice` / `vector_slice` / `format_wrapper`，支持二进制、十六进制、十进制、定点数、布尔等格式
- 常用激励辅助：时钟/复位生成、边沿等待与校验、仿真时间限制、并发任务池
- 同步原语：事件（event）、邮箱（mailbox）、信号量（semaphore）等
- Python 模块 `verilator_utils_rtl`：生成 SystemVerilog 测试源文件与激励数据，绘制波形、频谱等可视化图表
- 框架单元测试统一通过 `unit_test` 模块（`test/common.cpp`）获得公共测试环境，并适配不同 Verilator 版本的内部接口
- 彩色输出支持，并适配 doctest 的 `--force-colors` / `--no-colors` 选项
- 断言失败时包含栈回溯信息

## 目录结构

```text
├── src/      # 框架核心代码（C++20 模块）
│             # assert / context / scheduler / task / utils / wrapper / verilator / internal
├── test/     # 测试代码：框架单元测试（doctest）与基于 rtl/ 的集成测试
│             # verilator_utils_rtl/ 为 Python 包自身的 pytest 单元测试
├── rtl/      # 集成测试用的示例 SystemVerilog 模块（counter、FIFO、双口 RAM 等）
│             # verilator_utils_rtl/ 为 Python 包：生成源文件、激励数据与可视化图表
├── script/   # xmake 包定义、构建脚本与 Python 依赖扫描脚本
├── pyproject.toml # Python 工程与 uv 配置
└── xmake.lua # 顶层构建脚本
```

## 构建

依赖：xmake、支持 C++20 模块与 C++26 的编译器、Verilator，以及 FST 波形支持所需的 zlib / lz4（可选）。

运行测试还需要 Python 环境（生成测试源文件与可视化）。Python 环境由 [uv](https://docs.astral.sh/uv/) 管理：

```bash
uv sync --extra build-verilator --extra complete # 创建/更新虚拟环境 .venv
source .venv/bin/activate                        # 推荐：先激活虚拟环境
xmake                                            # 默认 release 构建（只编译 C++，不依赖 Python）
xmake test                                       # 运行测试（需要 Python 环境）
```

- `build-verilator` 可选包组提供从源码构建 Verilator 所需的 pip / setuptools / wheel，`complete` 可选包组提供命令行补全（argcomplete）
- 系统中没有 verilator 包时，`xmake config` 会从源码编译 Verilator，此时也需要 Python 环境：系统已有 python 可直接构建，否则请使用本项目的虚拟环境（需安装 `build-verilator` 可选包组）
- `xmake build` 只涉及 C++ 代码，无需 Python 环境；建议在运行 xmake 命令前先激活虚拟环境，这样无需判断某条命令是否依赖 Python

Python 包自身的单元测试由 pytest 驱动：

```bash
uv run pytest
```

支持 `debug` / `release` / `releasedbg` 三种模式，可选配置项通过 `xmake f --<选项>=y|n` 设置：

- `use_sanitizer`：启用地址/未定义行为消毒器
- `use_std_harden`：C++ 标准库加固
- `use_lto`：链接时优化
- `trace_support_fst`：FST 波形支持，需要 zlib / lz4
- `with_main`：构建单元测试主程序 (doctest 入口，`verilator_utils_main` 目标)
- `enable_test`：启用单元测试与 RTL 集成测试目标；关闭后不再构建测试目标，也就不需要 Python 环境
- `visualize`：允许 Python 脚本输出可视化图表

仅支持 64 位平台与 `static` / `shared` 两种目标类型。

## 许可证

[MIT](LICENSE)
