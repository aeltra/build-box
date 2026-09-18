# -*- encoding: utf-8 -*-

import os

import pytest

from aeltra.buildbox import cli as cli_module
from aeltra.buildbox.cli import BuildBoxCLI, EXIT_ERROR, EXIT_OK
from aeltra.buildbox.error import BuildBoxError
from aeltra.buildbox.misc.distribution import Distribution
from aeltra.buildbox.target import BuildBoxTarget


@pytest.fixture
def target(monkeypatch):
    """Record what the CLI asks BuildBoxTarget to do."""
    calls = []
    for name in ["create", "list", "info", "delete"]:
        monkeypatch.setattr(
            BuildBoxTarget, name,
            classmethod(
                lambda cls, *a, _n=name, **kw: calls.append((_n, a, kw))
            )
        )
    return calls


@pytest.fixture
def known_release(monkeypatch):
    monkeypatch.setattr(Distribution, "latest_release", staticmethod(lambda: "ollie"))
    monkeypatch.setattr(Distribution, "valid_release", staticmethod(lambda r: r == "ollie"))
    monkeypatch.setattr(Distribution, "valid_libc", staticmethod(lambda r, l: l in ("musl", "glibc")))
    monkeypatch.setattr(Distribution, "valid_arch", staticmethod(lambda r, a, libc="musl": a in ("x86_64", "s390x")))
    monkeypatch.setattr(
        cli_module.ImageGeneratorUtils, "collect_specfiles",
        staticmethod(lambda r, l, a, *specs: [s + ".resolved" for s in specs])
    )


# ── create ───────────────────────────────────────────────────────────

def test_create_defaults(target, known_release):
    BuildBoxCLI().execute_command("create", "t", "base.spec")

    assert target == [("create", ("t", "base.spec.resolved"), {
        "release": "ollie", "libc": "musl", "arch": "x86_64",
        "target_prefix": "/var/lib/build-box/users/{}/targets".format(os.getuid()),
        "force": False, "repo_base": "http://archive.aeltra.eu/dists",
        "verify": True,
    })]


def test_create_options(target, known_release, tmp_path):
    (tmp_path / "link").symlink_to(tmp_path)

    BuildBoxCLI().execute_command(
        "create", "-r", "ollie", "-a", "s390x", "-l", "glibc",
        "--repo-base", "http://mirror/dists", "--force", "--no-verify",
        "-t", str(tmp_path / "link"), "t", "a.spec", "b.spec"
    )

    (_, args, kwargs), = target
    assert args == ("t", "a.spec.resolved", "b.spec.resolved")
    assert kwargs["arch"] == "s390x"
    assert kwargs["libc"] == "glibc"
    assert kwargs["repo_base"] == "http://mirror/dists"
    assert kwargs["force"] is True
    assert kwargs["verify"] is False
    assert kwargs["target_prefix"] == str(tmp_path)


def test_create_normalizes_the_architecture_spelling(target, known_release):
    BuildBoxCLI().execute_command("create", "-a", "x86-64", "t", "a.spec")
    assert target[0][2]["arch"] == "x86_64"


@pytest.mark.parametrize("args, message", [
    (["-r", "nope", "t", "a.spec"], "not found"),
    (["-l", "uclibc", "t", "a.spec"], "C runtime"),
    (["-a", "vax", "t", "a.spec"], "architecture"),
])
def test_create_rejects_unknown_release_libc_and_arch(
        target, known_release, args, message):
    with pytest.raises(BuildBoxError, match=message):
        BuildBoxCLI().execute_command("create", *args)
    assert target == []


def test_create_needs_a_name_and_a_spec(target, known_release, capsys):
    with pytest.raises(SystemExit) as e:
        BuildBoxCLI().execute_command("create", "t")
    assert e.value.code == EXIT_ERROR
    assert "USAGE" in capsys.readouterr().out
    assert target == []


def test_create_help_exits_cleanly(target, capsys):
    with pytest.raises(SystemExit) as e:
        BuildBoxCLI().execute_command("create", "--help")
    assert e.value.code == EXIT_OK
    assert "USAGE" in capsys.readouterr().out


def test_create_rejects_an_unknown_option(target, capsys):
    with pytest.raises(SystemExit) as e:
        BuildBoxCLI().execute_command("create", "--frobnicate", "t", "a.spec")
    assert e.value.code == EXIT_ERROR


def test_create_rejects_the_short_c_option(target, known_release, capsys):
    # "-c" used to be in the getopt string with no case to handle it, and
    # the switch helper turns such a fall-through into a RuntimeError.
    with pytest.raises(SystemExit) as e:
        BuildBoxCLI().execute_command("create", "-c", "x", "t", "a.spec")
    assert e.value.code == EXIT_ERROR
    assert "USAGE" in capsys.readouterr().out
    assert target == []


# ── list, info, delete ───────────────────────────────────────────────

def test_list_passes_the_prefix(target, tmp_path):
    BuildBoxCLI().execute_command("list", "-t", str(tmp_path))
    assert target == [("list", (), {"target_prefix": str(tmp_path)})]


def test_list_takes_no_positional_argument(target, capsys):
    with pytest.raises(SystemExit) as e:
        BuildBoxCLI().execute_command("list", "extra")
    assert e.value.code == EXIT_ERROR
    assert target == []


def test_info_options(target, tmp_path):
    BuildBoxCLI().execute_command(
        "info", "--json", "-k", "libc", "-t", str(tmp_path), "t"
    )
    assert target == [("info", ("t",), {
        "format": "json", "key": "libc", "target_prefix": str(tmp_path)
    })]


def test_info_wants_exactly_one_target(target):
    for args in [[], ["a", "b"]]:
        with pytest.raises(SystemExit):
            BuildBoxCLI().execute_command("info", *args)
    assert target == []


def test_delete_takes_several_targets(target, tmp_path):
    BuildBoxCLI().execute_command("delete", "-t", str(tmp_path), "a", "b")
    assert target == [("delete", (("a", "b"),), {"target_prefix": str(tmp_path)})]


def test_delete_wants_at_least_one_target(target):
    with pytest.raises(SystemExit):
        BuildBoxCLI().execute_command("delete")
    assert target == []
