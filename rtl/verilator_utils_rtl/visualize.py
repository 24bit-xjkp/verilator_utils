import subprocess
from collections.abc import Callable, Generator
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager
from enum import Enum
from pathlib import Path
from typing import Any, Literal, overload

import matplotlib
import matplotlib.pyplot as plt
import numpy as np
import scipy
from matplotlib.axes import Axes
from matplotlib.figure import Figure, SubFigure
from matplotlib.layout_engine import LayoutEngine
from numpy import ndarray
from numpy.typing import NDArray

from .common import basic_config, find_tool, is_in_notebook, logger
from .dsp import linear2dB


def set_notebook_env() -> None:
    """配置Jupyter Notebook环境"""
    assert is_in_notebook(), "必须在Jupyter Notebook环境中运行"
    from IPython.core.getipython import get_ipython

    ip = get_ipython()
    assert ip is not None
    ip.run_line_magic("matplotlib", "inline")
    ip.run_line_magic("config", "InlineBackend.figure_format = 'svg'")


def set_matplotlib_font() -> None:
    """设置matplotlib中文字体"""
    matplotlib.rcParams["font.serif"] = "Noto Serif CJK SC"
    matplotlib.rcParams["font.sans-serif"] = "Noto Sans CJK SC"
    matplotlib.rcParams["axes.unicode_minus"] = False


@overload
def create_subplots(
    rows: Literal[1],
    cols: Literal[1],
    single_figsize: tuple[int | float, int | float],
    squeeze: Literal[True] = ...,
    layout: str | LayoutEngine | None = ...,
    **kwargs: Any,
) -> tuple[Figure, Axes]: ...


@overload
def create_subplots(
    rows: int,
    cols: int,
    single_figsize: tuple[int | float, int | float],
    squeeze: bool,
    layout: str | LayoutEngine | None = ...,
    **kwargs: Any,
) -> tuple[Figure, ndarray]: ...


def create_subplots(
    rows: int,
    cols: int,
    single_figsize: tuple[int | float, int | float],
    squeeze: bool = True,
    layout: str | LayoutEngine | None = "constrained",
    **kwargs: Any,
) -> tuple[Figure, Axes | ndarray]:
    """创建子图

    Args:
        rows (int): 行数
        cols (int): 列数
        single_figsize (tuple[int | float, int | float]): 单个子图的尺寸
        squeeze (bool, optional): 是否展平子图数组的维度
        layout (str | LayoutEngine | None, optional): 布局引擎，默认为constrained引擎

    Returns:
        tuple[Figure, Axes | ndarray]: 子图对象
    """
    w, h = single_figsize
    figsize = w * cols, h * rows
    return plt.subplots(rows, cols, figsize=figsize, squeeze=squeeze, layout=layout, **kwargs)


@overload
@contextmanager
def subplots_env(
    rows: Literal[1],
    cols: Literal[1],
    single_figsize: tuple[int | float, int | float],
    squeeze: Literal[True] = ...,
    layout: str | LayoutEngine | None = ...,
    **kwargs: Any,
) -> Generator[tuple[Figure, Axes], Any, None]: ...


@overload
@contextmanager
def subplots_env(
    rows: int,
    cols: int,
    single_figsize: tuple[int | float, int | float],
    squeeze: bool,
    layout: str | LayoutEngine | None = ...,
    **kwargs: Any,
) -> Generator[tuple[Figure, ndarray], Any, None]: ...


@contextmanager
def subplots_env(
    rows: int,
    cols: int,
    single_figsize: tuple[int | float, int | float],
    squeeze: bool = True,
    layout: str | LayoutEngine | None = "constrained",
    **kwargs: Any,
) -> Generator[tuple[Figure, Axes | ndarray], Any, None]:
    """创建子图环境，退出时关闭子图

    Args:
        rows (int): 行数
        cols (int): 列数
        single_figsize (tuple[int | float, int | float]): 单个子图的尺寸
        squeeze (bool, optional): 是否展平子图数组的维度
        layout (str | LayoutEngine | None, optional): 布局引擎，默认为constrained引擎
    """
    fig, axes = create_subplots(rows, cols, single_figsize, squeeze, layout, **kwargs)
    try:
        yield fig, axes
    except:
        raise
    finally:
        plt.close(fig)


