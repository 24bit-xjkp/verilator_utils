"""verilator_utils_rtl.visualize模块测试"""

import subprocess
import sys
import types
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Any, cast

import matplotlib
import matplotlib.pyplot as plt
import numpy as np
import pytest
import scipy.fft
import scipy.signal.windows
import verilator_utils_rtl.visualize as viz
from matplotlib.axes import Axes
from matplotlib.figure import Figure, SubFigure
from numpy.typing import NDArray
from verilator_utils_rtl.common import basic_config


def make_axes() -> tuple[Figure, Axes]:
    fig, ax = plt.subplots(figsize=(6, 4))
    return fig, ax


def as_float64(x: object) -> NDArray[np.float64]:
    """将matplotlib返回的数组转换为float64数组以便比较"""
    return np.asarray(cast(Any, x), dtype=np.float64)


class TestTimeUnit:
    def test_values(self) -> None:
        assert float(viz.time_unit.s) == 1.0
        assert float(viz.time_unit.ms) == 1e-3
        assert float(viz.time_unit.us) == 1e-6
        assert float(viz.time_unit.ns) == 1e-9
        assert float(viz.time_unit.ps) == 1e-12
        assert float(viz.time_unit.fs) == 1e-15

    def test_member_is_float(self) -> None:
        assert isinstance(viz.time_unit.ms, float)
        assert viz.time_unit.us.name == "us"

    def test_ordering(self) -> None:
        units = list(viz.time_unit)
        assert all(units[i] > units[i + 1] for i in range(len(units) - 1))


class TestFreqUnit:
    def test_values(self) -> None:
        assert float(viz.freq_unit.THz) == 1e12
        assert float(viz.freq_unit.GHz) == 1e9
        assert float(viz.freq_unit.MHz) == 1e6
        assert float(viz.freq_unit.kHz) == 1e3
        assert float(viz.freq_unit.Hz) == 1.0


