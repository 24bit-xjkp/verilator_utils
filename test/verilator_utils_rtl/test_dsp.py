"""verilator_utils_rtl.dsp模块测试"""

import math

import numpy as np
import pytest
from verilator_utils_rtl.dsp import (
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


class TestGetRange:
    def test_signed(self) -> None:
        assert get_range(8, True) == (-128, 127)
        assert get_range(1, True) == (-1, 0)
        assert get_range(16, True) == (-32768, 32767)

    def test_unsigned(self) -> None:
        assert get_range(8, False) == (0, 255)
        assert get_range(1, False) == (0, 1)


class TestAffineScaleParam:
    def test_basic(self) -> None:
        x = np.array([1.0, 3.0])
        factor, bias = affine_scale_param(x, 8, True, guard=0.0)
        assert factor == pytest.approx(127.5)
        assert bias == pytest.approx(-255.0)

    def test_default_guard(self) -> None:
        x = np.array([1.0, 3.0])
        factor, bias = affine_scale_param(x, 8, True)
        # 默认guard=0.01，缩放因子乘上(1-guard)
        assert factor == pytest.approx(255 / 2 * 0.99)
        # 缩放后的数据应当留出guard保护空间
        y = x * factor + bias
        assert np.all(y >= -128 * (1 - 0.01) - 1)
        assert np.all(y <= 127 * (1 - 0.01) + 1)

    def test_unsigned_zero_point(self) -> None:
        x = np.array([1.0, 3.0])
        factor, bias = affine_scale_param(x, 8, False, guard=0.1)
        # y_zero_point=128, x_zero_point=2, y_ptp=255: factor=255/2*0.9, bias=128-2*114.75
        assert factor == pytest.approx(114.75)
        assert bias == pytest.approx(-101.5)
        y = x * factor + bias
        assert np.all(y >= 0)
        assert np.all(y <= 255)

    def test_constant_input(self) -> None:
        x = np.full(4, 5.0)
        assert affine_scale_param(x, 8, True) == (0.0, 0.0)
        assert affine_scale_param(x, 8, False) == (0.0, 128.0)

    def test_invalid_width(self) -> None:
        with pytest.raises(AssertionError, match="信号位宽必须为正数"):
            affine_scale_param(np.array([1.0]), 0, True)

    @pytest.mark.parametrize("guard", [1.0, -0.1, 1.5])
    def test_invalid_guard(self, guard: float) -> None:
        with pytest.raises(AssertionError, match="保护空间"):
            affine_scale_param(np.array([1.0]), 8, True, guard=guard)


class TestAffineScale:
    def test_param_mode_equals_param_mode_result(self) -> None:
        x = np.array([-3.0, 1.0, 2.0, 5.0])
        factor, bias = affine_scale_param(x, 12, True)
        np.testing.assert_allclose(affine_scale(x, 12, True), x * factor + bias)

    def test_unsigned_output_in_range(self) -> None:
        x = np.array([-2.0, 0.0, 3.0, 10.0])
        y = affine_scale(x, 10, False)
        assert y.dtype == np.float64
        assert np.all(y >= 0)
        assert np.all(y <= 1023)

    def test_direct_factor_bias(self) -> None:
        x = np.array([1.0, 2.0, 3.0])
        np.testing.assert_allclose(affine_scale(x, 2.5, -1.0), x * 2.5 - 1.0)
        np.testing.assert_allclose(affine_scale(x, 2, 3), x * 2 + 3)

    def test_width_must_be_int(self) -> None:
        with pytest.raises(AssertionError, match="信号宽度必须是整数"):
            affine_scale(np.array([1.0]), 8.0, True)

    def test_bias_must_be_number(self) -> None:
        with pytest.raises(AssertionError, match="偏置必须是整数或浮点数"):
            affine_scale(np.array([1.0]), 8.0, "bias")  # type: ignore[call-overload]


class TestLinearScaleParam:
    def test_signed_balanced(self) -> None:
        x = np.array([-4.0, 4.0])
        assert linear_scale_param(x, 8, True, guard=0.0) == pytest.approx(31.75)
        y = x * 31.75
        assert np.all(y >= -128)
        assert np.all(y <= 127)

    def test_signed_unbalanced_uses_min_factor(self) -> None:
        # 缩放因子取两个方向因子的较小者：min(-128/-8, 127/4) = min(16, 31.75)
        x = np.array([-8.0, 4.0])
        factor = linear_scale_param(x, 8, True, guard=0.0)
        assert factor == pytest.approx(min(16.0, 31.75))
        y = x * factor
        assert np.all(y >= -128)
        assert np.all(y <= 127)

    def test_signed_same_sign_returns_none(self) -> None:
        assert linear_scale_param(np.array([2.0, 4.0]), 8, True) is None
        assert linear_scale_param(np.array([-4.0, -2.0]), 8, True) is None

    def test_signed_positive_only_returns_none(self) -> None:
        # 输入不跨越零点时，无偏置的线性变换无法映射到有符号范围
        assert linear_scale_param(np.array([0.0, 4.0]), 8, True) is None

    def test_all_zero(self) -> None:
        # 无符号输入全零可以缩放到0
        assert linear_scale_param(np.zeros(3), 8, False) == 0.0
        # 有符号输入全零无法求解（符号检查先返回None）
        assert linear_scale_param(np.zeros(3), 8, True) is None

    def test_unsigned(self) -> None:
        x = np.array([2.0, 4.0])
        assert linear_scale_param(x, 8, False, guard=0.0) == pytest.approx(63.75)

    def test_unsigned_mixed_sign_returns_none(self) -> None:
        assert linear_scale_param(np.array([-1.0, 1.0]), 8, False) is None

    def test_unsigned_all_negative_uses_negative_factor(self) -> None:
        x = np.array([-8.0, -2.0])
        factor = linear_scale_param(x, 8, False, guard=0.0)
        assert factor == pytest.approx(-31.875)
        assert np.all(x * factor >= 0)
        assert np.all(x * factor <= 255)

    def test_guard_scales_factor(self) -> None:
        # guard仅以(1-guard)的形式乘到因子上
        x = np.array([-4.0, 4.0])
        factor_zero_guard = linear_scale_param(x, 8, True, guard=0.0)
        factor_half_guard = linear_scale_param(x, 8, True, guard=0.5)
        assert factor_zero_guard == pytest.approx(31.75)
        assert factor_half_guard == pytest.approx(31.75 * 0.5)


class TestLinearScale:
    def test_width_mode(self) -> None:
        x = np.array([-4.0, 4.0])
        y = linear_scale(x, 8, True)
        assert np.all(y >= -128)
        assert np.all(y <= 127)

    def test_width_mode_impossible_raises(self) -> None:
        # 无符号数据跨越正负，无法找到满足条件的线性因子
        with pytest.raises(AssertionError, match="无法找到满足约束条件的缩放因子"):
            linear_scale(np.array([-1.0, 1.0]), 8, False)

    def test_factor_mode(self) -> None:
        x = np.array([1.0, 2.0, 3.0])
        np.testing.assert_allclose(linear_scale(x, 2.0), x * 2.0)


class TestWidthCast:
    def test_truncate_to_width(self) -> None:
        x = np.array([5, 255, 256, 300, -1], dtype=np.int64)
        y = width_cast(x, 8)
        assert y.dtype == np.uint64
        np.testing.assert_array_equal(y, np.array([5, 255, 0, 44, 255], dtype=np.uint64))

    def test_small_widths(self) -> None:
        x = np.array([-1, 1, 2, 15], dtype=np.int64)
        np.testing.assert_array_equal(width_cast(x, 4), np.array([15, 1, 2, 15], dtype=np.uint64))
        np.testing.assert_array_equal(width_cast(x, 1), np.array([1, 1, 0, 1], dtype=np.uint64))

    def test_width_64(self) -> None:
        y = width_cast(np.array([-1], dtype=np.int64), 64)
        assert int(y[0]) == 2**64 - 1

    def test_in_range_untouched(self) -> None:
        x = np.array([3, 7], dtype=np.int64)
        np.testing.assert_array_equal(width_cast(x, 8), x.astype(np.uint64))

    def test_invalid_width(self) -> None:
        with pytest.raises(AssertionError, match="信号位宽必须为正数"):
            width_cast(np.array([1], dtype=np.int64), 0)


class TestInRange:
    @pytest.mark.parametrize("width,signed", [(4, True), (4, False), (8, True), (8, False)])
    def test_boundaries(self, width: int, signed: bool) -> None:
        x_min, x_max = get_range(width, signed)
        assert in_range(x_min, width, signed) is True
        assert in_range(x_max, width, signed) is True
        assert in_range(x_min - 1, width, signed) is False
        assert in_range(x_max + 1, width, signed) is False

    def test_float_scalar(self) -> None:
        assert in_range(0.5, 4, False) is True
        assert in_range(-0.5, 4, False) is False

    def test_array(self) -> None:
        mask = in_range(np.array([-8, 0, 7], dtype=np.int64), 4, True)
        assert np.asarray(mask).dtype == np.dtype(bool)
        assert np.all(mask)
        mixed = in_range(np.array([-8, 9], dtype=np.int64), 4, True)
        np.testing.assert_array_equal(mixed, np.array([True, False]))


class TestCheckRange:
    def test_in_range_passes(self) -> None:
        check_range(100, 8, True)  # 不抛出
        check_range(np.array([0, 255], dtype=np.int64), 8, False)

    def test_scalar_out_of_range(self) -> None:
        with pytest.raises(AssertionError, match=r"300超出uint8范围"):
            check_range(300, 8, False)
        with pytest.raises(AssertionError, match=r"-9超出int4范围"):
            check_range(-9, 4, True)

    def test_array_out_of_range(self) -> None:
        with pytest.raises(AssertionError, match="超出uint8范围"):
            check_range(np.array([1, 300], dtype=np.int64), 8, False)


class TestNcoTime:
    def test_basic(self) -> None:
        t = nco.time(10, 3)
        assert t.shape == (3,)
        assert t.dtype == np.float64
        np.testing.assert_allclose(t, [0.0, 0.1, 0.2])

    def test_t0(self) -> None:
        np.testing.assert_allclose(nco.time(10, 3, t0=1.0), [1.0, 1.1, 1.2])

    def test_float_n_is_rounded(self) -> None:
        t = nco.time(10, 4.6)
        assert len(t) == 5
        np.testing.assert_allclose(t, np.arange(5) / 10.0)

    def test_single_sample(self) -> None:
        np.testing.assert_allclose(nco.time(10, 1, t0=2.0), [2.0])

    @pytest.mark.parametrize("n", [0, -3])
    def test_non_positive_n_raises(self, n: int) -> None:
        with pytest.raises(AssertionError, match="样本点数必须为正数"):
            nco.time(10, n)


class TestNcoSine:
    def test_basic_n_form(self) -> None:
        y = nco.sine(1.0, 1.0, 0.0, 5, 4)
        assert y.shape == (5,)
        assert y.dtype == np.float64
        # fs=4, f=1：相位从0线性到2π，共5点
        np.testing.assert_allclose(y, np.cos(np.linspace(0.0, 2.0 * np.pi, 5)), atol=1e-12)

    def test_amp_and_phi(self) -> None:
        y = nco.sine(2.0, 1.0, np.pi / 2, 5, 4)
        expected = 2.0 * np.cos(np.linspace(np.pi / 2, np.pi / 2 + 2.0 * np.pi, 5))
        np.testing.assert_allclose(y, expected, atol=1e-12)

    def test_t_form(self) -> None:
        t = np.linspace(0.0, 1.0, 5)
        y = nco.sine(1.0, 1.0, 0.0, t)
        np.testing.assert_allclose(y, np.cos(2.0 * np.pi * t), atol=1e-12)

    def test_t_form_without_fs_derives_from_spacing(self) -> None:
        # 通过t的间距推导fs=4并做尼奎斯特检查：f=1合法
        t = np.linspace(0.0, 1.0, 5)
        y = nco.sine(1.0, 1.0, 0.0, t)
        assert len(y) == 5

    def test_array_amp_broadcast(self) -> None:
        amp = np.array([[1.0], [2.0]])
        y = nco.sine(amp, 1.0, 0.0, 5, 4)
        assert y.shape == (2, 5)
        phase = np.linspace(0.0, 2.0 * np.pi, 5)
        np.testing.assert_allclose(y, amp * np.cos(phase), atol=1e-12)

    def test_nyquist_scalar_raises(self) -> None:
        with pytest.raises(AssertionError, match="超过尼奎斯特频率"):
            nco.sine(1.0, 3.0, 0.0, 5, 4)

    def test_nyquist_boundary_ok(self) -> None:
        y = nco.sine(1.0, 2.0, 0.0, 5, 4)
        assert y.shape == (5,)

    def test_nyquist_array_raises(self) -> None:
        with pytest.raises(AssertionError, match="超过尼奎斯特频率"):
            nco.sine(1.0, np.array([1.0, 3.0]), 0.0, 5, 4)

    def test_t_form_nyquist_check_with_inferred_fs(self) -> None:
        t = np.linspace(0.0, 1.0, 5)  # 间距0.25 -> fs=4
        with pytest.raises(AssertionError, match="超过尼奎斯特频率"):
            nco.sine(1.0, 3.0, 0.0, t)

    def test_missing_fs_raises(self) -> None:
        with pytest.raises(AssertionError, match="必须设置采样率"):
            nco.sine(1.0, 1.0, 0.0, 5, None)

    @pytest.mark.parametrize("n", [0, -2])
    def test_non_positive_n_raises(self, n: int) -> None:
        with pytest.raises(AssertionError, match="样本点数必须为正数"):
            nco.sine(1.0, 1.0, 0.0, n, 4)


class TestNcoSinCos:
    def test_cos(self) -> None:
        t = np.linspace(0.0, 1.0, 5)
        y = nco.cos(2.0, 1.0, t)
        np.testing.assert_allclose(y, 2.0 * np.cos(2.0 * np.pi * t), atol=1e-12)

    def test_sin(self) -> None:
        t = np.linspace(0.0, 1.0, 5)
        y = nco.sin(2.0, 1.0, t)
        np.testing.assert_allclose(y, 2.0 * np.sin(2.0 * np.pi * t), atol=1e-12)

    def test_sin_is_sine_with_minus_pi_over_2(self) -> None:
        np.testing.assert_allclose(nco.sin(1.0, 1.0, 5, 4), nco.sine(1.0, 1.0, -np.pi / 2, 5, 4), atol=1e-12)

    def test_n_form(self) -> None:
        y = nco.cos(1.0, 1.0, 5, 4)
        np.testing.assert_allclose(y, np.cos(np.linspace(0.0, 2.0 * np.pi, 5)), atol=1e-12)
        y = nco.sin(1.0, 1.0, 5, 4)
        np.testing.assert_allclose(y, np.sin(np.linspace(0.0, 2.0 * np.pi, 5)), atol=1e-12)


class TestPadding:
    def test_pad_right_with_zeros(self) -> None:
        x = np.array([1, 2, 3])
        np.testing.assert_array_equal(padding(x, 3), np.array([1, 2, 3, 0, 0, 0]))

    def test_custom_value(self) -> None:
        x = np.array([1, 2, 3])
        np.testing.assert_array_equal(padding(x, 2, v=7), np.array([1, 2, 3, 7, 7]))

    def test_float_n_rounded(self) -> None:
        x = np.array([1, 2, 3])
        np.testing.assert_array_equal(padding(x, 2.6, v=7), np.array([1, 2, 3, 7, 7, 7]))

    def test_zero_pad_returns_same_content(self) -> None:
        x = np.array([1, 2, 3])
        np.testing.assert_array_equal(padding(x, 0), x)

    def test_dtype_preserved(self) -> None:
        x = np.array([1.5, 2.5], dtype=np.float32)
        y = padding(x, 2)
        assert y.dtype == np.float32
        np.testing.assert_array_equal(y, np.array([1.5, 2.5, 0.0, 0.0], dtype=np.float32))


class TestClog2:
    @pytest.mark.parametrize("x,bits", [(1, 0), (2, 1), (3, 2), (4, 2), (7, 3), (8, 3), (1024, 10)])
    def test_values(self, x: int, bits: int) -> None:
        assert clog2(x) == bits

    @pytest.mark.parametrize("x", [0, -1, -100])
    def test_non_positive_raises(self, x: int) -> None:
        with pytest.raises(AssertionError):
            clog2(x)


class TestLinear2dB:
    def test_scalar(self) -> None:
        assert linear2dB(2.0) == pytest.approx(10 * math.log10(2.0))
        assert linear2dB(2.0, square=True) == pytest.approx(20 * math.log10(2.0))

    def test_scalar_ref(self) -> None:
        assert linear2dB(2.0, ref=2.0) == pytest.approx(0.0)
        assert linear2dB(10, ref=10) == pytest.approx(0.0)
        assert isinstance(linear2dB(10, ref=10), float)

    def test_array_default_ref_is_max(self) -> None:
        x = np.array([1.0, 10.0, 100.0])
        y = linear2dB(x)
        assert isinstance(y, np.ndarray)
        assert y.dtype == np.float64
        np.testing.assert_allclose(y, 10 * np.log10(x / 100.0))

    def test_array_explicit_ref(self) -> None:
        x = np.array([1.0, 10.0, 100.0])
        np.testing.assert_allclose(linear2dB(x, ref=10.0), 10 * np.log10(x / 10.0))
        np.testing.assert_allclose(linear2dB(x, ref=np.array([10.0, 10.0, 10.0])), 10 * np.log10(x / 10.0))

    def test_scalar_with_array_ref_raises(self) -> None:
        with pytest.raises(AssertionError, match="不支持以ndarray为参考值"):
            linear2dB(2.0, ref=np.array([2.0]))  # type: ignore[call-overload]

    def test_int_array(self) -> None:
        x = np.array([1, 100])
        y = linear2dB(x)
        assert y.dtype == np.float64