@overload
def create_figures(
    rows: int,
    cols: int,
    figsize: tuple[int | float, int | float],
    squeeze: bool,
    layout: str | LayoutEngine | None = ...,
    figure_kwargs: dict[str, Any] = ...,
    subfigures_kwargs: dict[str, Any] = ...,
) -> tuple[Figure, ndarray]: ...


@overload
def create_figures(
    rows: Literal[1],
    cols: Literal[1],
    figsize: tuple[int | float, int | float],
    squeeze: Literal[True] = ...,
    layout: str | LayoutEngine | None = ...,
    figure_kwargs: dict[str, Any] = ...,
    subfigures_kwargs: dict[str, Any] = ...,
) -> tuple[Figure, SubFigure]: ...


def create_figures(
    rows: int,
    cols: int,
    figsize: tuple[int | float, int | float],
    squeeze: bool = True,
    layout: str | LayoutEngine | None = "constrained",
    figure_kwargs: dict[str, Any] = {},
    subfigures_kwargs: dict[str, Any] = {},
) -> tuple[Figure, SubFigure | ndarray]:
    """创建子图

    Args:
        rows (int): 行数
        cols (int): 列数
        figsize (tuple[int | float, int | float]): 整个图的尺寸
        squeeze (bool, optional): 是否展平子图数组的维度
        layout (str | LayoutEngine | None, optional): 布局引擎，默认为constrained引擎
        figure_kwargs (dict[str, Any], optional): 传递给plt.figure函数的额外参数
        subfigures_kwargs (dict[str, Any], optional): 传递给fig.subfigures函数的额外参数

    Returns:
        tuple[Figure, SubFigure | ndarray]: 子图对象
    """
    fig = plt.figure(figsize=figsize, layout=layout, **figure_kwargs)
    subfigures = fig.subfigures(rows, cols, squeeze, **subfigures_kwargs)  # type: ignore[call-overload]
    return fig, subfigures


@overload
@contextmanager
def figure_env(
    rows: int,
    cols: int,
    figsize: tuple[int | float, int | float],
    squeeze: bool,
    layout: str | LayoutEngine | None = ...,
    figure_kwargs: dict[str, Any] = {},
    subfigures_kwargs: dict[str, Any] = {},
) -> Generator[tuple[Figure, ndarray], Any, None]: ...


@overload
@contextmanager
def figure_env(
    rows: Literal[1],
    cols: Literal[1],
    figsize: tuple[int | float, int | float],
    squeeze: Literal[True] = ...,
    layout: str | LayoutEngine | None = ...,
    figure_kwargs: dict[str, Any] = {},
    subfigures_kwargs: dict[str, Any] = {},
) -> Generator[tuple[Figure, SubFigure], Any, None]: ...


@contextmanager
def figure_env(
    rows: int,
    cols: int,
    figsize: tuple[int | float, int | float],
    squeeze: bool = True,
    layout: str | LayoutEngine | None = "constrained",
    figure_kwargs: dict[str, Any] = {},
    subfigures_kwargs: dict[str, Any] = {},
) -> Generator[tuple[Figure, SubFigure | ndarray], Any, None]:
    """创建子图环境，退出时关闭子图

    Args:
        rows (int): 行数
        cols (int): 列数
        figsize (tuple[int | float, int | float]): 整个图的尺寸
        squeeze (bool, optional): 是否展平子图数组的维度
        layout (str | LayoutEngine | None, optional): 布局引擎，默认为constrained引擎
        figure_kwargs (dict[str, Any], optional): 传递给plt.figure函数的额外参数
        subfigures_kwargs (dict[str, Any], optional): 传递给fig.subfigures函数的额外参数
    """
    fig, subfigures = create_figures(rows, cols, figsize, squeeze, layout, figure_kwargs, subfigures_kwargs)
    try:
        yield fig, subfigures
    except:
        raise
    finally:
        plt.close(fig)


class time_unit(float, Enum):
    """时间单位"""

    s = 1.0
    ms = 1e-3
    us = 1e-6
    ns = 1e-9
    ps = 1e-12
    fs = 1e-15


class freq_unit(float, Enum):
    """频率单位"""

    THz = 1e12
    GHz = 1e9
    MHz = 1e6
    kHz = 1e3
    Hz = 1.0


def as_discrete(length: int, discrete: bool | None) -> bool:
    """是否按离散信号绘图

    Args:
        length (int): 信号长度
        discrete (bool | None): 是否按离散信号绘图，为None根据length推导
    """
    if discrete is None:
        discrete = True if length <= 100 else False
    return discrete


