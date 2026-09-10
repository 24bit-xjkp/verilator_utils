import json
import subprocess
import typing
from collections.abc import Generator, Iterable
from contextlib import contextmanager
from dataclasses import dataclass
from enum import IntEnum, auto
from pathlib import Path
from typing import Any, overload

import numpy as np
from numpy import ndarray

from .common import basic_config, find_tool, logger
from .dsp import check_range


class system_verilog_context:
    # 缩进层级
    __indent: int
    # 表示一级缩进的字符串
    __indent_str: str
    # 生成出的SystemVerilog语句
    __sequences: list[str]

    def make_sequence(self, sequence: str, indent: int | None = None) -> str:
        """根据上下文生成一个SystemVerilog语句

        Args:
            sequence (str): 原始语句，不包含缩进和换行
            indent (int | None, optional): 语句的缩进层级，为None使用上下文中记录的层级

        Returns:
            str: _description_
        """
        actual_indent = self.__indent if indent is None else indent
        return f"{self.__indent_str * actual_indent}{sequence}\n"

    def write_sequence(self, sequence: str, indent: int | None = None) -> None:
        """直接写入一个SystemVerilog语句

        Args:
            sequence (str): 要写入的语句，不包含缩进和换行
            indent (int, optional): 语句的缩进层级，为None使用上下文中记录的层级
        """
        self.__sequences.append(self.make_sequence(sequence, indent))

    @contextmanager
    def enter_context(self) -> Generator[None, Any, None]:
        """进入一个上下文，调整层级计数"""
        self.__indent += 1
        try:
            yield
        finally:
            self.__indent -= 1

    class vector_type(IntEnum):
        logic = auto()
        bit = auto()

    class literal_format(IntEnum):
        """字面量格式"""

        none = auto()
        dec = auto()
        hex = auto()
        bin = auto()

        def format(self, x: int | str, width: int | None) -> str:
            match x:
                case int():
                    sign_str = "-" if x < 0 else ""
                    width_str = "" if width is None or self is self.none else f"{width}'"
                    base_str = {self.none: "", self.dec: "d", self.bin: "b", self.hex: "h"}[self]
                    # SystemVerilog的十六进制字面量基数字符为'h'，对应python内建格式码为'x'
                    py_format_code = {self.none: "", self.dec: "d", self.bin: "b", self.hex: "x"}[self]
                    return f"{sign_str}{width_str}{base_str}{abs(x):{py_format_code}}"
                case str():
                    assert width is None, "字符串不能设置宽度"
                    assert self is self.none, "字符串只支持none格式"
                    # json的字符串格式和SystemVerilog相同，使用json.dumps处理转义
                    return json.dumps(x)

        def __call__(self, x: int | str, width: int | None) -> str:
            return self.format(x, width)

    # 可以被引用的节点类型
    type referable_node = parameter_node | localparam_node

    @dataclass(frozen=True)
    class vector_node:
        """向量节点类型"""

        name: str
        signed: bool
        packed_dims: list[int | system_verilog_context.referable_node]
        unpacked_dims: list[int | system_verilog_context.referable_node]
        vector_type: system_verilog_context.vector_type
        expr: str

        @staticmethod
        def __deref_dims(dims: list[int | system_verilog_context.referable_node]) -> list[int]:
            """将dims数组中的节点解引用"""

            def transform(dim: int | system_verilog_context.referable_node) -> int:
                if isinstance(dim, int):
                    return dim
                else:
                    assert isinstance(dim.value, int)
                    return dim.value

            return [transform(dim) for dim in dims]

        def deref_packed_dims(self) -> list[int]:
            return self.__deref_dims(self.packed_dims)

        def deref_unpacked_dims(self) -> list[int]:
            return self.__deref_dims(self.unpacked_dims)

    def vector(
        self,
        name: str,
        signed: bool,
        packed_dims: Iterable[int | referable_node] = (),
        unpacked_dims: Iterable[int | referable_node] = (),
        vector_type: vector_type = vector_type.logic,
    ) -> vector_node:
        """生成一个向量

        Args:
            name (str): 向量名称
            signed (bool): 是否有符号
            packed_dims (Iterable[int | referable_node], optional): 压缩数组维度
            unpacked_dims (Iterable[int | referable_node], optional): 非压缩数组维度
            vector_type (vector_type, optional): 元素类型

        Returns:
            vector_context: 描述向量的节点
        """
        vector_type_str = vector_type.name + " "
        signed_str = "signed " if signed else ""

        def check_referable_node(node: system_verilog_context.referable_node) -> None:
            assert isinstance(node.value, int) and node.value > 0, "信号位宽必须为正整数"
            match node:
                case system_verilog_context.parameter_node():
                    pass
                case system_verilog_context.localparam_node():
                    assert self.__indent >= node.level, f"超出localparam作用层级{node.level}, 当前层级为{self.__indent}"
                case _:
                    raise AssertionError(f"不受支持的节点类型: {type(node)}")

        packed_dims_str = ""
        for packed_dim in packed_dims:
            match packed_dim:
                case int():
                    assert packed_dim > 0, "信号位宽必须为正整数"
                    packed_dims_str = f"[{packed_dim - 1}:0]{packed_dims_str}"
                case self.parameter_node() | self.localparam_node():
                    check_referable_node(packed_dim)
                    packed_dims_str = f"[{packed_dim.name}-1:0]{packed_dims_str}"
                case _:
                    raise AssertionError(f"不受支持的类型: {type(packed_dim)}")
        packed_dims_str += " " if packed_dims_str else ""
        unpacked_dims_str = ""
        for unpacked_dim in unpacked_dims:
            match unpacked_dim:
                case int():
                    assert unpacked_dim > 0, "信号位宽必须为正整数"
                    unpacked_dims_str = f"{unpacked_dims_str}[{unpacked_dim}]"
                case self.parameter_node() | self.localparam_node():
                    check_referable_node(unpacked_dim)
                    unpacked_dims_str = f"{unpacked_dims_str}[{unpacked_dim.name}]"
                case _:
                    raise AssertionError(f"不受支持的类型: {type(unpacked_dim)}")

        expr = f"{vector_type_str}{signed_str}{packed_dims_str}{name}{unpacked_dims_str}"
        return self.vector_node(name, signed, [*packed_dims], [*unpacked_dims], vector_type, expr)

    @dataclass(frozen=True)
    class string_node:
        """字符串节点类型"""

        name: str
        expr: str

    @classmethod
    def string(cls, name: str) -> string_node:
        return cls.string_node(name, f"string {name}")

    @dataclass(frozen=True)
    class int_node:
        """整数节点类型"""

        name: str
        expr: str

    @classmethod
    def int_(cls, name: str) -> int_node:
        return cls.int_node(name, f"int {name}")

    type assignable_node = vector_node | string_node | int_node
    type assignable_value = ndarray | int | str

    @overload
    def assign(self, lhs: vector_node, rhs: ndarray | int, format: literal_format = ...) -> str: ...

    @overload
    def assign(self, lhs: string_node, rhs: str, format: literal_format = ...) -> str: ...

    @overload
    def assign(self, lhs: int_node, rhs: int, format: literal_format = ...) -> str: ...

    @overload
    def assign(self, lhs: assignable_node, rhs: assignable_value, format: literal_format = ...) -> str: ...

    def assign(self, lhs: assignable_node, rhs: assignable_value, format: literal_format = literal_format.none) -> str:
        """将值rhs赋值给节点lhs

        Args:
            lhs (assignable_node): 左节点
            rhs (assignable_value): 右值
            format (literal_format, optional): 字面量格式

        Returns:
            str: 表达式字符串
        """

        rhs_str = ""
        match lhs:
            case self.vector_node():
                assert isinstance(rhs, ndarray | int)
                assert len(lhs.packed_dims) <= 1, "只支持1维压缩数组或标量"
                signed = lhs.signed
                if len(lhs.packed_dims) == 0:
                    width = 1
                else:
                    packed_dim = lhs.packed_dims[0]
                    width = packed_dim if isinstance(packed_dim, int) else typing.cast(int, packed_dim.value)
                if len(lhs.unpacked_dims) == 0:
                    assert isinstance(rhs, int) or rhs.size == 1, "非数组只支持一个值"
                    value = int(rhs if isinstance(rhs, int) else rhs.item())
                    check_range(value, width, signed)
                    rhs_str = format(value, width)
                else:
                    assert isinstance(rhs, ndarray), "非压缩数组只支持通过ndarray赋值"
                    assert rhs.dtype in (np.int8, np.int16, np.int32, np.int64), f"rhs的数据类型必须为有符号整数，实际为{rhs.dtype}"
                    assert [*rhs.shape] == lhs.deref_unpacked_dims()

                    def recursion_assign(array: ndarray, last: bool = True) -> None:
                        nonlocal rhs_str
                        if array.ndim == 1:
                            value_str = ", ".join(format(int(num), width) for num in array)
                            rhs_str += self.make_sequence(f"'{{{value_str}}}{'' if last else ','}")
                        else:
                            rhs_str += self.make_sequence("'{")
                            for i, sub_array in enumerate(array):
                                with self.enter_context():
                                    recursion_assign(sub_array, i == len(array) - 1)
                            rhs_str += self.make_sequence("}" if last else "},")

                    rhs_str += "\n"
                    recursion_assign(rhs)
                    # 去除最后一个make_sequence带来的\n
                    rhs_str = rhs_str[:-1]
            case self.int_node():
                assert isinstance(rhs, int)
                check_range(rhs, 32, True)
                rhs_str = format(rhs, 32)
            case self.string_node():
                assert isinstance(rhs, str)
                rhs_str = format(rhs, None)
            case _:
                raise AssertionError(f"未知的节点类型: {type(lhs)}")

        return f"{lhs.expr} = {rhs_str}"

    @dataclass(frozen=True)
    class parameter_node:
        """参数节点类型"""

        name: str
        # 在可能的情况下记录参数的值
        value: int | str | None

    def parameter(self, lhs: assignable_node, rhs: assignable_value, format: literal_format = literal_format.none) -> parameter_node:
        """创建参数节点

        Args:
            lhs (assignable_node): 左节点
            rhs (assignable_value): 右值
            format (literal_format, optional): 字面量格式

        Returns:
            parameter_node: 参数节点
        """
        name = lhs.name
        assign: str = self.assign(lhs, rhs, format)
        self.write_sequence(f"parameter {assign};")
        return self.parameter_node(name, rhs if isinstance(rhs, int | str) else None)

    @dataclass(frozen=True)
    class localparam_node:
        """参数节点类型"""

        name: str
        # 在可能的情况下记录参数的值
        value: int | str | None
        # 所在层级
        level: int

    def localparam(self, lhs: assignable_node, rhs: assignable_value, format: literal_format = literal_format.none) -> localparam_node:
        """创建参数节点

        Args:
            lhs (assignable_node): 左节点
            rhs (assignable_value): 右值
            format (literal_format, optional): 字面量格式

        Returns:
            localparam_node: 参数节点
        """
        name = lhs.name
        assign: str = self.assign(lhs, rhs, format)
        self.write_sequence(f"localparam {assign};")
        return self.localparam_node(name, rhs if isinstance(rhs, int | str) else None, self.__indent)

    @dataclass(frozen=True)
    class package_context:
        """在其他地方引用包所需的上下文类型"""

        name: str

    @contextmanager
    def package(self, name: str) -> Generator[package_context, Any, None]:
        """创建package上下文

        Args:
            name (str): _description_

        Yields:
            Generator[package_context, Any, None]: _description_
        """
        self.write_sequence(f"package {name};")
        with self.enter_context():
            yield self.package_context(name)
        self.write_sequence("endpackage")

    def __init__(self, indent: int = 4) -> None:
        """初始化SystemVerilog上下文

        Args:
            indent (int, optional): 一级缩进中空格的数量
        """
        self.__indent = 0
        self.__sequences = []
        self.__indent_str = " " * indent

    def build(self) -> str:
        """生成SystemVerilog代码

        Returns:
            str: 生成后的代码
        """
        return "".join(self.__sequences)


