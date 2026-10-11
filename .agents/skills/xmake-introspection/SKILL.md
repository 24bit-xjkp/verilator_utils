---
name: xmake-introspection
description: Use when you need to inspect an Xmake project programmatically instead of guessing — listing targets, reading a target's real sourcefiles/deps/packages/flags, exporting the target dependency graph as JSON or DOT, dumping the package dependency graph with `xrepo info --depgraph`, or querying any project data from a Lua script. Prefer this over reading xmake.lua by hand.
---

# Project Introspection

`xmake.lua` is a script, not a manifest: values can come from options, packages, rules,
`on_config` hooks and the toolchain. Reading the file tells you what the *author wrote*,
not what the build *resolves to*. Ask xmake instead.

## Show project information

```bash
xmake show                      # project name, version, toolchains, targets ...
xmake show -t hello             # everything resolved for one target
xmake show -l targets           # list target names
xmake show -l toolchains        # what toolchains this xmake knows
xmake show -l rules             # builtin + project rules
xmake show -l policies          # policy names and their defaults
xmake show -l packages          # required packages of this project
xmake show -l envs              # the environment variables xmake reads
```

## Machine-readable output

Any `show` output can be emitted as JSON, which is what you want when a script or an agent
consumes it:

```bash
xmake show --format=json                 # whole project as json
xmake show -t hello --format=json        # one target as json
xmake show -l targets --format=json
xmake show --format=json --pretty        # indented, easier to read in a terminal
```

::: tip
`--json` still works but is deprecated, use `--format=json`.
:::

Query one field without a json parser:

```bash
xmake show -t hello --format=json | grep -o '"sourcefiles":[^]]*]'
```

## Dependency graphs

The target dependency graph, as text, JSON, or graphviz DOT:

```bash
xmake show --info=depgraph                     # human readable tree
xmake show --info=depgraph --target=app        # only the subtree of one target
xmake show --info=depgraph --format=json       # for tooling
xmake show --info=depgraph --format=dot        # graphviz
xmake show --info=depgraph --format=dot | dot -Tsvg -o deps.svg
```

The package dependency graph comes from xrepo:

```bash
xrepo info --depgraph zlib
```

## Ask Lua directly

Anything the show plugin does not print is still reachable from a script, and this is the
most precise way to answer "what does the build actually see":

```bash
# the sourcefiles a target really compiles, after all rules and options
xmake l -c 'import("core.project.project"); for _, f in ipairs(project.target("hello"):sourcefiles()) do print(f) end'

# the deps and the packages of a target
xmake l -c 'import("core.project.project"); print(project.target("hello"):get("deps"))'

# the value of a config/option
xmake l -c 'import("core.project.config"); config.load(); print(config.get("arch"))'
```

`xmake l` runs in the project directory and loads it lazily, so `project.target(...)`
works without an explicit load.

::: warning
The resolved **flags** are the exception: reaching for `compiler.compflags()` from a bare
`xmake l` prints nothing and warns that the toolchain "has been not checked yet", because
no toolchain check ran in that process. Use `xmake show -t <target>` for flags — it prints
`compflags (cxx)` and `linkflags (ld)` after the real check.
:::

Useful entry points:

| Module | What it answers |
| --- | --- |
| `core.project.project` | targets, options, requires, rules of the loaded project |
| `core.project.config` | the current configuration (plat, arch, mode, builddir …) |
| `core.tool.toolchain` | the resolved toolchain and its toolset |
| `core.base.json` | encode the result when you need to pipe it somewhere |

## Where things are on disk

```bash
xmake show -l envs                 # XMAKE_* variables in effect
ls .xmake/<plat>/<arch>/           # per-project configuration cache
cat .xmake/<plat>/<arch>/xmake.conf
```

## Rules of thumb

- Do not infer the build from `xmake.lua` alone — options and packages change it.
- `xmake show -t <target>` first, `xmake l` only when you need a field it does not print.
- Configure before you introspect: `xmake f -p <plat> -a <arch>` — the resolved values
  depend on the current configuration.
- `--format=json` for anything a program reads; the plain output is for humans and may change.
