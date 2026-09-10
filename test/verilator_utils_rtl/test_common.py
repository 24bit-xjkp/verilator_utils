"""verilator_utils_rtl.common模块测试"""

import argparse
import shutil
import sys
from pathlib import Path

import pytest
from colorama import Fore
from verilator_utils_rtl.common import basic_config, find_tool, is_in_notebook, logger


class TestFindTool:
    def test_found(self, monkeypatch: pytest.MonkeyPatch) -> None:
        name = "vut_find_tool_probe_found"
        monkeypatch.setattr(shutil, "which", lambda _: "/usr/bin/vut_probe_found")
        result = find_tool(name)
        assert result == Path("/usr/bin/vut_probe_found")
        assert isinstance(result, Path)

    def test_not_found(self, monkeypatch: pytest.MonkeyPatch) -> None:
        name = "vut_find_tool_probe_missing"
        monkeypatch.setattr(shutil, "which", lambda _: None)
        assert find_tool(name) is None

    def test_cached(self, monkeypatch: pytest.MonkeyPatch) -> None:
        """第一次查询结果会被缓存，之后不再调用shutil.which"""
        name = "vut_find_tool_probe_cached"
        find_tool.cache_clear()
        calls: list[str] = []

        def fake_which(name_: str) -> str | None:
            calls.append(name_)
            return "/usr/bin/vut_probe_cached"

        monkeypatch.setattr(shutil, "which", fake_which)
        assert find_tool(name) == Path("/usr/bin/vut_probe_cached")
        assert find_tool(name) == Path("/usr/bin/vut_probe_cached")
        assert calls == [name]
        # 清空缓存后重新查询
        find_tool.cache_clear()
        assert find_tool(name) == Path("/usr/bin/vut_probe_cached")
        assert calls == [name, name]


class TestBasicConfigDefaults:
    def test_defaults(self) -> None:
        assert basic_config.data_output_dir == Path.cwd()
        assert basic_config.source_output_dir == Path.cwd()
        assert basic_config.quiet is False
        assert basic_config.format is True
        assert basic_config.visualize is True
        assert basic_config.sym_quant is True
        assert basic_config.jobs > 0

    def test_register(self) -> None:
        parser = argparse.ArgumentParser()
        basic_config.register(parser)
        assert parser.get_default("data_output_dir") == basic_config.data_output_dir
        assert parser.get_default("source_output_dir") == basic_config.source_output_dir
        assert parser.get_default("jobs") == basic_config.jobs
        assert parser.get_default("format") is basic_config.format
        assert parser.get_default("visualize") is basic_config.visualize
        assert parser.get_default("sym_quant") is basic_config.sym_quant

    def test_parse_round_trip(self, tmp_path: Path) -> None:
        parser = argparse.ArgumentParser()
        basic_config.register(parser)
        data_dir = tmp_path / "data"
        source_dir = tmp_path / "source"
        args = parser.parse_args(
            [
                "-o",
                str(data_dir),
                "-g",
                str(source_dir),
                "-j",
                "4",
                "--no-format",
                "--no-visualize",
                "--optimize-svg",
                "--no-sym-quant",
            ]
        )
        basic_config.parse(args)
        assert basic_config.data_output_dir == data_dir
        assert basic_config.source_output_dir == source_dir
        assert basic_config.jobs == 4
        assert basic_config.format is False
        assert basic_config.visualize is False
        assert basic_config.optimize_svg is True
        assert basic_config.sym_quant is False

    def test_parse_quiet_flag(self) -> None:
        """带-q解析后quiet为True"""
        parser = argparse.ArgumentParser()
        basic_config.register(parser)
        basic_config.parse(parser.parse_args(["-q"]))
        assert basic_config.quiet is True

    def test_parse_without_quiet_keeps_quiet_true(self) -> None:
        """现状：parse中`cls.quiet = args.quiet is not None`与register的default=False
        组合导致未传-q时quiet同样被置为True（疑似实现缺陷，若修正需同步修改本用例）"""
        parser = argparse.ArgumentParser()
        basic_config.register(parser)
        basic_config.quiet = False
        basic_config.parse(parser.parse_args([]))
        assert basic_config.quiet is True

    def test_parse_jobs_zero_keeps_default(self) -> None:
        parser = argparse.ArgumentParser()
        basic_config.register(parser)
        default_jobs = basic_config.jobs
        basic_config.parse(parser.parse_args(["-j", "0"]))
        assert basic_config.jobs == default_jobs

    def test_parse_jobs_negative_raises(self) -> None:
        parser = argparse.ArgumentParser()
        basic_config.register(parser)
        with pytest.raises(AssertionError, match="并发数必须是正数"):
            basic_config.parse(parser.parse_args(["-j", "-1"]))

    def test_parse_converts_dirs_to_path(self, tmp_path: Path) -> None:
        parser = argparse.ArgumentParser()
        basic_config.register(parser)
        basic_config.parse(parser.parse_args(["-o", str(tmp_path), "-g", str(tmp_path)]))
        assert isinstance(basic_config.data_output_dir, Path)
        assert isinstance(basic_config.source_output_dir, Path)