def visualize_waveform(
    ax: Axes, t: ndarray, x: ndarray, title: str = "波形图", unit: time_unit | None = None, discrete: bool | None = None, **kwargs: Any
) -> None:
    """绘制波形图

    Args:
        ax (Axes): 子图
        t (ndarray): 时间数组，单位为s
        x (ndarray): 信号数组
        title (str, optional): 图表标题
        unit (time_unit | None, optional): 图的时间单位，为None根据t的范围推导
        discrete (bool | None, optional): 是否按离散信号绘图，为None根据t的长度推导
    """
    assert t.ndim == 1 and x.ndim == 1, "波形图只接受一维数组"
    n = len(t)
    assert n == len(x), f"时间数组长度{n}和信号数组长度{len(x)}不相等"
    if unit is None:
        # 时间应当是单调递增的
        t_max = float(t[-1])
        for unit_ in time_unit:
            if t_max > unit_:
                unit = unit_
                break
        else:
            unit = time_unit.fs
    (ax.stem if as_discrete(n, discrete) else ax.plot)(t / unit, x, **kwargs)
    unit_str = unit.name if unit is not time_unit.us else R"$\mathrm{\mu s}$"
    ax.set_xlabel(f"时间 ({unit_str})")
    ax.set_ylabel("幅度")
    ax.set_title(title)


def visualize_spectrogram(
    ax: Axes,
    f: ndarray,
    x: ndarray,
    linear: bool = True,
    title: str | None = None,
    unit: freq_unit | None = None,
    discrete: bool | None = None,
    **kwargs: Any,
) -> None:
    """绘制频谱图

    Args:
        ax (Axes): 子
        f (ndarray): 频率数组，单位为Hz
        x (ndarray): 频谱数组
        linear (bool, optional): 为True幅度为线性值，为False幅度为dB，且最大值为0dB
        title (str | None, optional): 图表标题，为None根据频谱性质推断名称
        unit (freq_unit | None, optional): 图的频率单位，为None根据fs的范围推导
        discrete (bool | None, optional): 是否按离散信号绘图，为None根据f的长度推导
    """
    assert f.ndim == 1 and x.ndim == 1, "波形图只接受一维数组"
    n = len(f)
    assert n == len(x), f"频率数组长度{n}和频谱数组长度{len(x)}不相等"
    if unit is None:
        if float(f[-1]) < 0.0:
            # 进行fftshift
            f = scipy.fft.fftshift(f)
            x = scipy.fft.fftshift(x)
        nyquist_freq = float(f[-1])
        for unit_ in freq_unit:
            if nyquist_freq > unit_:
                unit = unit_
                break
        else:
            unit = freq_unit.Hz
    x = np.abs(x)
    if not linear:
        x = linear2dB(x)
    (ax.stem if as_discrete(n, discrete) else ax.plot)(f / unit, x, **kwargs)
    ax.set_xlabel(f"频率 ({unit.name})")
    ax.set_ylabel("幅度" if linear else "dB")
    if title is None:
        title = "双边功率谱" if float(f[0]) < 0.0 else "单边功率谱"
    ax.set_title(title)


type window_t = Callable[[int], ndarray] | ndarray | None


def get_window_array(n: int, window: window_t) -> ndarray | None:
    """根据窗函数计算窗数组

    Args:
        n (int): 窗长
        window (window_t): 窗函数，为None表示不加窗

    Returns:
        ndarray | None: 窗数组
    """
    return window if isinstance(window, ndarray | None) else window(n)


def visualize_spectrogram_fft(
    ax: Axes,
    fs: int | float,
    x: ndarray,
    window: window_t = scipy.signal.windows.hann,
    linear: bool = True,
    title: str = "双边幅度谱",
    unit: freq_unit | None = None,
    discrete: bool | None = None,
    **kwargs: Any,
) -> tuple[NDArray[np.complex128], NDArray[np.complex128]]:
    """计算FFT并绘制频谱图

    Args:
        ax (Axes): 子
        fs (int | float): 采样率，单位为Hz
        x (ndarray): 信号数组
        window (window_t, optional): 窗函数，为None表示不加窗
        linear (bool, optional): 为True幅度为线性值，为False幅度为dB，且最大值为0dB
        title (str, optional): 图表标题
        unit (freq_unit | None, optional): 图的频率单位，为None根据fs的范围推导
        discrete (bool | None, optional): 是否按离散信号绘图，为None根据x的长度推导

    Returns:
        tuple[NDArray[np.complex128], NDArray[np.complex128]]: FFT结果, 修正后的FFT结果
    """
    assert x.ndim == 1, "波形图只接受一维数组"
    n = len(x)
    f: NDArray[np.float64] = scipy.fft.fftfreq(n, 1 / fs)
    window_arr = get_window_array(n, window)
    spectrogram: NDArray[np.complex128] = scipy.fft.fft(x if window_arr is None else x * window_arr)
    scaled_spectrogram = spectrogram / (n if window_arr is None else np.sum(window_arr))
    visualize_spectrogram(ax, f, scaled_spectrogram, linear, title, unit, discrete, **kwargs)
    return spectrogram, scaled_spectrogram


