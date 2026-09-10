"""verilator_utils_rtl.sv模块测试"""

import json
from pathlib import Path
from typing import Any, cast

import numpy as np
import pytest
from verilator_utils_rtl.common import basic_config
from verilator_utils_rtl.sv import system_verilog, system_verilog_context


def make_ctx(indent: int = 4) -> system_verilog_context:
    return system_verilog_context(indent)


class TestIndentAndSequences:
    def test_make_sequence_indent_levels(self) -> None:
        ctx = make_ctx(4)
        assert ctx.make_sequence("foo") == "foo\n"
        with ctx.enter_context():
            assert ctx.make_sequence("foo") == "    foo\n"
            with ctx.enter_context():
                assert ctx.make_sequence("foo") == "        foo\n"

    def test_make_sequence_explicit_indent(self) -> None:
        ctx = make_ctx(4)
        with ctx.enter_context():
            assert ctx.make_sequence("x", indent=5) == "                    x\n"
            assert ctx.make_sequence("y", indent=0) == "y\n"

    def test_custom_indent_width(self) -> None:
        ctx = make_ctx(2)
        with ctx.enter_context():
            assert ctx.make_sequence("foo") == "  foo\n"

    def test_write_sequence_and_build(self) -> None:
        ctx = make_ctx()
        ctx.write_sequence("logic a;")
        with ctx.enter_context():
            ctx.write_sequence("logic b;")
        assert ctx.build() == "logic a;\n    logic b;\n"

    def test_empty_build(self) -> None:
        assert make_ctx().build() == ""

    def test_contexts_are_isolated(self) -> None:
        first, second = make_ctx(), make_ctx()
        first.write_sequence("a;")
        assert first.build() == "a;\n"
        assert second.build() == ""


class TestLiteralFormat:
    def test_none_format(self) -> None:
        fmt = system_verilog_context.literal_format
        assert fmt.none.format(3, None) == "3"
        assert fmt.none.format(3, 16) == "3"
        assert fmt.none.format(-3, None) == "-3"

    def test_dec_format(self) -> None:
        fmt = system_verilog_context.literal_format
        assert fmt.dec.format(42, 16) == "16'd42"
        # 不带宽度时仍输出进制字符（现状）
        assert fmt.dec.format(42, None) == "d42"

    def test_bin_format(self) -> None:
        fmt = system_verilog_context.literal_format
        assert fmt.bin.format(5, 4) == "4'b101"
        assert fmt.bin.format(-5, 4) == "-4'b101"

    def test_hex_format(self) -> None:
        fmt = system_verilog_context.literal_format
        assert fmt.hex.format(255, 8) == "8'hff"
        assert fmt.hex.format(-7, 16) == "-16'h7"

    def test_hex_lowcase_digits(self) -> None:
        fmt = system_verilog_context.literal_format
        assert fmt.hex.format(15, 4) == "4'hf"

    def test_callable(self) -> None:
        fmt = system_verilog_context.literal_format.hex
        assert fmt(255, 8) == "8'hff"

    def test_string_escaped_as_json(self) -> None:
        fmt = system_verilog_context.literal_format
        text = 'he"llo\nworld'
        assert fmt.none.format(text, None) == json.dumps(text)

    def test_string_with_width_raises(self) -> None:
        fmt = system_verilog_context.literal_format
        with pytest.raises(AssertionError, match="字符串不能设置宽度"):
            fmt.none.format("abc", 8)

    def test_string_with_base_raises(self) -> None:
        fmt = system_verilog_context.literal_format
        with pytest.raises(AssertionError, match="字符串只支持none格式"):
            fmt.dec.format("abc", None)

    def test_enum_names(self) -> None:
        fmt = system_verilog_context.literal_format
        assert fmt.none.name == "none"
        assert fmt.hex.name == "hex"
        assert fmt.dec < fmt.hex < fmt.bin  # IntEnum按声明顺序递增


