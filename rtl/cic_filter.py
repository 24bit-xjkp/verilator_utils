#!/usr/bin/env python
# PYTHON_ARGCOMPLETE_OK
# %%
import argparse
import typing
from pathlib import Path
from typing import Final

import numpy as np
import verilator_utils_rtl as rtl
from matplotlib.figure import SubFigure
from numpy.typing import NDArray

config = rtl.basic_config


class sim_env:
    # ---- 仿真参数 ----

    # 信号宽度
    width: Final = 16
    # 抽取速率
    rate: Final = 20
    # CIC滤波器级联数
    n: Final = 5
    # 采样率
    fs: Final = 500e6  # 500MHz
    # 抽取后采样率
    fs_d: Final = fs / rate
    # 信号基波频率
    f_signal: Final = 1e6  # 1MHz
    # 信号频率步进
    f_step: Final = 5e6  # 5MHz
    # 信号基波周期数
    cycle: Final = 30
    # 使用对称量化
    sym_quant: Final = True

    # ---- 仿真数据 ----

    # 原始时间数组
    t: NDArray[np.float64]
    # 原始信号数组
    x: NDArray[np.float64]
    # CIC滤波器冲激响应，等效FIR滤波器卷积核
    kernel: NDArray[np.float64]
    # 直流增益
    dc_gain: float
    # 抽取后时间数组
    t_d: NDArray[np.float64]
    # CIC滤波后的信号数组
    y: NDArray[np.float64]
    # 量化后的原始信号数组
    q_x: NDArray[np.int16]
    # 量化后的CIC滤波后信号数组
    q_y: NDArray[np.int16]

    def eval(self, sym_quant: bool | None = None) -> None:
        """执行仿真过程

        Args:
            sym_quant (bool | None, optional): 使用对称量化，为None使用运行环境中配置的默认参数
        """
        if sym_quant is None:
            sym_quant = self.sym_quant if rtl.is_in_notebook() else config.sym_quant

        # 生成原始时间序列
        n: Final = self.fs / self.f_signal * self.cycle
        self.t = rtl.nco.time(self.fs, n)
        # 生成原始信号
        self.x = rtl.nco.cos(1.0, self.f_signal, self.t)
        for f in np.arange(self.f_step, self.fs_d, self.f_step):
            self.x += rtl.nco.cos(1.0, f, self.t)
        for f in np.arange(self.fs_d, self.fs / 2, self.f_step):
            self.x += rtl.nco.cos((self.fs_d / f) ** 2 / 2, f, self.t)

        if sym_quant:
            scaled_x = rtl.linear_scale(self.x, self.width, True)
        else:
            scaled_x = rtl.affine_scale(self.x, self.width, True)
        self.q_x = scaled_x.round().astype(np.int16)

        # 生成等效卷积核
        self.kernel = np.ones(1)
        for _ in range(self.n):
            self.kernel = np.convolve(self.kernel, np.ones(self.rate))
        norm_factor = self.rate**self.n
        # 采用逐级截断处理舍入
        kernel_factor: int = 2 ** rtl.clog2(norm_factor)
        self.kernel /= kernel_factor
        self.dc_gain = norm_factor / kernel_factor

        # 进行CIC滤波
        begin = self.rate - 1
        end = begin + len(self.x)
        self.y = np.convolve(self.x, self.kernel)[begin : end : self.rate]
        scaled_y = np.convolve(scaled_x, self.kernel)[begin : end : self.rate]
        self.q_y = scaled_y.round().astype(np.int16)
        # 对时间数组进行抽取
        self.t_d = self.t[begin : end : self.rate]

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

        # 报告直流增益
        rtl.logger.info(f"CIC滤波器直流增益: {self.dc_gain:.4f} ({rtl.linear2dB(self.dc_gain):.3f}dB)")

        with rtl.process_figure(optimize_svg, jobs) as proc:
            # 绘制冲激响应
            with rtl.subplots_env(2, 1, (12, 2), True) as (fig, axes):
                rtl.visualize_waveform(axes[0], np.arange(0, len(self.kernel)), self.kernel)
                rtl.visualize_spectrogram_fft(axes[1], sim_env.fs, rtl.padding(self.kernel, 1e3), None, False)
                fig.suptitle("CIC滤波器冲激响应")
                proc(fig, output_dir / "冲激响应.svg")

            def visualize_signal(quant: bool) -> None:
                name = "量化信号" if quant else "未量化信号"
                x, y = (self.q_x, self.q_y) if quant else (self.x, self.y)
                with rtl.figure_env(2, 1, (12, 8), True) as (fig, subfigs):
                    # 绘制原始信号
                    subfig = typing.cast(SubFigure, subfigs[0])
                    axes = subfig.subplots(2, 1)
                    rtl.visualize_waveform(axes[0], self.t, x, "原始信号波形图")
                    rtl.visualize_spectrogram_fft(axes[1], self.fs, x, title="原始信号双边功率谱")
                    # 绘制滤波后信号
                    subfig = typing.cast(SubFigure, subfigs[1])
                    axes = subfig.subplots(2, 1)
                    rtl.visualize_waveform(axes[0], self.t_d, y, "CIC滤波后信号波形图")
                    rtl.visualize_spectrogram_fft(axes[1], self.fs_d, y, title="CIC滤波后信号双边功率谱")
                    fig.suptitle(name, fontsize="x-large")
                    proc(fig, output_dir / f"{name}.svg")

            visualize_signal(False)
            visualize_signal(True)

    @classmethod
    def sv(cls, output_dir: Path | None = None) -> None:
        """将配置写入SystemVerilog源文件

        Args:
            output_dir (Path | None, optional): 输出目录
        """
        if output_dir is None:
            output_dir = config.source_output_dir
        source_path = output_dir / "cic_filter_param.sv"
        with rtl.system_verilog(source_path) as sv:
            with sv.package("cic_filter_param"):
                sv.parameter(sv.int_(f"default_width"), cls.width)
                sv.parameter(sv.int_(f"default_rate"), cls.rate)
                sv.parameter(sv.int_(f"default_n"), cls.n)

    def save(self, output_dir: Path | None = None) -> None:
        """将仿真结果保存到文件

        Args:
            output_dir (Path | None, optional): 输出目录
        """
        if output_dir is None:
            output_dir = config.data_output_dir
        output_dir.mkdir(parents=True, exist_ok=True)
        data_path = output_dir / "cic_filter.npz"
        np.savez(data_path, x=self.q_x, y=self.q_y)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="仿真CIC滤波器", formatter_class=argparse.ArgumentDefaultsHelpFormatter)
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
