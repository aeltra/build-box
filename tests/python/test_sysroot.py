# -*- encoding: utf-8 -*-

import pytest

from aeltra.buildbox import sysroot as sysroot_module
from aeltra.buildbox.error import BuildBoxError
from aeltra.buildbox.sysroot import Sysroot


class Completed:
    def __init__(self, returncode):
        self.returncode = returncode


class Helper(list):
    """Every command the wrapper hands to the setuid helper, and the
    exit status the stub answers with."""

    rc = 0


@pytest.fixture
def helper(monkeypatch):
    calls = Helper()

    def run(cmd, **kwargs):
        calls.append(list(cmd))
        return Completed(calls.rc)

    monkeypatch.setattr(sysroot_module.subprocess, "run", run)
    monkeypatch.setattr(sysroot_module.sys, "argv", ["/usr/bin/build-box"])
    return calls


def test_enter_mounts_the_special_filesystems_by_name(tmp_path, helper):
    target = tmp_path / "targets" / "t"
    target.mkdir(parents=True)

    with Sysroot(str(target)):
        pass

    mount = helper[0]
    assert mount[:2] == ["/usr/bin/build-box", "mount"]
    assert mount[-3:] == ["-t", str(tmp_path / "targets"), "t"]
    flags = mount[2:-3]
    assert flags[0::2] == ["-m", "-m", "-m"]
    assert sorted(flags[1::2]) == ["dev", "proc", "sys"]


def test_exit_unmounts_everything(tmp_path, helper, monkeypatch):
    target = tmp_path / "targets" / "t"
    target.mkdir(parents=True)
    monkeypatch.setattr(Sysroot, "terminate_processes", lambda self: None)

    with Sysroot(str(target)):
        pass

    assert helper[-1] == [
        "/usr/bin/build-box", "umount", "-t", str(tmp_path / "targets"), "t"
    ]


def test_a_failed_mount_is_an_error(tmp_path, helper):
    helper.rc = 1

    with pytest.raises(BuildBoxError, match="bind mounts"):
        Sysroot(str(tmp_path)).__enter__()


def test_a_failed_umount_is_an_error(tmp_path, helper):
    helper.rc = 1

    with pytest.raises(BuildBoxError, match="release"):
        Sysroot(str(tmp_path)).umount_all()


def test_the_sysroot_path_is_normalized(tmp_path, helper):
    (tmp_path / "targets" / "t").mkdir(parents=True)
    (tmp_path / "link").symlink_to(tmp_path / "targets")

    Sysroot(str(tmp_path / "link" / "t" / ".")).umount_all()

    assert helper[0][-2:] == [str(tmp_path / "targets"), "t"]
