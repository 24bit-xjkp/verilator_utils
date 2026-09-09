import math
from typing import overload

import numpy as np
from numpy import ndarray
from numpy.typing import NDArray


def get_range(width: int, signed: bool) -> tuple[int, int]:
    """根据数据宽度和符号获取取值范围

    Args:
        width (int): 数据宽度
        signed (bool): 是否有符号

    Returns:
        tuple[int, int]: (最小值, 最大值)
    """
    x_min = -1 << (width - 1) if signed else 0
    x_max = (1 << (width - 1 if signed else width)) - 1
    return x_min, x_max


def affine_scale_param(x: ndarray, width: int, signed: bool = True, guard: float = 0.01) -> tuple[float, float]:
    """通过仿射变换将输入缩放到指定范围内所需的参数

    Args:
        x (ndarray): 输入数组
        width (int): 输出数据宽度
        signed (bool, optional): 输出是否有符号
        guard (float, optional): 保护空间比例，取值范围为[0, 1)

    Returns:
        tuple[float, float]: (缩放因子, 偏置)
    """
    assert width > 0, "信号位宽必须为正数"
    assert 0.0 <= guard < 1, f"保护空间{guard}超出取值范围[0, 1)"
    y_min, y_max = get_range(width, signed)
    y_zero_point = (y_min + y_max + 1) // 2
    y_ptp = y_max - y_min
    x_min, x_max = float(np.min(x)), float(np.max(x))
    x_ptp = x_max - x_min
    x_zero_point = x_min + x_ptp / 2.0

    if x_ptp == 0.0:
        return 0.0, float(y_zero_point)
    factor = y_ptp / x_ptp * (1.0 - guard)
    bias = y_zero_point - x_zero_point * factor
    return factor, bias


@overload
def affine_scale(x: ndarray, width: int, signed: bool = ..., guard: float = ..., /) -> ndarray:
    """通过仿射变换将输入缩放到指定范围内

    Args:
        x (ndarray): 输入数组
        width (int): 输出数据宽度
        signed (bool, optional): 输出是否有符号
        guard (float, optional): 保护空间比例，取值范围为[0, 1)

    Returns:
        ndarray: 缩放后的数组，不论有无符号都按照int64储存
    """


@overload
def affine_scale(x: ndarray, factor: int | float, bias: int | float, /) -> ndarray:
    """通过仿射变换将输入缩放到指定范围内

    Args:
        x (ndarray): 输入数组
        factor (int | float): 缩放因子
        bias (int | float): 偏置

    Returns:
        ndarray: 缩放后的数组，不论有无符号都按照int64储存
    """


def affine_scale(x: ndarray, factor_width: int | float, bias_signed: int | float | bool = True, guard: float = 0.01) -> ndarray:
    if isinstance(bias_signed, bool):
        assert isinstance(factor_width, int), "信号宽度必须是整数"
        width, signed = factor_width, bias_signed
        factor, bias = affine_scale_param(x, width, signed, guard)
    else:
        assert isinstance(bias_signed, int | float), "偏置必须是整数或浮点数"
        factor, bias = float(factor_width), float(bias_signed)
    return x * factor + bias


def linear_scale_param(x: ndarray, width: int, signed: bool = True, guard: float = 0.01) -> float | None:
    """通过线性变换将输入缩放到指定范围内所需的参数

    Args:
        x (ndarray): 输入数组
        width (int): 输出数据宽度
        signed (bool, optional): 输出是否有符号
        guard (float, optional): 保护空间比例，取值范围为[0, 1)

    Returns:
        float | None: 缩放因子，求解失败时为None
    """
    x_min, x_max = float(np.min(x)), float(np.max(x))
    if signed and math.copysign(1.0, x_min) == math.copysign(1.0, x_max):
        return None
    if not signed and math.copysign(1.0, x_min) != math.copysign(1.0, x_max):
        return None
    y_min, y_max = get_range(width, signed)
    guard_factor = 1.0 - guard
    if signed:
        if x_min == 0.0 and x_max == 0.0:
            return 0.0
        min_factor = y_min / x_min if x_min != 0.0 else math.inf
        max_factor = y_max / x_max if x_max != 0.0 else math.inf
        return min(min_factor, max_factor) * guard_factor
    else:
        abs_max = max(abs(x_min), abs(x_max))
        if abs_max == 0.0:
            return 0.0
        return math.copysign(y_max / abs_max * guard_factor, x_max)


