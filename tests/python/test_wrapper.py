# -*- encoding: utf-8 -*-
#
# bin/build-box has no .py extension and does its work under
# __main__, so it is run with runpy, with exec and the CLI replaced.

import logging
import os
import runpy

import pytest

WRAPPER = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "..", "bin", "build-box")
)


class Exec(Exception):
    """Raised by the exec stub so the wrapper stops where exec would."""

    def __init__(self, path, argv):
        self.path = path
        self.argv = argv


@pytest.fixture
def wrapper(monkeypatch):
    """Run the wrapper with the given arguments, unprivileged."""
    monkeypatch.setattr(os, "getuid", lambda: 1000)
    monkeypatch.delenv("BUILD_BOX_WRAPPER_A883DAFC", raising=False)
    cli_calls = []

    def stub_exec(path, argv):
        raise Exec(path, argv)

    monkeypatch.setattr(os, "execvp", stub_exec)

    from aeltra.buildbox.cli import BuildBoxCLI
    monkeypatch.setattr(
        BuildBoxCLI, "execute_command",
        lambda self, *args: cli_calls.append(args)
    )

    def run(*args):
        monkeypatch.setattr("sys.argv", [WRAPPER] + list(args))
        try:
            runpy.run_path(WRAPPER, run_name="__main__")
        finally:
            # The wrapper adds a handler to the root logger every run.
            for h in list(logging.getLogger().handlers):
                logging.getLogger().removeHandler(h)

    run.cli_calls = cli_calls
    return run


@pytest.mark.parametrize("command", ["init", "login", "mount", "umount", "run"])
def test_privileged_commands_exec_the_helper_with_the_token_set(wrapper, command):
    with pytest.raises(Exec) as e:
        wrapper(command, "--isolate", "t", "--", "make")

    assert e.value.path == "build-box-do"
    assert e.value.argv == [WRAPPER, command, "--isolate", "t", "--", "make"]
    assert os.environ["BUILD_BOX_WRAPPER_A883DAFC"] == "1"


@pytest.mark.parametrize("command", ["create", "info", "list", "delete"])
def test_python_commands_go_to_the_cli(wrapper, command):
    wrapper(command, "-t", "/x", "t")
    assert wrapper.cli_calls == [(command, "-t", "/x", "t")]


def test_root_is_refused(wrapper, monkeypatch, capsys):
    monkeypatch.setattr(os, "getuid", lambda: 0)

    with pytest.raises(SystemExit) as e:
        wrapper("list")

    assert e.value.code == 2
    assert "must not be used by root" in capsys.readouterr().err
    assert wrapper.cli_calls == []


def test_unknown_command(wrapper, capsys):
    with pytest.raises(SystemExit) as e:
        wrapper("frobnicate")
    assert e.value.code == 1
    assert "unknown command" in capsys.readouterr().err


def test_no_command_prints_usage(wrapper, capsys):
    with pytest.raises(SystemExit) as e:
        wrapper()
    assert e.value.code == 1
    assert "USAGE" in capsys.readouterr().out


def test_help(wrapper, capsys):
    wrapper("--help")
    assert "USAGE" in capsys.readouterr().out


def test_a_missing_helper_is_reported(wrapper, monkeypatch, capsys):
    def no_helper(path, argv):
        raise FileNotFoundError(path)

    monkeypatch.setattr(os, "execvp", no_helper)

    with pytest.raises(SystemExit) as e:
        wrapper("login", "t")
    assert e.value.code == 2
    assert "failed to exec build-box-do" in capsys.readouterr().err
