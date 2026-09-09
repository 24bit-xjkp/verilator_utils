import argparse
import os
import shutil
import sys
from functools import cache
from pathlib import Path

from colorama import Fore


@cache
def find_tool(name: str) -> Path | None:
    """查找工具所在路径，带有缓存

    Args:
        name (str): _工具名称

    Returns:
        Path | None: 工具所在路径，未找到为None
    """
    return Path(path) if (path := shutil.which(name)) is not None else None


class basic_config:
    """配置选项"""

    # 数据文件输出目录
    data_output_dir: Path = Path.cwd()
    # SystemVerilog源文件输出目录
    source_output_dir: Path = Path.cwd()
    # 安静模式
    quiet: bool = False
    # 最大并发任务数
    jobs: int = os.cpu_count() or 1
    # 源文件格式化
    format: bool = True
    # 在非交互环境下生成可视化图表
    visualize: bool = True
    # 优化svg输出
    optimize_svg: bool = find_tool("svgo") is not None
    # 对称量化
    sym_quant: bool = True

    @classmethod
    def register(cls, parser: argparse.ArgumentParser) -> None:
        """向argparse中注册命令行选项

        Args:
            parser (argparse.ArgumentParser): 选项解析器
        """
        # -o -> output
        parser.add_argument("-o", "--data-output-dir", type=str, help="数据文件输出目录", default=cls.data_output_dir)
        #  -g -> generate
        parser.add_argument("-g", "--source-output-dir", type=str, help="SystemVerilog源文件输出目录", default=cls.source_output_dir)
        # -q -> quiet
        parser.add_argument("-q", "--quiet", action="store_true", help="只输出错误信息")
        # -j -> jobs
        parser.add_argument("-j", "--jobs", type=int, help="最大并发任务数，为0使用CPU核心数", default=cls.jobs)
        parser.add_argument("--format", action=argparse.BooleanOptionalAction, help="在可能的情况下格式化输出的源文件", default=cls.format)
        parser.add_argument(
            "--visualize", action=argparse.BooleanOptionalAction, help="在非交互环境下生成可视化图表", default=cls.visualize
        )
        parser.add_argument(
            "--optimize-svg", action=argparse.BooleanOptionalAction, help="在输出svg图表时使用svgo工具进行优化", default=cls.optimize_svg
        )
        parser.add_argument("--sym-quant", action=argparse.BooleanOptionalAction, help="使用对称量化", default=cls.sym_quant)

    @classmethod
    def parse(cls, args: argparse.Namespace) -> None:
        """根据解析后的选项初始化属性

        Args:
            args (argparse.Namespace): 解析后的命令行选项
        """
        cls.data_output_dir = Path(args.data_output_dir)
        cls.source_output_dir = Path(args.source_output_dir)
        cls.quiet = args.quiet is not None
        if args.jobs != 0:
            cls.jobs = args.jobs
        assert cls.jobs > 0, "并发数必须是正数"
        cls.format = args.format
        cls.visualize = args.visualize
        cls.optimize_svg = args.optimize_svg
        cls.sym_quant = args.sym_quant

    @staticmethod
    def setup_argcomplete(parser: argparse.ArgumentParser) -> None:
        """配置argcomplete功能

        Args:
            parser (argparse.ArgumentParser): 选项解析器
        """
        try:
            import argcomplete

            argcomplete.autocomplete(parser)
        except:
            pass


@cache
def is_in_notebook() -> bool:
    """判断是否在Jupyter Notebook环境中运行"""

    return "ipykernel" in sys.modules


class logger:

    @classmethod
    def warning(cls, msg: str) -> None:
        """打印警告信息

        Args:
            msg (str): 要打印的信息
        """
        if basic_config.quiet:
            return
        print(f"{Fore.MAGENTA}{msg}{Fore.RESET}")

    @classmethod
    def note(cls, msg: str) -> None:
        """打印提示信息

        Args:
            msg (str): 要打印的信息
        """
        if basic_config.quiet:
            return
        print(f"{Fore.LIGHTBLUE_EX}{msg}{Fore.RESET}")

    @classmethod
    def success(cls, msg: str) -> None:
        """打印成功信息

        Args:
            msg (str): 要打印的信息
        """
        if basic_config.quiet:
            return
        print(f"{Fore.GREEN}{msg}{Fore.RESET}")

    @classmethod
    def info(cls, msg: str) -> None:
        """打印信息

        Args:
            msg (str): 要打印的信息
        """
        if basic_config.quiet:
            return
        print(msg)