class TestVector:
    def test_vector_node_fields(self) -> None:
        ctx = make_ctx()
        node = ctx.vector("sig", True, [8], [3], ctx.vector_type.logic)
        assert node.name == "sig"
        assert node.signed is True
        assert node.packed_dims == [8]
        assert node.unpacked_dims == [3]
        assert node.vector_type is ctx.vector_type.logic
        assert node.expr == "logic signed [7:0] sig[3]"
        assert node.deref_packed_dims() == [8]
        assert node.deref_unpacked_dims() == [3]

    def test_vector_variants(self) -> None:
        ctx = make_ctx()
        assert ctx.vector("u", False, [4]).expr == "logic [3:0] u"
        assert ctx.vector("b", True).expr == "logic signed b"
        assert ctx.vector("barr", False, unpacked_dims=[2], vector_type=ctx.vector_type.bit).expr == "bit barr[2]"
        assert ctx.vector("sig", True, packed_dims=[8, 4]).expr == "logic signed [3:0][7:0] sig"

    def test_vector_with_parameter_dim(self) -> None:
        ctx = make_ctx()
        width = ctx.parameter(ctx.int_("WIDTH"), 9)
        node = ctx.vector("sig", True, packed_dims=[width])
        assert node.expr == "logic signed [WIDTH-1:0] sig"
        assert node.deref_packed_dims() == [9]
        assert ctx.build() == "parameter int WIDTH = 9;\n"

    def test_vector_with_localparam_dim_scope_check(self) -> None:
        ctx = make_ctx()
        with ctx.enter_context():
            depth = ctx.localparam(ctx.int_("DEPTH"), 4)
            assert ctx.vector("inner", True, unpacked_dims=[depth]).expr == "logic signed inner[DEPTH]"
        # 离开localparam所在层级后不能再引用
        with pytest.raises(AssertionError, match="超出localparam作用层级"):
            ctx.vector("outer", True, unpacked_dims=[depth])

    def test_invalid_int_dim(self) -> None:
        ctx = make_ctx()
        with pytest.raises(AssertionError, match="信号位宽必须为正整数"):
            ctx.vector("v", True, [0])
        with pytest.raises(AssertionError, match="信号位宽必须为正整数"):
            ctx.vector("v", True, unpacked_dims=[-1])

    def test_referable_node_value_must_be_positive_int(self) -> None:
        ctx = make_ctx()
        bad = ctx.parameter(ctx.string("TEXT"), "hello")
        with pytest.raises(AssertionError, match="信号位宽必须为正整数"):
            ctx.vector("v", True, packed_dims=[bad])

    def test_unsupported_dim_type(self) -> None:
        ctx = make_ctx()
        with pytest.raises(AssertionError, match="不受支持的类型"):
            ctx.vector("v", True, packed_dims=[cast(Any, "x")])

    def test_vector_type_enum(self) -> None:
        ctx = make_ctx()
        names = {member.name for member in ctx.vector_type}
        assert names == {"logic", "bit"}


class TestNodeFactories:
    def test_string_node(self) -> None:
        ctx = make_ctx()
        node = ctx.string("s")
        assert node.name == "s"
        assert node.expr == "string s"

    def test_int_node(self) -> None:
        ctx = make_ctx()
        node = ctx.int_("n")
        assert node.name == "n"
        assert node.expr == "int n"