@contextmanager
def system_verilog(file: Path, indent: int = 4, format: bool | None = None) -> Generator[system_verilog_context, Any, None]:
    """创建一个SystemVerilog源文件，只有当所有语句都顺利生成后才会写入文件

    Args:
        file (Path): 目标文件路径
        indent (int, optional): 一级缩进中空格的数量
        format (bool, optional): 使用verible-verilog-format对生成的文件进行格式化

    Yields:
        Generator[system_verilog_context, Any, None]: 生成一个上下文对象
    """
    assert file.suffix in (".sv", ".svh"), "目标文件应当是SystemVerilog源文件"
    ctx = system_verilog_context(indent)
    yield ctx
    file.parent.mkdir(parents=True, exist_ok=True)
    file.write_text(ctx.build())
    if format is None:
        format = basic_config.format
    if not format:
        return
    verible_verilog_format = find_tool("verible-verilog-format")
    if verible_verilog_format is not None:
        verible_format = Path(__file__).parents[2] / ".verible-format"
        args: list[str | Path] = [verible_verilog_format, "--flagfile", verible_format, "--inplace", file]
        result = subprocess.run(args, capture_output=True, text=True)
        if result.returncode != 0:
            logger.warning(f"verible-verilog-format运行失败，错误码为{result.returncode}，消息如下：")
            logger.info(result.stderr)
    else:
        logger.note("未找到verible-verilog-format，跳过格式化")