class TestSetMatplotlibFont:
    def test_rcparams_updated(self, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.setitem(matplotlib.rcParams, "font.serif", "old-serif")
        monkeypatch.setitem(matplotlib.rcParams, "font.sans-serif", "old-sans")
        monkeypatch.setitem(matplotlib.rcParams, "axes.unicode_minus", True)
        viz.set_matplotlib_font()
        # matplotlib会把单个字符串字体配置规范化为列表
        assert matplotlib.rcParams["font.serif"] == ["Noto Serif CJK SC"]
        assert matplotlib.rcParams["font.sans-serif"] == ["Noto Sans CJK SC"]
        assert matplotlib.rcParams["axes.unicode_minus"] is False


class TestSetNotebookEnv:
    def test_requires_notebook(self, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.setattr(viz, "is_in_notebook", lambda: False)
        with pytest.raises(AssertionError, match="必须在Jupyter Notebook环境中运行"):
            viz.set_notebook_env()

    def test_apply_magics(self, monkeypatch: pytest.MonkeyPatch) -> None:
        calls: list[tuple[str, str]] = []

        class FakeIP:
            def run_line_magic(self, name: str, line: str) -> None:
                calls.append((name, line))

        fake_module = types.ModuleType("IPython.core.getipython")
        setattr(fake_module, "get_ipython", lambda: FakeIP())
        monkeypatch.setitem(sys.modules, "IPython.core.getipython", fake_module)
        monkeypatch.setattr(viz, "is_in_notebook", lambda: True)
        viz.set_notebook_env()
        assert calls == [("matplotlib", "inline"), ("config", "InlineBackend.figure_format = 'svg'")]


class TestCreateSubplots:
    def test_single_squeeze(self) -> None:
        fig, ax = viz.create_subplots(1, 1, (6, 4))
        assert isinstance(ax, Axes)
        assert isinstance(fig, Figure)
        np.testing.assert_allclose(fig.get_size_inches(), (6, 4))

    def test_grid(self) -> None:
        fig, axes = viz.create_subplots(2, 3, (2.0, 1.5), squeeze=True)
        assert axes.shape == (2, 3)
        np.testing.assert_allclose(fig.get_size_inches(), (6.0, 3.0))
        for ax in axes.flat:
            assert isinstance(ax, Axes)

    def test_no_squeeze(self) -> None:
        _, axes = viz.create_subplots(1, 1, (6, 4), squeeze=False)
        assert axes.shape == (1, 1)

    def test_kwargs_forwarded(self) -> None:
        _, ax = viz.create_subplots(1, 1, (6, 4), sharex=True)
        # 只验证不会因额外参数报错，且返回有效对象
        assert ax is not None


class TestSubplotsEnv:
    def test_yields_and_closes(self) -> None:
        with viz.subplots_env(1, 1, (6, 4)) as (fig, ax):
            assert isinstance(ax, Axes)
            assert plt.fignum_exists(fig.number)
        assert not plt.fignum_exists(fig.number)

    def test_closes_on_exception(self) -> None:
        fig_number: int | str = 0
        with pytest.raises(RuntimeError):
            with viz.subplots_env(1, 1, (6, 4)) as (fig, _):
                assert plt.fignum_exists(fig_number := fig.number)
                raise RuntimeError
        assert not plt.fignum_exists(fig_number)


class TestCreateFigures:
    def test_single_subfigure(self) -> None:
        fig, sub = viz.create_figures(1, 1, (6, 4))
        assert isinstance(sub, SubFigure)
        assert isinstance(fig, Figure)

    def test_grid_subfigures(self) -> None:
        fig, subs = viz.create_figures(2, 2, (8, 6), squeeze=True)
        assert subs.shape == (2, 2)
        assert all(isinstance(s, SubFigure) for s in subs.flat)

    def test_figure_kwargs(self) -> None:
        fig, _ = viz.create_figures(1, 1, (6, 4), figure_kwargs={"facecolor": "red"})
        assert fig.get_facecolor() == (1.0, 0.0, 0.0, 1.0)


class TestFigureEnv:
    def test_yields_and_closes(self) -> None:
        with viz.figure_env(1, 1, (6, 4)) as (fig, sub):
            assert isinstance(sub, SubFigure)
            assert plt.fignum_exists(fig.number)
        assert not plt.fignum_exists(fig.number)

    def test_closes_on_exception(self) -> None:
        fig_number: int | str = 0
        with pytest.raises(RuntimeError):
            with viz.figure_env(2, 1, (6, 4), True) as (fig, _):
                assert plt.fignum_exists(fig_number := fig.number)
                raise RuntimeError
        assert not plt.fignum_exists(fig_number)


class TestAsDiscrete:
    def test_auto_derivation(self) -> None:
        assert viz.as_discrete(100, None) is True
        assert viz.as_discrete(101, None) is False
        assert viz.as_discrete(1, None) is True

    def test_explicit(self) -> None:
        assert viz.as_discrete(500, True) is True
        assert viz.as_discrete(3, False) is False


class TestVisualizeWaveform:
    def test_plot_path_with_unit_inference(self) -> None:
        _, ax = make_axes()
        t = np.linspace(0.0, 1e-3, 101)  # t[-1]=1ms -> 自动选择us单位
        x = np.cos(2 * np.pi * 1000 * t)
        viz.visualize_waveform(ax, t, x)
        assert len(ax.lines) == 1
        np.testing.assert_allclose(as_float64(ax.lines[0].get_xdata()), t / 1e-6)
        np.testing.assert_allclose(as_float64(ax.lines[0].get_ydata()), x)
        assert ax.get_xlabel() == f"时间 ({R'$\mathrm{\mu s}$'})"
        assert ax.get_ylabel() == "幅度"
        assert ax.get_title() == "波形图"

    def test_ms_unit_inference(self) -> None:
        _, ax = make_axes()
        t = np.linspace(0.0, 4e-3, 101)
        viz.visualize_waveform(ax, t, np.sin(t))
        assert ax.get_xlabel() == "时间 (ms)"

    def test_second_unit_inference(self) -> None:
        _, ax = make_axes()
        t = np.linspace(0.0, 9.0, 101)
        viz.visualize_waveform(ax, t, np.sin(t))
        assert ax.get_xlabel() == "时间 (s)"

    def test_explicit_unit(self) -> None:
        _, ax = make_axes()
        t = np.linspace(0.0, 9.0, 101)
        viz.visualize_waveform(ax, t, np.sin(t), unit=viz.time_unit.ms)
        np.testing.assert_allclose(as_float64(ax.lines[0].get_xdata()), t / 1e-3)
        assert ax.get_xlabel() == "时间 (ms)"

    def test_custom_title(self) -> None:
        _, ax = make_axes()
        t = np.arange(101, dtype=float)
        viz.visualize_waveform(ax, t, t, title="自定义标题")
        assert ax.get_title() == "自定义标题"

    def test_stem_dispatch_for_short_signal(self, monkeypatch: pytest.MonkeyPatch) -> None:
        _, ax = make_axes()
        called: list[tuple[object, ...]] = []
        monkeypatch.setattr(ax, "stem", lambda *args, **kwargs: called.append(args))
        t = np.arange(8.0)
        viz.visualize_waveform(ax, t, t)
        assert len(called) == 1
        np.testing.assert_allclose(as_float64(called[0][0]), t)
        np.testing.assert_allclose(as_float64(called[0][1]), t)

    def test_plot_dispatch_for_long_signal(self, monkeypatch: pytest.MonkeyPatch) -> None:
        _, ax = make_axes()
        called: list[tuple[object, ...]] = []
        monkeypatch.setattr(ax, "plot", lambda *args, **kwargs: called.append(args))
        t = np.arange(101.0)
        viz.visualize_waveform(ax, t, t, color="red")
        assert len(called) == 1

    def test_kwargs_forwarded_to_plot(self, monkeypatch: pytest.MonkeyPatch) -> None:
        _, ax = make_axes()
        kwargs: dict[str, object] = {}
        monkeypatch.setattr(ax, "plot", lambda *args, **kw: kwargs.update(kw))
        t = np.arange(101.0)
        viz.visualize_waveform(ax, t, t, color="red", marker="o")
        assert kwargs == {"color": "red", "marker": "o"}

    def test_explicit_discrete_stem(self, monkeypatch: pytest.MonkeyPatch) -> None:
        _, ax = make_axes()
        called: list[tuple[object, ...]] = []
        monkeypatch.setattr(ax, "stem", lambda *args, **kwargs: called.append(args))
        t = np.arange(200.0)
        viz.visualize_waveform(ax, t, t, discrete=True)
        assert len(called) == 1

    def test_length_mismatch_raises(self) -> None:
        _, ax = make_axes()
        with pytest.raises(AssertionError, match="长度"):
            viz.visualize_waveform(ax, np.arange(10.0), np.arange(9.0))

    def test_ndim_raises(self) -> None:
        _, ax = make_axes()
        with pytest.raises(AssertionError, match="只接受一维数组"):
            viz.visualize_waveform(ax, np.zeros((2, 2)), np.zeros((2, 2)))


class TestVisualizeSpectrogram:
    def test_double_sided_fftshift(self) -> None:
        _, ax = make_axes()
        f = scipy.fft.fftfreq(128, 1 / 1000.0)
        x = np.linspace(1.0, 2.0, 128)
        viz.visualize_spectrogram(ax, f, x)
        assert ax.get_title() == "双边功率谱"
        shifted_f = scipy.fft.fftshift(f)
        shifted_x = scipy.fft.fftshift(np.abs(x))
        np.testing.assert_allclose(as_float64(ax.lines[0].get_xdata()), shifted_f)
        np.testing.assert_allclose(as_float64(ax.lines[0].get_ydata()), shifted_x)
        assert ax.get_xlabel() == "频率 (Hz)"

    def test_single_sided_no_shift(self) -> None:
        _, ax = make_axes()
        f = np.linspace(0.0, 400.0, 128)
        x = np.linspace(1.0, 2.0, 128)
        viz.visualize_spectrogram(ax, f, x)
        assert ax.get_title() == "单边功率谱"
        np.testing.assert_allclose(as_float64(ax.lines[0].get_ydata()), x)
        assert ax.get_ylabel() == "幅度"

    def test_magnitude_applied(self) -> None:
        _, ax = make_axes()
        f = np.linspace(0.0, 400.0, 128)
        x = -np.linspace(1.0, 2.0, 128)  # 负数会先取绝对值
        viz.visualize_spectrogram(ax, f, x)
        np.testing.assert_allclose(as_float64(ax.lines[0].get_ydata()), np.abs(x))

    def test_db_mode_max_is_zero(self) -> None:
        _, ax = make_axes()
        f = np.linspace(0.0, 400.0, 128)
        x = np.linspace(1.0, 2.0, 128)
        viz.visualize_spectrogram(ax, f, x, linear=False)
        assert ax.get_ylabel() == "dB"
        expected = 10 * np.log10(x / np.max(x))
        np.testing.assert_allclose(as_float64(ax.lines[0].get_ydata()), expected)

    def test_explicit_unit(self) -> None:
        _, ax = make_axes()
        f = np.linspace(0.0, 400.0, 128)
        viz.visualize_spectrogram(ax, f, np.ones(128), unit=viz.freq_unit.kHz)
        assert ax.get_xlabel() == "频率 (kHz)"

    def test_length_mismatch_raises(self) -> None:
        _, ax = make_axes()
        with pytest.raises(AssertionError, match="长度"):
            viz.visualize_spectrogram(ax, np.arange(10.0), np.arange(9.0))

    def test_stem_dispatch_short(self, monkeypatch: pytest.MonkeyPatch) -> None:
        _, ax = make_axes()
        called: list[tuple[object, ...]] = []
        monkeypatch.setattr(ax, "stem", lambda *args, **kwargs: called.append(args))
        f = np.linspace(0.0, 100.0, 8)
        viz.visualize_spectrogram(ax, f, np.ones(8))
        assert len(called) == 1


class TestGetWindowArray:
    def test_none(self) -> None:
        assert viz.get_window_array(10, None) is None

    def test_array_passthrough(self) -> None:
        window = np.ones(5)
        assert viz.get_window_array(5, window) is window

    def test_callable_invoked(self) -> None:
        calls: list[int] = []

        def win(n: int) -> NDArray[np.float64]:
            calls.append(n)
            return np.zeros(n)

        result = viz.get_window_array(7, win)
        assert calls == [7]
        assert result is not None
        assert len(result) == 7


class TestVisualizeSpectrogramFft:
    def test_default_window(self) -> None:
        _, ax = make_axes()
        fs = 1000.0
        x = np.cos(2 * np.pi * 100 * np.arange(128) / fs)
        spectrogram, scaled = viz.visualize_spectrogram_fft(ax, fs, x)
        assert spectrogram.dtype == np.complex128
        window_arr = scipy.signal.windows.hann(128)
        np.testing.assert_allclose(spectrogram, scipy.fft.fft(x * window_arr))
        np.testing.assert_allclose(scaled, spectrogram / np.sum(window_arr))

    def test_no_window(self) -> None:
        _, ax = make_axes()
        fs = 1000.0
        x = np.arange(128, dtype=float)
        spectrogram, scaled = viz.visualize_spectrogram_fft(ax, fs, x, window=None)
        np.testing.assert_allclose(spectrogram, scipy.fft.fft(x))
        np.testing.assert_allclose(scaled, scipy.fft.fft(x) / 128)

    def test_custom_window_callable(self) -> None:
        _, ax = make_axes()
        fs = 1000.0
        x = np.arange(64, dtype=float)
        calls: list[int] = []

        def window(n: int) -> NDArray[np.float64]:
            calls.append(n)
            return np.full(n, 2.0)

        _, scaled = viz.visualize_spectrogram_fft(ax, fs, x, window=window)
        assert calls == [64]
        np.testing.assert_allclose(scaled, scipy.fft.fft(x * 2.0) / (64 * 2.0))

    def test_complex_input_ok(self) -> None:
        _, ax = make_axes()
        x = np.exp(2j * np.pi * 0.1 * np.arange(64))
        spectrogram, _ = viz.visualize_spectrogram_fft(ax, 1000.0, x)
        np.testing.assert_allclose(spectrogram, scipy.fft.fft(x * scipy.signal.windows.hann(64)))

    def test_title_and_axis(self) -> None:
        _, ax = make_axes()
        x = np.arange(128, dtype=float)
        viz.visualize_spectrogram_fft(ax, 1000.0, x)
        assert ax.get_title() == "双边幅度谱"
        assert ax.get_ylabel() == "幅度"


class TestVisualizeSpectrogramRfft:
    def test_even_length(self) -> None:
        _, ax = make_axes()
        fs = 1000.0
        n = 100
        x = np.cos(2 * np.pi * 50 * np.arange(n) / fs)
        _, scaled = viz.visualize_spectrogram_rfft(ax, fs, x)
        window_arr = scipy.signal.windows.hann(n)
        expected = scipy.fft.rfft(x * window_arr)
        expected /= np.sum(window_arr)
        expected[1:-1] *= 2
        np.testing.assert_allclose(scaled, expected)
        assert ax.get_title() == "单边幅度谱"

    def test_odd_length(self) -> None:
        _, ax = make_axes()
        n = 101
        x = np.arange(n, dtype=float)
        _, scaled = viz.visualize_spectrogram_rfft(ax, 1000.0, x)
        expected = scipy.fft.rfft(x * scipy.signal.windows.hann(n))
        expected = expected / np.sum(scipy.signal.windows.hann(n))
        expected[1:] *= 2
        np.testing.assert_allclose(scaled, expected)

    def test_no_window_even(self) -> None:
        _, ax = make_axes()
        n = 10
        x = np.arange(n, dtype=float)
        _, scaled = viz.visualize_spectrogram_rfft(ax, 1000.0, x, window=None)
        expected = scipy.fft.rfft(x) / n
        expected[1:-1] *= 2
        np.testing.assert_allclose(scaled, expected)

    def test_complex_input_raises(self) -> None:
        _, ax = make_axes()
        x = np.full(8, 1 + 2j)
        with pytest.raises(AssertionError, match="RFFT只接受实信号"):
            viz.visualize_spectrogram_rfft(ax, 1000.0, x)


class TestFigureProcessor:
    def test_save_without_pool(self, tmp_path: Path) -> None:
        fig = plt.figure()
        proc = viz.figure_processor(None)
        svg_path = tmp_path / "out.svg"
        proc(fig, svg_path)
        assert svg_path.exists()
        assert svg_path.stat().st_size > 0
        png_path = tmp_path / "out.png"
        proc(fig, png_path)
        assert png_path.exists()

    def test_show_in_notebook(self, monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
        monkeypatch.setattr(viz, "is_in_notebook", lambda: True)
        shown: list[Figure] = []

        def fake_show(fig: Figure) -> None:
            shown.append(fig)

        monkeypatch.setattr(plt, "show", fake_show)
        fig = plt.figure()
        out = tmp_path / "out.svg"
        proc = viz.figure_processor(None)
        proc(fig, out)
        assert shown == [fig]
        assert not out.exists()

    def test_warns_when_svgo_missing(self, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
        monkeypatch.setattr(viz, "find_tool", lambda _: None)
        with ThreadPoolExecutor(1) as pool:
            viz.figure_processor(pool)
        assert "未找到svgo，跳过svg图片优化" in capsys.readouterr().out

    def test_optimize_svg_submits_svgo(self, monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
        fake_svgo = Path("/bin/true")
        monkeypatch.setattr(viz, "find_tool", lambda _: fake_svgo)
        calls: list[tuple[tuple[object, ...], dict[str, object]]] = []

        def fake_run(*args: object, **kwargs: object) -> None:
            calls.append((args, kwargs))

        monkeypatch.setattr(subprocess, "run", fake_run)
        fig = plt.figure()
        out = tmp_path / "out.svg"
        pool = ThreadPoolExecutor(1)
        try:
            proc = viz.figure_processor(pool)
            proc(fig, out)
        finally:
            pool.shutdown(wait=True)
        assert out.exists()
        assert len(calls) == 1
        assert calls[0][0] == ([fake_svgo, out],)
        assert calls[0][1] == {"stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL}

    def test_png_not_optimized(self, monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
        monkeypatch.setattr(viz, "find_tool", lambda _: Path("/bin/true"))
        calls: list[tuple[tuple[object, ...], dict[str, object]]] = []

        def fake_run(*args: object, **kwargs: object) -> None:
            calls.append((args, kwargs))

        monkeypatch.setattr(subprocess, "run", fake_run)
        fig = plt.figure()
        out = tmp_path / "out.png"
        pool = ThreadPoolExecutor(1)
        try:
            proc = viz.figure_processor(pool)
            proc(fig, out)
        finally:
            pool.shutdown(wait=True)
        assert calls == []


class TestProcessFigure:
    def test_no_optimize(self) -> None:
        with viz.process_figure(optimize_svg=False) as proc:
            assert getattr(proc, "_figure_processor__pool") is None

    def test_optimize_creates_pool(self, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.setattr(viz, "find_tool", lambda _: None)
        with viz.process_figure(optimize_svg=True, jobs=2) as proc:
            pool = getattr(proc, "_figure_processor__pool")
            assert pool is not None
            assert pool._max_workers == 2

    def test_defaults_from_config(self, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.setattr(viz, "find_tool", lambda _: None)
        monkeypatch.setattr(basic_config, "optimize_svg", True)
        monkeypatch.setattr(basic_config, "jobs", 3)
        with viz.process_figure() as proc:
            pool = getattr(proc, "_figure_processor__pool")
            assert pool is not None
            assert pool._max_workers == 3
        monkeypatch.setattr(basic_config, "optimize_svg", False)
        with viz.process_figure() as proc:
            assert getattr(proc, "_figure_processor__pool") is None