class TestAssign:
    def test_scalar_formats(self) -> None:
        ctx = make_ctx()
        a = ctx.vector("a", True, [16])
        assert ctx.assign(a, -7, ctx.literal_format.hex) == "logic signed [15:0] a = -16'h7"
        assert ctx.assign(a, 42, ctx.literal_format.dec) == "logic signed [15:0] a = 16'd42"
        assert ctx.assign(a, 3, ctx.literal_format.bin) == "logic signed [15:0] a = 16'b11"
        assert ctx.assign(a, 3) == "logic signed [15:0] a = 3"
        assert ctx.assign(a, -7) == "logic signed [15:0] a = -7"

    def test_unsigned_scalar(self) -> None:
        ctx = make_ctx()
        b = ctx.vector("b", False)
        assert ctx.assign(b, 1) == "logic b = 1"

    def test_out_of_range_scalar_raises(self) -> None:
        ctx = make_ctx()
        a = ctx.vector("a", True, [16])
        with pytest.raises(AssertionError, match=r"32768超出int16范围"):
            ctx.assign(a, 32768)
        b = ctx.vector("b", False, [1])
        with pytest.raises(AssertionError, match=r"2超出uint1范围"):
            ctx.assign(b, 2)

    def test_single_element_array_to_scalar(self) -> None:
        ctx = make_ctx()
        b = ctx.vector("b", False, [1])
        assert ctx.assign(b, np.array([1])) == "logic [0:0] b = 1"

    def test_array_to_scalar_too_many_values_raises(self) -> None:
        ctx = make_ctx()
        a = ctx.vector("a", True, [16])
        with pytest.raises(AssertionError, match="非数组只支持一个值"):
            ctx.assign(a, np.array([1, 2], dtype=np.int64))

    def test_wrong_rhs_type_raises(self) -> None:
        ctx = make_ctx()
        a = ctx.vector("a", True, [16])
        with pytest.raises(AssertionError):
            ctx.assign(a, "text")

    def test_1d_array_assign(self) -> None:
        ctx = make_ctx()
        arr = ctx.vector("arr", True, [16], [3])
        expected = "logic signed [15:0] arr[3] = \n'{1, -2, 3}"
        assert ctx.assign(arr, np.array([1, -2, 3], dtype=np.int64)) == expected

    def test_2d_array_assign(self) -> None:
        ctx = make_ctx()
        m = ctx.vector("m", True, [8], [2, 3])
        data = np.array([[1, -2, 3], [4, 5, 6]], dtype=np.int64)
        expected = "logic signed [7:0] m[2][3] = \n'{\n    '{1, -2, 3},\n    '{4, 5, 6}\n}"
        assert ctx.assign(m, data) == expected

    def test_2d_array_assign_inside_context(self) -> None:
        ctx = make_ctx()
        with ctx.enter_context():
            m = ctx.vector("m", True, [8], [2, 2])
            data = np.array([[1, 2], [3, 4]], dtype=np.int64)
            expected = "logic signed [7:0] m[2][2] = \n    '{\n        '{1, 2},\n        '{3, 4}\n    }"
            assert ctx.assign(m, data) == expected

    def test_array_rhs_must_be_signed_int(self) -> None:
        ctx = make_ctx()
        arr = ctx.vector("arr", True, [16], [2])
        with pytest.raises(AssertionError, match="必须为有符号整数"):
            ctx.assign(arr, np.array([1, 2], dtype=np.float64))
        with pytest.raises(AssertionError, match="必须为有符号整数"):
            ctx.assign(arr, np.array([1, 2], dtype=np.uint32))

    def test_array_shape_mismatch_raises(self) -> None:
        ctx = make_ctx()
        arr = ctx.vector("arr", True, [16], [3])
        with pytest.raises(AssertionError):
            ctx.assign(arr, np.array([1, 2], dtype=np.int64))

    def test_multi_dimensional_packed_dims_assign_raises(self) -> None:
        ctx = make_ctx()
        a = ctx.vector("a", True, [8, 4])
        with pytest.raises(AssertionError, match="只支持1维压缩数组或标量"):
            ctx.assign(a, 1)

    def test_width_from_parameter_dim(self) -> None:
        ctx = make_ctx()
        width = ctx.parameter(ctx.int_("WIDTH"), 9)
        a = ctx.vector("a", True, [width])
        assert ctx.assign(a, 7, ctx.literal_format.dec) == "logic signed [WIDTH-1:0] a = 9'd7"

    def test_int_node_assign(self) -> None:
        ctx = make_ctx()
        n = ctx.int_("n")
        assert ctx.assign(n, 42) == "int n = 42"
        assert ctx.assign(n, 42, ctx.literal_format.dec) == "int n = 32'd42"

    def test_int_node_out_of_range(self) -> None:
        ctx = make_ctx()
        n = ctx.int_("n")
        with pytest.raises(AssertionError, match="超出int32范围"):
            ctx.assign(n, 2**31)
        with pytest.raises(AssertionError):
            ctx.assign(n, 1.5)  # type: ignore[call-overload]

    def test_string_node_assign(self) -> None:
        ctx = make_ctx()
        s = ctx.string("s")
        text = 'he"llo'
        assert ctx.assign(s, text) == f"string s = {json.dumps(text)}"

    def test_string_node_wrong_type_raises(self) -> None:
        ctx = make_ctx()
        s = ctx.string("s")
        with pytest.raises(AssertionError):
            ctx.assign(s, 42)


