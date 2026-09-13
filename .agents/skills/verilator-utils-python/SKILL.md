---
name: verilator-utils-python
description: 'Use when: 开发、修改、审查或测试 verilator_utils_rtl Python 包（rtl/verilator_utils_rtl/）、rtl/ 下的 Python 生成器脚本（如 cic_filter.py）、script/scan_deps.py 以及 test/verilator_utils_rtl/ 下的 pytest 测试。覆盖包结构与开发约定、pytest 并行运行、mypy 并行静态检查与 # type: ignore[code] 误报处理、black 与 isort 并行格式化，以及这四类工具的验收基线。'
argument-hint: '描述要开发或测试的 Python 模块/行为；如为 mypy 报错请附告警文本与错误码'
---

# verilator_utils_rtl Python 包开发与测试

本 skill 是 Python 侧（`rtl/verilator_utils_rtl`、`rtl/*.py` 生成器脚本、`test/verilator_utils_rtl`）**指定命令与验收判据的唯一出处**。

## 适用场景

- 在 `rtl/verilator_utils_rtl/` 中新增或修改模块、函数、类型标注。
- 编写/重构 `rtl/` 下的 Python 生成器脚本（由 xmake 的 `python` 规则调用，生成 SystemVerilog 源与激励数据、绘制图表）。
- 在 `test/verilator_utils_rtl/` 下新增或重构 pytest 测试。
- 交付前跑测试、静态检查与格式化，并判断报告是否达标。

## 1. 环境准备（每次先做）

所有命令都在**仓库根目录**执行；先激活虚拟环境（`AGENTS.md` 的要求）：

```bash
uv sync --all-extra      # 仅首次或依赖变化时
source .venv/bin/activate
```

- 包以 editable 方式安装，修改 `rtl/verilator_utils_rtl/*.py` 后**无需重新安装**，import 立即生效。
- Python 版本为 3.14（`requires-python >= 3.12`），可直接使用 PEP 695 泛型语法等新特性（`dsp.py` 的 `def padding[T: np.generic]` 即为例）。
- `test_visualize.py` 等会 import matplotlib。本机 `~/.config/matplotlib` 与 `~/.cache/matplotlib` 均可写时不会出现缓存目录警告，也不会在 `/tmp` 留下 `matplotlib-*`。当这两个目录不可写时，matplotlib 才会打印一条 “is not a writable directory / created a temporary cache directory” 警告并退回 `/tmp/matplotlib-*`——**不影响测试结果**；要在那种环境消除，必须在**解释器启动前**导出 `MPLCONFIGDIR`：

  ```bash
  MPLCONFIGDIR="$(mktemp -d)" pytest -n auto -q
  ```

  `MPLCONFIGDIR`/`MPLBACKEND` 这类环境变量都在 `import matplotlib` 期间就被读取，写在 `import matplotlib.pyplot` 之后不再生效（实测在 import 之后设 `MPLBACKEND=pdf`，`plt.get_backend()` 仍返回 `tkagg`），所以要用它们必须在解释器启动前导出。`conftest.py` 用 `matplotlib.use("Agg")` 在 `import matplotlib.pyplot` **之前**固定 Agg，实测会话内 `plt.get_backend()` 为 `Agg`。该文件因此把 pyplot 的导入单独放在 `matplotlib.use(...)` 之后，**不要把它合并回上面的导入块**（isort 实测会原样保留这个布局）。

## 2. 代码布局与职责

| 路径 | 职责 |
| --- | --- |
| `rtl/verilator_utils_rtl/common.py` | `basic_config`（全局配置）、`find_tool`、`is_in_notebook`、`logger` |
| `rtl/verilator_utils_rtl/dsp.py` | 定标/量化（`affine_scale`、`linear_scale`、`width_cast`）、范围检查（`in_range`、`check_range`）、`nco`、`padding`、`clog2`、`linear2dB` |
| `rtl/verilator_utils_rtl/sv.py` | SystemVerilog 代码生成：`system_verilog_context`、`system_verilog` 上下文管理器、`vector`/`parameter`/`localparam`/`assign` 节点 |
| `rtl/verilator_utils_rtl/visualize.py` | matplotlib 绘图：`create_figures`/`create_subplots`/`figure_env`、`visualize_waveform`、`visualize_spectrogram*`、`process_figure`（线程池 + svgo 优化） |
| `rtl/verilator_utils_rtl/__init__.py` | 公共 API 汇总：`from .x import ...` + `__all__` + `__version__` |
| `rtl/cic_filter.py` | 生成器脚本样例：被 `rtl/xmake.lua` 的 `python` 规则以 `-o <data_dir> -g <gen_src_dir> [--visualize]` 调用 |
| `script/scan_deps.py` | xmake `python` 规则在运行脚本前调用的依赖扫描器（第三方 `depfinder`） |
| `test/verilator_utils_rtl/` | pytest 测试：`conftest.py` + 每个模块一个 `test_<module>.py` |

