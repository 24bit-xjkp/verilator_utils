# xmake skills

本目录是 **xmake 用法的权威出处**：命令、目标、选项、规则、插件、包管理与工具链怎么用，都以这里的 skill 为准。

## 使用约定

- 需要 xmake 的某项用法时，按 **skill 优先** 的顺序取值：先加载对应主题的 skill（见下方索引）；skill 未覆盖或与实际行为不一致时，用**网络搜索**（xmake 官方文档、issue、社区讨论）作为备选，`xmake <command> --help` 属于同一类的就地形式；仍不明确时再**阅读 xmake 源码或内部实现**。
- 不要把读源码当作首选：skill 已写明的命令与选项，不必为"再确认一下"去通读 xmake 源码或 `~/.xmake` 缓存。
- skill 与实际行为不一致时，以实际行为为准，并在报告中说明差异。
- 运行目标（测试、示例、代码生成脚本）时使用 `xmake run` / `xmake test` 等入口，不要直接执行构建目录下的产物：入口会先构建目标，并应用目标或用例声明的 `runargs`、`runenvs`、包库路径与 rpath（入口无法表达需求时除外，此时在报告中写明原因）。详见 [cli/xmake-commands](./cli/xmake-commands/) 与 [testing/xmake-tests](./testing/xmake-tests/)。
- 本项目还把常用外部工具封装成了自定义任务（`script/task.lua`）：**直接调用外部工具前先看这些任务**，见 `AGENTS.md` 的「先看仓库封装的自定义任务」。

## 主题索引

| 分组 | 内容 |
| --- | --- |
| [basics](./basics/) | 新建工程、最小 `xmake.lua`、惯用风格 |
| [project-config](./project-config/) | `target` / `option` / `package` / `rule` / `policy` 等声明式 API |
| [cli](./cli/) | 命令行子命令、运行与项目信息查询、`xmake project -k …` 项目文件生成 |
| [testing](./testing/) | `add_tests` 与 `xmake test` |
| [scripting](./scripting/) | `xmake.lua` 中的 Lua 脚本域、模块、插件、addon 与异步任务 |
| [packages](./packages/) | 包管理：`xrepo`、私有仓库、配方测试与调试 |
| [toolchains](./toolchains/) | 编译器切换、交叉编译、C++ 模块支持 |
| [performance](./performance/) | 构建加速：并行、缓存与 LTO |

## 本工作区未收录的主题

以下主题的上游 skill 未随本工作区提供：模板与脚手架（`xmake create` 之外的部分）、项目文件生成器的深入用法（`xmake project -k …` 的基础命令见 [cli/xmake-commands](./cli/xmake-commands/)）、驱动第三方构建系统（CMake / Autotools / Meson / Ninja / Bazel）、下载加速 / 代理、分布式编译、远程编译，以及 xmake 自身测试套件。
需要时用**网络搜索**（xmake 官方文档、issue、社区）或 `xmake <command> --help`，仍不明确时再**阅读 xmake 源码**；本仓库已有覆盖的部分见 [basics/xmake-basics](./basics/xmake-basics/)、[cli/xmake-commands](./cli/xmake-commands/) 与 [scripting/xmake-plugins](./scripting/xmake-plugins/)。