class TestParameterAndLocalparam:
    def test_parameter(self) -> None:
        ctx = make_ctx()
        width = ctx.parameter(ctx.int_("WIDTH"), 9)
        assert width.name == "WIDTH"
        assert width.value == 9
        assert ctx.build() == "parameter int WIDTH = 9;\n"

    def test_parameter_on_vector(self) -> None:
        ctx = make_ctx()
        w = ctx.parameter(ctx.vector("w", False, [16]), 100)
        assert w.value == 100
        assert ctx.build() == "parameter logic [15:0] w = 100;\n"

    def test_parameter_on_string(self) -> None:
        ctx = make_ctx()
        s = ctx.parameter(ctx.string("SN"), "abc")
        assert s.name == "SN"
        assert s.value == "abc"
        assert ctx.build() == 'parameter string SN = "abc";\n'

    def test_parameter_array_value_is_none(self) -> None:
        ctx = make_ctx()
        arr = ctx.vector("arr", True, [16], [2])
        node = ctx.parameter(arr, np.array([1, 2], dtype=np.int64))
        assert node.value is None
        assert ctx.build().startswith("parameter logic signed [15:0] arr[2] = \n")

    def test_localparam_records_level(self) -> None:
        ctx = make_ctx()
        node = ctx.localparam(ctx.vector("w", False, [16]), 100)
        assert node.level == 0
        with ctx.enter_context():
            inner = ctx.localparam(ctx.int_("D"), 4)
            assert inner.level == 1
        assert ctx.build() == "localparam logic [15:0] w = 100;\n    localparam int D = 4;\n"

    def test_localparam_as_dimension(self) -> None:
        ctx = make_ctx()
        w = ctx.localparam(ctx.int_("W"), 5)
        v = ctx.vector("sig", True, packed_dims=[w])
        assert v.expr == "logic signed [W-1:0] sig"
        assert v.deref_packed_dims() == [5]


class TestPackage:
    def test_package_round_trip(self) -> None:
        ctx = make_ctx()
        with ctx.package("pkg") as pkg:
            assert pkg.name == "pkg"
            ctx.write_sequence("logic foo;")
        assert ctx.build() == "package pkg;\n    logic foo;\nendpackage\n"

    def test_nested_package(self) -> None:
        ctx = make_ctx()
        with ctx.package("outer"):
            with ctx.package("inner"):
                ctx.write_sequence("logic x;")
        expected = "package outer;\n    package inner;\n        logic x;\n    endpackage\nendpackage\n"
        assert ctx.build() == expected

    def test_enter_context_balances_on_exception(self) -> None:
        ctx = make_ctx()
        with pytest.raises(RuntimeError):
            with ctx.enter_context():
                raise RuntimeError
        assert ctx.make_sequence("foo") == "foo\n"


class TestSystemVerilogFile:
    def test_wrong_suffix_raises(self, tmp_path: Path) -> None:
        with pytest.raises(AssertionError, match="SystemVerilog源文件"):
            with system_verilog(tmp_path / "module.txt", format=False):
                pass

    def test_write_file(self, tmp_path: Path) -> None:
        path = tmp_path / "gen" / "module.sv"
        with system_verilog(path, format=False) as ctx:
            ctx.write_sequence("logic [3:0] x;")
        assert path.read_text() == "logic [3:0] x;\n"

    def test_svh_suffix_allowed(self, tmp_path: Path) -> None:
        path = tmp_path / "header.svh"
        with system_verilog(path, format=False):
            pass
        assert path.exists()

    def test_no_write_on_exception(self, tmp_path: Path) -> None:
        path = tmp_path / "module.sv"
        with pytest.raises(RuntimeError):
            with system_verilog(path, format=False) as ctx:
                ctx.write_sequence("logic x;")
                raise RuntimeError("boom")
        assert not path.exists()

    def test_format_default_uses_config(self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
        """format缺省时使用basic_config.format；为False时不调用格式化工具"""
        path = tmp_path / "module.sv"
        monkeypatch.setattr(basic_config, "format", False)
        with system_verilog(path) as ctx:
            ctx.write_sequence("logic x;")
        assert path.read_text() == "logic x;\n"
        assert capsys.readouterr().out == ""

    def test_format_missing_tool_logs_note(
        self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
    ) -> None:
        import verilator_utils_rtl.sv as sv_module

        path = tmp_path / "module.sv"
        monkeypatch.setattr(sv_module, "find_tool", lambda _: None)
        with system_verilog(path, format=True) as ctx:
            ctx.write_sequence("logic x;")
        assert path.exists()
        assert "未找到verible-verilog-format，跳过格式化" in capsys.readouterr().out

    def test_format_tool_failure_logs_warning(
        self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
    ) -> None:
        import verilator_utils_rtl.sv as sv_module

        fake_tool = tmp_path / "fake-verible-format"
        fake_tool.write_text("#!/bin/sh\nexit 3\n")
        fake_tool.chmod(0o755)
        monkeypatch.setattr(sv_module, "find_tool", lambda _: fake_tool)
        path = tmp_path / "module.sv"
        with system_verilog(path, format=True) as ctx:
            ctx.write_sequence("logic x;")
        assert path.exists()
        assert "verible-verilog-format运行失败" in capsys.readouterr().out
