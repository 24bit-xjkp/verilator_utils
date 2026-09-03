#!/usr/bin/env python
import argparse
import importlib.util
import sys
from pathlib import Path

import depfinder

assert __name__ == "__main__", "该文件必须作为脚本执行"
parser = argparse.ArgumentParser(description="扫描python脚本的依赖")
parser.add_argument("-o", "--output", type=str, help="依赖文件，默认输出到标准输出流")
parser.add_argument("script", type=str, help="要扫描的python脚本")

project_root = Path(__file__).parents[1]
args = parser.parse_args()
script_path = Path(args.script)
script_path = script_path if script_path.is_absolute() else project_root / script_path
script_path = script_path.resolve()
assert script_path.is_file() and script_path.suffix == ".py", f"{script_path}不是一个Python脚本"

dependencies = depfinder.parse_file(script_path)[2]
script_dir = script_path.parent
if script_dir not in sys.path:
    sys.path.insert(0, str(script_dir))
deps: set[str] = set()
for module in dependencies.required_modules:
    spec = importlib.util.find_spec(module)
    if spec and spec.origin and (source_path := Path(spec.origin)).is_relative_to(project_root):
        if source_path.name == "__init__.py":
            deps |= set(str(dep.relative_to(project_root)) for dep in source_path.glob("*.py"))
        else:
            deps.add(str(source_path.relative_to(project_root)))
deps.add(str(script_path.relative_to(project_root)))

output_str = "\n".join(deps)
if args.output:
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(output_str)
else:
    print(f"Depends:\n{output_str}")