## 3. 开发约定

- **类型标注是硬要求**：`pyproject.toml` 的 `[tool.mypy] files` 覆盖 `script/*.py`、`rtl/**/*.py`、`test/**/*.py`（当前共 12 个文件），所有新增代码都在 mypy 检查范围内。数组用 `numpy.typing.NDArray[...]`，常量用 `typing.Final`，重载用 `@overload`（`dsp.py`、`visualize.py` 是既有范例）。
- **全局配置集中在 `basic_config`**：新增可配置项时同时更新类属性默认值、`register()` 的 argparse 选项与 `parse()` 的赋值，并保证 `-j 0` 这类约定语义（0 表示沿用默认并发数）。
- **`find_tool` / `is_in_notebook` 带 `@cache`**：测试中改环境或 `shutil.which` 后必须 `cache_clear()`，否则会读到上一次的结果。
- **输出走 `logger`**：`logger.info/warning/note/success` 在 `basic_config.quiet` 为真时应静默；不要直接 `print`。
- **公共 API 必须导出**：新增的公共名字要加进 `__init__.py` 的 import 与 `__all__`（按模块分组、组内字典序），否则生成器脚本无法通过 `import verilator_utils_rtl as rtl` 使用。
- **生成器脚本契约**：`rtl/*.py` 由 xmake `python` 规则执行，需保持 `config = rtl.basic_config` + `config.register/parse` 的 CLI 形态，并把产物写到 `-o`/`-g` 指定的目录；`rtl/xmake.lua` 的 `gen_src` 必须与实际生成的文件名一致，否则构建会缺文件。
- **前置条件用 `assert` + 中文消息**：既有代码统一这样写（如 `assert cls.jobs > 0, "并发数必须是正数"`），测试用 `pytest.raises(AssertionError, match="并发数必须是正数")` 断言消息。注意这意味着**测试依赖未加 `-O` 的解释器**，不要用 `python -O` 跑测试。
- **docstring 用中文 Google 风格**（`Args:` / `Returns:`），模块顶部一行中文说明；`sv.py`、`visualize.py` 的公开函数都有完整 docstring，新增函数照此办理。
- 不要把生成物写进仓库：输出目录一律来自 `basic_config.data_output_dir` / `source_output_dir`，测试里用 `tmp_path`。

## 4. 必做命令序列

改完 Python 代码后**按顺序**执行下面四步，全部通过才算完成。

### 4.1 运行测试（并行）

```bash
pytest -n auto -q
```

- `pyproject.toml` 已设 `testpaths = ["test/verilator_utils_rtl"]`，根目录直接 `pytest` 即可，无需给路径。
- `-n auto` 由 `pytest-xdist` 提供（在 dev 依赖组中）。`auto` 按物理核数起 worker；也可用 `-n "$(nproc)"`。
- 只跑一部分：`pytest -n auto -q test/verilator_utils_rtl/test_dsp.py -k nco`。
- 需要 `--pdb`、`capsys` 交互调试或单步定位失败时改为串行（`-n 0` 或省略 `-n`），并加 `-x` 快速失败。
- 需要按文件聚集用例（某文件有昂贵的模块级夹具）时用 `--dist loadfile`。

### 4.2 静态检查（并行；要求零告警）

```bash
mypy -n "$(nproc)"
```

- **`-n N`（N > 0）是 mypy 的并行开关**：起 N 个 worker 进程并行做类型检查。默认 `num_workers = 0`，即**默认是串行的**，必须显式打开。也可用环境变量 `MYPY_NUM_WORKERS`。并行度不写进配置，一律按当前机器的核数取。
- `-n` **不接受 `auto`**（`mypy -n auto` 会报 `invalid int value: 'auto'`），必须是整数；`-n 0` 表示关闭并行。
- 并行模式的前提：增量缓存开启（不要配 `--no-incremental` / `cache_dir = /dev/null`），且不能使用 `--report-*` 报告目录（mypy 会直接报错）。开启后 mypy 会强制 `--native-parser`。
- 任何新报错都要处理，不能默认“仓库本来就有报错”。若确认是第三方 stub（numpy / matplotlib 重载）导致的误报，按第 5 节处理。
- 只检查单个文件时（`mypy test/.../test_common.py`）会额外报 `Skipping analyzing "verilator_utils_rtl.common": module is installed, but missing library stubs or py.typed marker [import-untyped]`：单文件检查不会把 `rtl/` 源码一起纳入构建，editable 安装的包又没有 `py.typed`。这是既有现象（未改动的测试文件同样如此），**以不带文件参数的整库命令为准**，不要为此加 `# type: ignore`。

### 4.3 格式化（并行；black 先，isort 后）

```bash
black --workers "$(nproc)" .
isort -j "$(nproc)" .
```