@overload
def linear_scale(x: ndarray, width: int, signed: bool = ..., guard: float = ..., /) -> ndarray:
    """通过线性变换将输入缩放到指定范围内

    Args:
        x (ndarray): 输入数组
        width (int): 输出数据宽度
        signed (bool, optional): 输出是否有符号
        guard (float, optional): 保护空间比例，取值范围为[0, 1)

    Returns:
        ndarray: 缩放后的数组，不论有无符号都按照int64储存
    """


@overload
def linear_scale(x: ndarray, factor: float, /) -> ndarray:
    """通过线性变换将输入缩放到指定范围内

    Args:
        x (ndarray): 输入数组
        factor (float): 缩放因子

    Returns:
        ndarray: 缩放后的数组，不论有无符号都按照int64储存
    """


def linear_scale(x: ndarray, factor_width: int | float, signed: bool = True, guard: float = 0.01) -> ndarray:
    if isinstance(factor_width, int):
        factor = linear_scale_param(x, factor_width, signed, guard)
        assert factor is not None, "无法找到满足约束条件的缩放因子"
    else:
        factor = factor_width
    return x * factor


def width_cast(x: NDArray[np.int64], width: int) -> NDArray[np.uint64]:
    """将输入截断到指定宽度

    相当于bit_cast<uint64>(x)[width-1:0]

    Args:
        x (NDArray[np.int64]): 输入数组
        width (int): 输出数据宽度

    Returns:
        NDArray[np.uint64]: 截断后的数组
    """
    assert width > 0, "信号位宽必须为正数"
    y = x.astype(np.uint64)
    y &= (1 << width) - 1
    return y


def in_range(x: int | float | ndarray, width: int, signed: bool) -> bool | NDArray[np.bool]:
    """判断输入是否在数据的取值范围内

    Args:
        x (int | float | ndarray): 输入数据
        width (int): 数据宽度
        signed (bool): 是否有符号

    Returns:
        bool | NDArray[np.bool]: 判断结果
    """
    x_min, x_max = get_range(width, signed)
    match x:
        case ndarray():
            return (x >= x_min) & (x <= x_max)
        case _:
            return x_min <= x <= x_max


def check_range(x: int | float | ndarray, width: int, signed: bool) -> None:
    """检查输入是否在数据的取值范围内，超出范围则断言失败

    Args:
        x (int | float | ndarray): 输入数据
        width (int): 数据宽度
        signed (bool): 是否有符号
    """
    message = f"超出{'int' if signed else 'uint'}{width}范围"
    match x:
        case ndarray():
            mask = in_range(x, width, signed)
            assert np.all(mask), message
        case _:
            assert in_range(x, width, signed), f"{x}{message}"


