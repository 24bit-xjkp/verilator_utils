#!/usr/bin/env python
# PYTHON_ARGCOMPLETE_OK
# %%
import argparse
import typing
from pathlib import Path
from typing import Final

import numpy as np
import scipy
import verilator_utils_rtl as rtl
from matplotlib.figure import SubFigure
from numpy.typing import NDArray

config = rtl.basic_config


class sim_env:
    # ---- 仿真参数 ----

    # 信号宽度
    data_width: Final = 16
    # 权重宽度
    weight_width: Final = 16
    # 卷积核点数
    taps: Final = 31
    # 并行度
    parallel: Final = 4
    # 采样率
    fs: Final = 500e6  # 500MHz
    # 信号基波频率
    f_signal: Final = 5e6  # 5MHz
    # 截止频率
    f_cutoff: Final = 15e6  # 15MHz
    # 信号频率步进
    f_step: Final = 50e6  # 50MHz
    # 附加相移
    extra_delay_samples: Final = -taps / 6
    # 信号基波周期数
    cycle: Final = 30
    # 使用对称量化
    sym_quant: Final = True

    # ---- 仿真数据 ----

    # 原始时间数组
    t: NDArray[np.float64]
    # 原始信号数组
    x: NDArray[np.float64]
    # 卷积核
    kernel: NDArray[np.float64]
    # 带有附加相移的卷积核
    shifted_kernel: NDArray[np.float64]
    # 卷积核是否对称
    is_kernel_sym: bool
    # 带有附加相移的卷积核是否对称
    is_shifted_kernel_sym: bool
    # 量化后的卷积核
    q_kernel: NDArray[np.int16]
    # 量化后的带有附加相移的卷积核
    q_shifted_kernel: NDArray[np.int16]
    # FIR滤波后的信号数组
    y: NDArray[np.float64]
    # 带附加相移FIR滤波后的信号数组
    shifted_y: NDArray[np.float64]
    # 量化后的原始信号数组
    q_x: NDArray[np.int16]
    # 量化后的FIR滤波后信号数组
    q_y: NDArray[np.int16]
    # 量化后的带附加相移FIR滤波后信号数组
    q_shifted_y: NDArray[np.int16]

    def eval(self, sym_quant: bool | None = None) -> None:
        """执行仿真过程

        Args:
            sym_quant (bool | None, optional): 使用对称量化，为None使用运行环境中配置的默认参数
        """
        if sym_quant is None:
            sym_quant = self.sym_quant if rtl.is_in_notebook() else config.sym_quant

        # 生成原始时间序列
        n: Final = round(self.fs / self.f_signal * self.cycle)
        self.t = rtl.nco.time(self.fs, n)
        # 生成原始信号
        self.x = rtl.nco.cos(1.0, np.arange(self.f_signal, self.fs / 2, self.f_step).reshape(-1, 1), self.t).sum(axis=0)

        if sym_quant:
            scaled_x = rtl.linear_scale(self.x, self.data_width, True)
        else:
            scaled_x = rtl.affine_scale(self.x, self.data_width, True)
        self.q_x = scaled_x.round().astype(np.int16)

        # 生成卷积核
        self.kernel = scipy.signal.firwin(self.taps, self.f_cutoff, fs=self.fs)
        scale_factor = 2 ** (self.weight_width - 1)
        scaled_kernel = self.kernel * scale_factor
        self.q_kernel = scaled_kernel.round().astype(np.int16)
        self.is_kernel_sym = np.all(self.q_kernel == self.q_kernel[::-1]).item()

        # 生成带有附加相移的卷积核
        N_fft = 8192
        h_kernel = scipy.fft.rfft(self.kernel, N_fft)
        freqs = scipy.fft.rfftfreq(N_fft)
        extra_phase = -2 * np.pi * freqs * self.extra_delay_samples
        h_shifted_kernel = h_kernel * np.exp(1j * extra_phase)
        self.shifted_kernel = scipy.fft.irfft(h_shifted_kernel, N_fft)[: self.taps]
        scaled_shifted_kernel = self.shifted_kernel * scale_factor
        self.q_shifted_kernel = scaled_shifted_kernel.round().astype(np.int16)
        self.is_shifted_kernel_sym = np.all(self.q_shifted_kernel == self.q_shifted_kernel[::-1]).item()

        # 进行FIR滤波
        self.y = np.convolve(self.x, self.kernel)[:n]
        self.shifted_y = np.convolve(self.x, self.shifted_kernel)[:n]
        scaled_y: np.ndarray = np.convolve(scaled_x, scaled_kernel)[:n]
        scaled_shifted_y: np.ndarray = np.convolve(scaled_x, scaled_shifted_kernel)[:n]
        scaled_y /= scale_factor
        scaled_shifted_y /= scale_factor
        q_y_min, q_y_max = rtl.get_range(self.data_width, True)
        self.q_y = scaled_y.round().clip(q_y_min, q_y_max).astype(np.int16)
        self.q_shifted_y = scaled_shifted_y.round().clip(q_y_min, q_y_max).astype(np.int16)

    def visualize(self, output_dir: Path | None = None, optimize_svg: bool | None = None, jobs: int | None = None) -> None:
        """将仿真结果可视化

        Args:
            output_dir (Path | None, optional): 输出目录
            optimize_svg (bool | None, optional): 优化svg输出
            jobs (int | None, optional): 并发任务数
        """
        rtl.set_matplotlib_font()
        if output_dir is None:
            output_dir = config.data_output_dir

        print(f"冲激响应对称：{self.is_kernel_sym}")
        print(f"带附加相移的冲激响应对称：{self.is_shifted_kernel_sym}")

        with rtl.process_figure(optimize_svg, jobs) as proc:

            def visualize_signal(quant: bool, shifted: bool) -> None:
                with_shift = "带有附加相移的" if shifted else ""
                name = "量化信号" if quant else "未量化信号"
                name = f"{with_shift}{name}"
                x = self.q_x if quant else self.x
                match quant, shifted:
                    case False, False:
                        k, y = self.kernel, self.y
                    case False, True:
                        k, y = self.shifted_kernel, self.shifted_y
                    case True, False:
                        k, y = self.q_kernel, self.q_y
                    case True, True:
                        k, y = self.q_shifted_kernel, self.q_shifted_y
                with rtl.figure_env(3, 1, (12, 12), True) as (fig, subfigs):
                    # 绘制冲激响应
                    subfig = typing.cast(SubFigure, subfigs[0])
                    axes = subfig.subplots(2, 1)
                    rtl.visualize_waveform(axes[0], np.arange(0, len(k)), k, f"{with_shift}FIR滤波器冲激响应波形图")
                    rtl.visualize_spectrogram_fft(
                        axes[1], sim_env.fs, rtl.padding(k, 1e3), None, False, f"{with_shift}FIR滤波器冲激响应双边幅度谱"
                    )
                    # 绘制原始信号
                    subfig = typing.cast(SubFigure, subfigs[1])
                    axes = subfig.subplots(2, 1)
                    rtl.visualize_waveform(axes[0], self.t, x, "原始信号波形图")
                    rtl.visualize_spectrogram_fft(axes[1], self.fs, x, title="原始信号双边幅度谱")
                    # 绘制滤波后信号
                    subfig = typing.cast(SubFigure, subfigs[2])
                    axes = subfig.subplots(2, 1)
                    rtl.visualize_waveform(axes[0], self.t, y, f"{with_shift}FIR滤波后信号波形图")
                    rtl.visualize_spectrogram_fft(axes[1], self.fs, y, title=f"{with_shift}FIR滤波后信号双边幅度谱")
                    fig.suptitle(name, fontsize="x-large")
                    proc(fig, output_dir / f"{name}.svg")

            visualize_signal(False, False)
            visualize_signal(False, True)
            visualize_signal(True, False)
            visualize_signal(True, True)

    def sv(self, output_dir: Path | None = None) -> None:
        """将配置写入SystemVerilog源文件

        Args:
            output_dir (Path | None, optional): 输出目录
        """
        if output_dir is None:
            output_dir = config.source_output_dir
        source_path = output_dir / "fir_filter_param.sv"
        with rtl.system_verilog(source_path) as sv:
            with sv.package("fir_filter_param"):
                sv.parameter(sv.int_(f"data_width"), self.data_width)
                sv.parameter(sv.int_(f"weight_width"), self.weight_width)
                sv.parameter(sv.int_(f"taps"), self.taps)
                sv.parameter(sv.int_(f"parallel"), self.parallel)
                packed, unpacked = (self.weight_width,), (self.taps,)
                sv.parameter(sv.vector("weights", True, packed, unpacked), self.q_kernel)
                sv.parameter(sv.vector("shifted_weights", True, packed, unpacked), self.q_shifted_kernel)

    def save(self, output_dir: Path | None = None) -> None:
        """将仿真结果保存到文件

        Args:
            output_dir (Path | None, optional): 输出目录
        """
        if output_dir is None:
            output_dir = config.data_output_dir
        output_dir.mkdir(parents=True, exist_ok=True)
        data_path = output_dir / "fir_filter.npz"
        np.savez(data_path, x=self.q_x, y=self.q_y, shifted_y=self.q_shifted_y)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="仿真FIR滤波器", formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    config.register(parser)
    config.setup_argcomplete(parser)
    if not rtl.is_in_notebook():
        args = parser.parse_args()
        config.parse(args)
    env = sim_env()
    env.eval()
    if rtl.is_in_notebook():
        rtl.set_notebook_env()
        env.visualize()
    else:
        env.sv()
        env.save()
        if config.visualize:
            env.visualize()

# %%