- **顺序固定为 black → isort**（本项目的约定）。`isort` 使用 `profile = "black"`，因此其输出仍是 black 风格；第 4.4 步用 `black --check` 证明这一点。
- `black` 的 `--workers` 控制进程池大小，默认已是 CPU 核数，显式传入以保证不同机器上一致。
- `isort` 需要 `-j`：正数表示并行进程数，负数（如 `-j -1`）表示按 CPU 核数。
- **不要**在命令行重复传 `-l/--line-length`、`--profile`、`-t`：配置已在 `pyproject.toml`。命令行传入会与配置不一致。
- 两个工具都只处理 `.py`（black 的 `include = "(\.pyi?)$"`），`isort` 会自动跳过 `.git`、`.venv`、`build`、`.mypy_cache`（输出里的 `Skipped 4 files` 即指这些目录，不是漏检）。

### 4.4 收尾复检

```bash
black --check .          # 确认 isort 没有破坏 black 风格
git diff --check         # 空白字符错误检查（AGENTS.md 要求）
git status --short       # 只应留下本次任务相关的改动
```

若 4.3 改写了文件，重跑 4.1 与 4.2（格式化后的文件才是被检查的内容）。

## 5. mypy 误报的处理：# type: ignore[code]

规则：**只允许带具体错误码的 `# type: ignore[code]`，禁止裸 `# type: ignore`**（4.2 节 `pyproject.toml` 里的严格配置会强制这一点）。

1. 从 mypy 输出行末的中括号里取出错误码，例如：

   ```text
   rtl/verilator_utils_rtl/visualize.py:187: error: No overload variant ... [call-overload]
   ```

2. 在同一行末写带码的忽略，并保留原代码不变：

   ```python
   subfigures = fig.subfigures(rows, cols, squeeze, **subfigures_kwargs)  # type: ignore[call-overload]
   ```

3. 用 4.2 的命令复跑，确认 `Success`。码值不对时 mypy 会给出可直接照抄的提示，例如：

   ```text
   probe.py:3: error: Incompatible types in assignment ... [assignment]
   probe.py:3: note: Error code "assignment" not covered by "type: ignore[arg-type]" comment
   ```

   裸忽略则报 `error: "type: ignore" comment without error code (consider "type: ignore[assignment]" instead) [ignore-without-code]`。

既有先例（照此风格书写，不要另创写法）：

- `rtl/verilator_utils_rtl/visualize.py:187` — `matplotlib` 的 `subfigures` 重载
- `test/verilator_utils_rtl/test_dsp.py:100`、`test_dsp.py:398` — 故意传错类型以验证 `AssertionError` 的用例
- `test/verilator_utils_rtl/test_sv.py:282` — `ctx.assign` 的重载

**不要**用忽略注释掩盖真实的类型问题：如果报错说明标注与实现不符，先修标注/实现。也不要为了消音而给签名加上 `Any`、把返回类型放宽或到处 `cast`。反过来说，测试里“故意调用错类型”的用例（如上面 `test_dsp.py`）属于真误报场景，应当用带码忽略。

## 6. 测试并行安全（`-n auto` 的前提）

`conftest.py` 的 `_restore_global_state` 自动夹具在每个用例后恢复 `basic_config` 的类属性、清空 `find_tool`/`is_in_notebook` 缓存并 `plt.close("all")`。新测试必须遵守同样的约束：

- 不依赖用例执行顺序，不共享可变全局状态；改 `basic_config`、`sys.modules`、环境变量用 `monkeypatch` 或夹具恢复。
- 文件读写一律用 `tmp_path`，不要写仓库目录或固定路径的 `/tmp/xxx`（并行 worker 会互相覆盖）。
- matplotlib 不要在测试里 `plt.show()` 或依赖交互环境：`conftest.py` 已在导入 pyplot 前用 `matplotlib.use("Agg")` 把后端固定为 Agg（实测会话内 `plt.get_backend()` 为 `Agg`，见第 1 节）。
- 断言浮点用 `pytest.approx`，异常用 `pytest.raises(..., match=...)`，可选依赖用 `pytest.importorskip`，参数化用 `@pytest.mark.parametrize`（`test_dsp.py`、`test_visualize.py` 是既有范例）。
- 测试按被测对象分组为 `class TestXxx`，用例名写清行为（如 `test_parse_jobs_zero_keeps_default`），模块首行写 `"""verilator_utils_rtl.<module>模块测试"""`。

## 7. 检查清单

- [ ] 命令都在仓库根目录、且已 `source .venv/bin/activate`。
- [ ] `pytest -n auto -q` 全绿；新增行为有能真正失败的用例，且用例并行安全（见第 7 节）。
- [ ] `mypy -n "$(nproc)"` 输出 `Success`；所有忽略都带具体错误码。
- [ ] `black --workers "$(nproc)" .` 与 `isort -j "$(nproc)" .` 已按此顺序运行，`black --check .` 通过。
- [ ] `git diff --check` 通过，`git status --short` 只有本次相关改动。
- [ ] 报告里写明实际使用的命令、目标与配置，不要凭单一配置断言广泛的可移植性或完整回归覆盖。
