"""pytest公共配置

配置无界面后端，并在每个测试结束后恢复basic_config等全局状态，保证测试相互隔离。
"""

from collections.abc import Generator

import matplotlib
import pytest
from verilator_utils_rtl.common import basic_config, find_tool, is_in_notebook

# 无界面环境统一使用Agg后端，必须在导入pyplot之前完成切换
matplotlib.use("Agg")

import matplotlib.pyplot as plt

# basic_config中以类属性保存、且测试会修改的运行配置
_CONFIG_ATTRS: tuple[str, ...] = (
    "data_output_dir",
    "source_output_dir",
    "quiet",
    "jobs",
    "format",
    "visualize",
    "optimize_svg",
    "sym_quant",
)


@pytest.fixture(autouse=True)
def _restore_global_state() -> Generator[None, None, None]:
    """恢复basic_config类属性、清空find_tool缓存并关闭所有matplotlib图片"""
    saved = {name: getattr(basic_config, name) for name in _CONFIG_ATTRS}
    try:
        yield
    finally:
        for name, value in saved.items():
            setattr(basic_config, name, value)
        find_tool.cache_clear()
        is_in_notebook.cache_clear()
        plt.close("all")