class TestSetupArgcomplete:
    def test_autocomplete_called(self, monkeypatch: pytest.MonkeyPatch) -> None:
        argcomplete = pytest.importorskip("argcomplete")
        calls: list[argparse.ArgumentParser] = []

        def fake_autocomplete(parser: argparse.ArgumentParser) -> None:
            calls.append(parser)

        monkeypatch.setattr(argcomplete, "autocomplete", fake_autocomplete)
        parser = argparse.ArgumentParser()
        basic_config.setup_argcomplete(parser)
        assert calls == [parser]

    def test_error_is_swallowed(self, monkeypatch: pytest.MonkeyPatch) -> None:
        argcomplete = pytest.importorskip("argcomplete")

        def boom(parser: argparse.ArgumentParser) -> None:
            raise RuntimeError("boom")

        monkeypatch.setattr(argcomplete, "autocomplete", boom)
        basic_config.setup_argcomplete(argparse.ArgumentParser())


class TestIsInNotebook:
    def test_true_when_ipykernel_loaded(self, monkeypatch: pytest.MonkeyPatch) -> None:
        is_in_notebook.cache_clear()
        monkeypatch.setitem(sys.modules, "ipykernel", object())
        assert is_in_notebook() is True

    def test_false_without_ipykernel(self, monkeypatch: pytest.MonkeyPatch) -> None:
        is_in_notebook.cache_clear()
        monkeypatch.delitem(sys.modules, "ipykernel", raising=False)
        assert is_in_notebook() is False

    def test_cached(self, monkeypatch: pytest.MonkeyPatch) -> None:
        is_in_notebook.cache_clear()
        monkeypatch.setitem(sys.modules, "ipykernel", object())
        assert is_in_notebook() is True
        monkeypatch.delitem(sys.modules, "ipykernel", raising=False)
        # 结果被缓存，删除ipykernel后仍然返回True
        assert is_in_notebook() is True


class TestLogger:
    def test_info_plain_text(self, capsys: pytest.CaptureFixture[str]) -> None:
        basic_config.quiet = False
        logger.info("hello")
        assert capsys.readouterr().out == "hello\n"

    def test_warning_with_color(self, capsys: pytest.CaptureFixture[str]) -> None:
        basic_config.quiet = False
        logger.warning("careful")
        assert capsys.readouterr().out == f"{Fore.MAGENTA}careful{Fore.RESET}\n"

    def test_note_with_color(self, capsys: pytest.CaptureFixture[str]) -> None:
        basic_config.quiet = False
        logger.note("note it")
        assert capsys.readouterr().out == f"{Fore.LIGHTBLUE_EX}note it{Fore.RESET}\n"

    def test_success_with_color(self, capsys: pytest.CaptureFixture[str]) -> None:
        basic_config.quiet = False
        logger.success("done")
        assert capsys.readouterr().out == f"{Fore.GREEN}done{Fore.RESET}\n"

    def test_quiet_mode_suppresses_output(self, capsys: pytest.CaptureFixture[str]) -> None:
        basic_config.quiet = True
        logger.info("hidden")
        logger.warning("hidden")
        logger.note("hidden")
        logger.success("hidden")
        assert capsys.readouterr().out == ""