def visualize_spectrogram_rfft(
    ax: Axes,
    fs: int | float,
    x: ndarray,
    window: window_t = scipy.signal.windows.hann,
    linear: bool = True,
    title: str = "单边幅度谱",
    unit: freq_unit | None = None,
    discrete: bool | None = None,
    **kwargs: Any,
) -> tuple[NDArray[np.complex128], NDArray[np.complex128]]:
    """计算RFFT并绘制频谱图

    Args:
        ax (Axes): 子
        fs (int | float): 采样率，单位为Hz
        x (ndarray): 信号数组
        window (window_t, optional): 窗函数，为None表示不加窗
        linear (bool, optional): 为True幅度为线性值，为False幅度为dB，且最大值为0dB
        title (str, optional): 图表标题
        unit (freq_unit | None, optional): 图的频率单位，为None根据fs的范围推导
        discrete (bool | None, optional): 是否按离散信号绘图，为None根据x的长度推导

    Returns:
        tuple[NDArray[np.complex128], NDArray[np.complex128]]: FFT结果, 修正后的FFT结果
    """
    assert x.ndim == 1, "波形图只接受一维数组"
    assert np.isrealobj(x), "RFFT只接受实信号"
    n = len(x)
    f: NDArray[np.float64] = scipy.fft.rfftfreq(n, 1 / fs)
    window_arr = get_window_array(n, window)
    spectrogram: NDArray[np.complex128] = scipy.fft.rfft(x if window_arr is None else x * window_arr)
    scaled_spectrogram = spectrogram / (n if window_arr is None else np.sum(window_arr))
    if n % 2 == 0:
        scaled_spectrogram[1:-1] *= 2
    else:
        scaled_spectrogram[1:] *= 2
    visualize_spectrogram(ax, f, scaled_spectrogram, linear, title, unit, discrete, **kwargs)
    return spectrogram, scaled_spectrogram


class figure_processor:
    """图片处理器"""

    __pool: ThreadPoolExecutor | None
    __svgo: Path | None

    def __init__(self, pool: ThreadPoolExecutor | None) -> None:
        """创建图片处理对象

        Args:
            pool (ThreadPoolExecutor | None): 线程池，为None表示不对输出图片进行优化
        """
        self.__pool = pool
        if pool is not None:
            self.__svgo = find_tool("svgo")
            if self.__svgo is None:
                logger.warning("未找到svgo，跳过svg图片优化")
        else:
            self.__svgo = None

    def __call__(self, fig: Figure, output_path: Path) -> Any:
        """处理绘制完成的图片。在交互环境下会在notebook中展示，否则将图片保存到文件

        Args:
            fig (Figure): 图片对象
            output_path (Path): 输出路径
        """
        if is_in_notebook():
            plt.show(fig)
        else:
            plt.savefig(output_path)
            if output_path.suffix == ".svg" and self.__pool is not None and self.__svgo is not None:
                # 使得svgo和matplotlib并发执行
                self.__pool.submit(subprocess.run, [self.__svgo, output_path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


@contextmanager
def process_figure(optimize_svg: bool | None = None, jobs: int | None = None) -> Generator[figure_processor, Any, None]:
    """创建图片处理上下文

    Args:
        optimize_svg (bool | None, optional): 是否优化输出的svg图片
        jobs (int | None, optional): 最大并发任务数
    """
    if optimize_svg is None:
        optimize_svg = basic_config.optimize_svg
    if jobs is None:
        jobs = basic_config.jobs
    if optimize_svg:
        with ThreadPoolExecutor(jobs) as pool:
            yield figure_processor(pool)
    else:
        yield figure_processor(None)
