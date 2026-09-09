from .common import basic_config, find_tool, is_in_notebook, logger
from .dsp import (
    affine_scale,
    affine_scale_param,
    check_range,
    clog2,
    get_range,
    in_range,
    linear2dB,
    linear_scale,
    linear_scale_param,
    nco,
    padding,
    width_cast,
)
from .sv import system_verilog, system_verilog_context
from .visualize import (
    create_figures,
    create_subplots,
    figure_env,
    freq_unit,
    process_figure,
    set_matplotlib_font,
    set_notebook_env,
    subplots_env,
    time_unit,
    visualize_spectrogram,
    visualize_spectrogram_fft,
    visualize_spectrogram_rfft,
    visualize_waveform,
)

__version__ = "0.1.0"
__all__ = [
    # common
    "basic_config",
    "find_tool",
    "is_in_notebook",
    "logger",
    # dsp
    "affine_scale",
    "affine_scale_param",
    "check_range",
    "clog2",
    "get_range",
    "in_range",
    "linear2dB",
    "linear_scale",
    "linear_scale_param",
    "nco",
    "padding",
    "width_cast",
    # sv
    "system_verilog",
    "system_verilog_context",
    # visualize
    "create_figures",
    "create_subplots",
    "figure_env",
    "freq_unit",
    "process_figure",
    "set_matplotlib_font",
    "set_notebook_env",
    "subplots_env",
    "time_unit",
    "visualize_spectrogram",
    "visualize_spectrogram_fft",
    "visualize_spectrogram_rfft",
    "visualize_waveform",
]
