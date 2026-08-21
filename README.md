# verilator_utils

基于 doctest 与 Verilator 的 Verilog/SystemVerilog 测试激励框架。

## 简介

`verilator_utils` 是一个基于 C++26 的 Verilator 工具库，旨在为 Verilator C++ 测试激励提供一套现代化的接口与实用工具。项目以 C++20 模块组织核心代码，使用 C++ 协程表述测试任务与仿真调度，使用 doctest（以 C++20 模块形式接入）作为单元测试框架，使 RTL 的 C++ 测试激励与普通单元测试共用统一的断言、报告与命令行体验。

> 项目仍处于早期开发阶段，接口与功能在快速演进中，请以源码注释为准。

## 特性

- 基于协程的仿真调度器：任务挂起/恢复、仿真时间推进、时钟边沿等待、仿真超时与协作式取消
- 集成 doctest 单元测试框架，RTL 集成测试与框架单元测试使用同一套断言体系
- DUT 上下文封装：统一管理 `VerilatedContext`、DUT 实例与调度器，支持 VCD / FST / SAIF 波形记录
- 覆盖率收集（配合 Verilator `--coverage`），断言失败时自动记录随机种子便于回归复现
- 信号包装与数据格式：`bit_slice` / `vector_slice` / `format_wrapper`，支持二进制、十六进制、十进制、定点数、布尔等格式
- 常用激励辅助：时钟/复位生成、边沿等待与校验、仿真时间限制、并发任务池
- 同步原语：事件（event）、邮箱（mailbox）、信号量（semaphore）等
- 彩色输出支持，并适配 doctest 的 `--force-colors` / `--no-colors` 选项
- 断言失败时包含栈回溯信息

## 目录结构

```text
├── src/      # 框架核心代码（C++20 模块）
│             # assert / context / scheduler / task / utils / wrapper / verilator / internal
├── test/     # 测试代码：框架单元测试（doctest）与基于 rtl/ 的集成测试
├── rtl/      # 集成测试用的示例 SystemVerilog 模块（counter、FIFO、双口 RAM 等）
├── script/   # xmake 包定义（如以 C++20 模块形式使用的 doctest）
└── xmake.lua # 顶层构建脚本
```

## 构建

依赖：xmake、支持 C++20 模块与 C++26 的编译器、Verilator，以及 FST 波形支持所需的 zlib / lz4（可选）。

```bash
xmake        # 默认 release 构建
xmake test   # 运行测试
```

支持 `debug` / `release` / `releasedbg` 三种模式，可选配置项包括：

- `use_sanitizer`：启用地址/未定义行为消毒器
- `use_std_harden`：C++ 标准库加固
- `use_lto`：链接时优化
- `trace_support_fst`：FST 波形支持

## 依赖库

- [convert-cpp-std-headers-to-std-module](https://github.com/YexuanXiao/convert-cpp-std-headers-to-std-module): 提供一种简单的方式将头文件转化为模块
- [doctest_module](https://github.com/24bit-xjkp/doctest_module): 模块化的doctest单元测试框架
- [Verilator](https://github.com/verilator/verilator): Verilog/SystemVerilog仿真器，将HDL代码转化为C++代码
- [cpptrace](https://github.com/jeremy-rifkin/cpptrace): 栈回溯支持

## 许可证

[MIT](LICENSE)
