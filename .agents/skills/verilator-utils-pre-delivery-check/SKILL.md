---
name: verilator-utils-pre-delivery-check
description: 'Use when: finishing any C++ work in the verilator_utils repository and before reporting it as done — covers activating the Python environment for every xmake invocation, regenerating the compile database with --lsp=clangd, running clang-tidy static analysis and clang-format over the hand-written sources, interpreting the clean baseline, and handling third-party clang-tidy warnings by proposing a .clang-tidy change.'
argument-hint: 'No arguments needed；如为第三方库告警，请给出告警文本与文件行号'
---

# verilator_utils 交付前检查

改动 `src/*.cppm` 或 `test/*.cpp` 后，**在报告完成之前**必须执行本流程。这里是指定命令与判据的唯一出处，其他 skill 只引用本 skill，不重复命令细节。

## 1. 先激活 Python 环境

按 `AGENTS.md`：**运行任何 xmake 命令前先激活虚拟环境**，这样无需逐条判断命令是否依赖 Python。

```bash
uv sync --all-extra   # 首次或依赖变化时
source .venv/bin/activate
```

项目内有两条链路依赖 Python：Python 脚本规则（生成 SystemVerilog 源与激励数据、可视化）与 `script/scan_deps.py`（依赖扫描，生成编译数据库时会走到）。未激活环境时典型报错是 `ModuleNotFoundError: No module named 'depfinder'`。

## 2. 生成编译数据库

clang-tidy 依赖 `.vscode/compile_commands.json`，必须用下面这条命令生成：

```bash
xmake project -k compile_commands --lsp=clangd .vscode
```

- 一定要带 `--lsp=clangd`：它写入 clangd/clang-tidy 所需的完整编译参数（含 `-isystem`、`@*.requiresflags.txt` 等）。
- 文件为空或过期时，clang-tidy 会退化成 `Running without flags`，把模块导入报成 `expected template` / `unknown type name 'import'` 之类的**假错误**，并连带在第三方头文件上刷出大量**假告警**。
- **看到任何 clang-tidy 告警，先重跑本命令，再判断告警真假。**

## 3. 运行两条指定命令

```bash
xmake check clang.tidy --compdb=.vscode --configfile=.clang-tidy --quiet -f "src/*.cppm:test/*.cpp"
xmake format -af "src/*.cppm:test/*.cpp"
```

顺序：先 clang-tidy 后 format（format 会改写文件；若先 format，已分析的源码就不是最终内容）。

### 文件选择的语法

`-f` / `-af` 的取值是一组文件模式：

- **`:` 分隔多个模式**（xmake 用环境变量式的路径分隔符切分，Linux 上即 `:`）。上面两条等价于"`src/*.cppm` 一批 + `test/*.cpp` 一批"。
- **`|` 表示从该模式中排除**，不是"或"。排除项按"去掉匹配目录前缀后的相对路径"匹配，通常是文件名级模式：

  ```bash
  # 只检查 src/*.cppm，但排除 assert.cppm
  xmake check clang.tidy --compdb=.vscode --configfile=.clang-tidy --quiet -f "src/*.cppm|assert.cppm"
  ```

  排除项要写到对应模式内部（`|` 之前是包含模式，之后是排除模式）。需要排除多个时，对每个模式分别写 `|`。

- 这两组模式只覆盖手写源码，不含 `build/` 下的 Verilator 生成代码（生成文件不参与 clang-tidy 与格式化）。

### 当前基线

在本仓库当前配置（clang 工具链、debug、sanitizer + 标准库加固）下，第 3 节两条命令的预期结果是：

- `clang.tidy`：exit 0，**0 error、0 warning**；
- `format`：正常情况下不改动任何已格式化文件。

**0 error、0 warning 是基线**：任何新告警都需要解释或消除，不能默认"仓库本来就有告警"。

## 4. 收尾

```bash
git diff --check
git status --short
```

- `xmake format` 会**就地改写**文件。确认这些改动属于纯格式化，没有引入语义变化；如果有非格式化的意外改动，需要逐一说明。
- 只应留下本次任务相关的文件改动。
- 报告验证结果时写明实际使用的目标与配置，不要凭单一配置断言广泛的移植性或完整回归覆盖。

## 第三方库告警的处理约定

当 clang-tidy 报出来自第三方库（doctest、cpptrace、Verilator 头等）的告警时，按以下顺序处理，**不要**通过修改业务代码、加 `NOLINT` 或改格式来"消音"：

1. **先排除编译数据库问题**：重跑第 2 节的命令。第 3 节基线是干净的，多数"第三方告警"其实是编译数据库缺失造成的假象。
2. **确认是真实第三方告警后，提示用户修改 `.clang-tidy`，并给出可直接套用的方案**。方案至少包含：改哪里（`HeaderFilterRegex` 还是 `Checks`）、改动前后的确切文本、以及副作用。
3. 典型改法：

   ```yaml
   # 方案 A：收紧 HeaderFilterRegex（当前值 '^(?!.*\.(h)$).*' 只排除 .h，不排除 .hpp）
   # 改前
   HeaderFilterRegex: '^(?!.*\.(h)$).*'
   # 改后（同时排除 xmake 包目录下的第三方头）
   HeaderFilterRegex: '^(?!.*\.(h|hpp)$)(?!.*/\.xmake/).*'
   ```

   ```yaml
   # 方案 B：只关闭具体检查项（Checks 按顺序生效，'-' 开头的关闭项必须放在启用项之后）
   Checks: >
     ...
     -modernize-type-traits,
   ```

   方案 A 的副作用：更严格的 `HeaderFilterRegex` 会让工程自有头文件（`src/*.hpp`）也不再被检查，需确认是否可接受。

4. 任何 `.clang-tidy` 改动都要重跑 `xmake check clang.tidy`，确认目标告警消失且没有连带关掉自有代码的检查，并在报告中写明改了什么、为什么改。

## 检查清单

- 运行 xmake 前已 `source .venv/bin/activate`。
- `.vscode/compile_commands.json` 是本次用 `--lsp=clangd` 生成的。
- 两条指定命令均已运行，且 clang-tidy 输出仍是 0 error / 0 warning（或有明确解释）。
- `xmake format` 没有引入语义变化，`git diff --check` 通过。
- 第三方告警已按上面的约定处理（先排查编译数据库；确认为真实告警时给出 `.clang-tidy` 修改方案，而不是消音）。
- 报告中写明了实际使用的目标与配置。