class nco:
    @staticmethod
    def time(fs: int | float, n: int | float, t0: int | float = 0.0) -> NDArray[np.float64]:
        """生成时间数组

        Args:
            fs (int | float): 采样率
            n (int | float): 样本点数
            t0 (int | float, optional): 初始时刻

        Returns:
            NDArray[np.float64]: 时间数组
        """
        fs, n, t0 = float(fs), round(n), float(t0)
        assert n > 0, "样本点数必须为正数"
        final_t = t0 + (n - 1) / fs
        return np.linspace(t0, final_t, n, dtype=np.float64)

    @staticmethod
    def __check_nyquist_freq(f: int | float | ndarray, fs: int | float) -> None:
        """检查信号频率是否超过尼奎斯特频率

        Args:
            f (int | float | ndarray): 信号频率
            fs (int | float): 采样率
        """
        nyquist_freq = fs / 2
        if isinstance(f, ndarray):
            assert np.all(f <= nyquist_freq).item(), f"信号频率超过尼奎斯特频率{nyquist_freq}"
        else:
            assert f <= nyquist_freq, f"信号频率{f}超过尼奎斯特频率{nyquist_freq}"

    @overload
    @staticmethod
    def sine(
        amp: int | float | ndarray, f: int | float | ndarray, phi: int | float, n: int | float, fs: int | float, /
    ) -> NDArray[np.float64]:
        """生成正弦信号

        Args:
            amp (int | float | ndarray): 振幅
            f (int | float | ndarray): 信号频率
            phi (int | float | ndarray): 附加初始相位
            n (int | float): 样本点数
            fs (int | float): 采样率

        Returns:
            NDArray[np.float64]: 信号数组
        """

    @overload
    @staticmethod
    def sine(amp: int | float | ndarray, f: int | float | ndarray, phi: int | float, t: ndarray, /) -> NDArray[np.float64]:
        """生成正弦信号

        Args:
            amp (int | float | ndarray): 振幅
            f (int | float | ndarray): 频率
            phi (int | float | ndarray): 附加初始相位
            t (ndarray): 时间数组，t0会作为初始相位的一部分

        Returns:
            NDArray[np.float64]: 信号数组
        """

    @overload
    @staticmethod
    def sine(
        amp: int | float | ndarray, f: int | float | ndarray, phi: int | float, n_t: int | float | ndarray, fs: int | float | None = ...
    ) -> NDArray[np.float64]:
        """生成正弦信号

        Args:
            amp (int | float | ndarray): 振幅
            f (int | float | ndarray): 频率
            n_t (int | float | ndarray): 样本点数或时间数组，为时间数组时t0会作为初始相位的一部分
            phi (int | float | ndarray): 附加初始相位
            fs (int | float | None, optional): 采样率，在n_t为采样点数时必须设置，在n_t为时间数组时可选

        Returns:
            NDArray[np.float64]: 信号数组
        """

    @staticmethod
    def sine(
        amp: int | float | ndarray, f: int | float | ndarray, phi: int | float, n_t: int | float | ndarray, fs: int | float | None = None
    ) -> NDArray[np.float64]:
        phase: NDArray[np.float64]
        if isinstance(n_t, ndarray):
            t = n_t
            # 尝试从时间数组中推导采样率
            if fs is None and t.ndim == 1 and len(t) >= 2:
                fs = 1.0 / (float(t[1]) - float(t[0]))
            if fs is not None:
                nco.__check_nyquist_freq(f, fs)
            phase = (2 * np.pi * f * t + phi).astype(np.float64)
        else:
            n = round(n_t)
            assert n > 0, "样本点数必须为正数"
            assert fs is not None, "必须设置采样率"
            nco.__check_nyquist_freq(f, fs)
            final_phase = phi + (n - 1) * f / fs * 2 * np.pi
            phase = np.linspace(phi, final_phase, n)
        return amp * np.cos(phase)

    @overload
    @staticmethod
    def sin(amp: int | float | ndarray, f: int | float | ndarray, n: int | float, fs: int | float, /) -> NDArray[np.float64]:
        """生成正弦波

        Args:
            amp (int | float | ndarray): 振幅
            f (int | float | ndarray): 信号频率
            n (int | float): 样本点数
            fs (int | float): 采样率

        Returns:
            NDArray[np.float64]: 信号数组
        """

    @overload
    @staticmethod
    def sin(amp: int | float | ndarray, f: int | float | ndarray, t: ndarray, /) -> NDArray[np.float64]:
        """生成正弦波

        Args:
            amp (int | float | ndarray): 振幅
            f (int | float | ndarray): 频率
            t (ndarray): 时间数组，t0会作为初始相位的一部分

        Returns:
            NDArray[np.float64]: 信号数组
        """

    @overload
    @staticmethod
    def sin(
        amp: int | float | ndarray, f: int | float | ndarray, n_t: int | float | ndarray, fs: int | float | None = ...
    ) -> NDArray[np.float64]:
        """生成正弦波

        Args:
            amp (int | float | ndarray): 振幅
            f (int | float | ndarray): 频率
            n_t (int | float | ndarray): 样本点数或时间数组，为时间数组时t0会作为初始相位的一部分
            fs (int | float | None, optional): 采样率，在n_t为采样点数时必须设置，在n_t为时间数组时可选

        Returns:
            NDArray[np.float64]: 信号数组
        """

    @staticmethod
    def sin(
        amp: int | float | ndarray, f: int | float | ndarray, n_t: int | float | ndarray, fs: int | float | None = None
    ) -> NDArray[np.float64]:
        return nco.sine(amp, f, -np.pi / 2, n_t, fs)

    @overload
    @staticmethod
    def cos(amp: int | float | ndarray, f: int | float | ndarray, n: int | float, fs: int | float, /) -> NDArray[np.float64]:
        """生成余弦波

        Args:
            amp (int | float | ndarray): 振幅
            f (int | float | ndarray): 信号频率
            n (int | float): 样本点数
            fs (int | float): 采样率

        Returns:
            NDArray[np.float64]: 信号数组
        """

    @overload
    @staticmethod
    def cos(amp: int | float | ndarray, f: int | float | ndarray, t: ndarray, /) -> NDArray[np.float64]:
        """生成余弦波

        Args:
            amp (int | float | ndarray): 振幅
            f (int | float | ndarray): 频率
            t (ndarray): 时间数组，t0会作为初始相位的一部分

        Returns:
            NDArray[np.float64]: 信号数组
        """

    @overload
    @staticmethod
    def cos(
        amp: int | float | ndarray, f: int | float | ndarray, n_t: int | float | ndarray, fs: int | float | None = ...
    ) -> NDArray[np.float64]:
        """生成余弦波

        Args:
            amp (int | float | ndarray): 振幅
            f (int | float | ndarray): 频率
            n_t (int | float | ndarray): 样本点数或时间数组，为时间数组时t0会作为初始相位的一部分
            fs (int | float | None, optional): 采样率，在n_t为采样点数时必须设置，在n_t为时间数组时可选

        Returns:
            NDArray[np.float64]: 信号数组
        """

    @staticmethod
    def cos(
        amp: int | float | ndarray, f: int | float | ndarray, n_t: int | float | ndarray, fs: int | float | None = None
    ) -> NDArray[np.float64]:
        return nco.sine(amp, f, 0, n_t, fs)


def padding[T: np.generic](x: NDArray[T], n: int | float, v: int | float = 0.0) -> NDArray[T]:
    """在数组右侧填充常数

    Args:
        x (NDArray[T]): 输入数组
        n (int | float): 填充长度
        v (int | float, optional): 填充值，默认为0

    Returns:
        NDArray[T]: 填充后的数组
    """
    return np.pad(x, (0, round(n)), constant_values=v)


def clog2(x: int) -> int:
    """寻找表达x所需的最小位数

    Args:
        x (int): 给定值，必须是正数

    Returns:
        int: 最小位数
    """
    assert x > 0
    return math.ceil(math.log2(x))


@overload
def linear2dB(linear: int | float, ref: int | float | None = ..., square: bool = False) -> float:
    """将线性值转化为dB数

    Args:
        linear (int | float | ndarray): 线性值
        ref (int | float | None, optional): 参考值，为None时以1为参考
        square (bool, optional): 计算时是否对线性值平方

    Returns:
        float: dB数
    """


@overload
def linear2dB(linear: ndarray, ref: int | float | ndarray | None = ..., square: bool = False) -> NDArray[np.float64]:
    """将线性值转化为dB数

    Args:
        linear (ndarray): 线性值
        ref (int | float | None, optional): 参考值，为None时以np.max(linear)为参考
        square (bool, optional): 计算时是否对线性值平方

    Returns:
        float: dB数
    """


def linear2dB(linear: int | float | ndarray, ref: int | float | ndarray | None = None, square: bool = False) -> float | NDArray[np.float64]:
    factor = 20.0 if square else 10.0
    match linear:
        case int() | float():
            assert not isinstance(ref, ndarray), "不支持以ndarray为参考值"
            if ref is None:
                ref = 1.0
            return factor * math.log10(linear / ref)
        case ndarray():
            if ref is None:
                ref = np.max(linear)
            return factor * np.log10((linear / ref).astype(np.float64))
