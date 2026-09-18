# -*- encoding: utf-8 -*-

import json
import os

import pytest

from aeltra.buildbox import target as target_module
from aeltra.buildbox.error import BuildBoxError
from aeltra.buildbox.target import BuildBoxTarget


class QuietSysroot:
    """Stands in for Sysroot: nothing to terminate, nothing to unmount."""

    calls = []

    def __init__(self, sysroot):
        self.sysroot = sysroot

    def __enter__(self):
        self.calls.append("enter")
        return self

    def __exit__(self, *exc):
        self.calls.append("exit")
        return False

    def terminate_processes(self):
        self.calls.append("terminate")

    def umount_all(self):
        self.calls.append("umount")


@pytest.fixture
def quiet_sysroot(monkeypatch):
    QuietSysroot.calls = []
    monkeypatch.setattr(target_module, "Sysroot", QuietSysroot)
    return QuietSysroot


@pytest.fixture
def rmtree_recorder(monkeypatch):
    removed = []
    monkeypatch.setattr(target_module.shutil, "rmtree", removed.append)
    return removed


# ── target names ─────────────────────────────────────────────────────

@pytest.mark.parametrize("name", ["s390x", "a.b-c_D9", "...", "-x"])
def test_target_name_accepts_the_documented_characters(name):
    BuildBoxTarget._target_name_valid_or_raise(name)


@pytest.mark.parametrize("name", [".", "..", "", "a/b", "a b", "a\tb", "ä"])
def test_target_name_rejects_everything_else(name):
    with pytest.raises(BuildBoxError):
        BuildBoxTarget._target_name_valid_or_raise(name)


# ── the /proc/mounts guard ───────────────────────────────────────────

@pytest.mark.parametrize("escaped, plain", [
    (rb"/a\040b", b"/a b"),
    (rb"/tab\011here", b"/tab\there"),
    (rb"/new\012line", b"/new\nline"),
    (rb"/back\134slash", b"/back\\slash"),
    (rb"/\040\011\012\134", b"/ \t\n\\"),
    (b"/gr\xc3\xbc\xc3\x9f", b"/gr\xc3\xbc\xc3\x9f"),
    (b"/bad\xffbyte", b"/bad\xffbyte"),
    (rb"/not\40an\9escape", rb"/not\40an\9escape"),
])
def test_unescape_undoes_exactly_the_kernels_octal_escapes(escaped, plain):
    assert BuildBoxTarget._unescape_mount_path(escaped) == plain


def mounts_file(tmp_path, *mountpoints):
    lines = [
        b"none " + mp + b" tmpfs rw,relatime 0 0\n" for mp in mountpoints
    ]
    f = tmp_path / "mounts"
    f.write_bytes(b"".join(lines))
    return str(f)


def test_mount_below_finds_a_mount_on_a_path_with_a_space(tmp_path):
    target_dir = str(tmp_path / "my target" / "t")
    mp = os.fsencode(target_dir) + b"/inner"
    os.makedirs(mp)
    mounts = mounts_file(tmp_path, mp.replace(b" ", rb"\040"))

    assert BuildBoxTarget._mount_below(target_dir, mounts) \
        == os.fsdecode(mp)


def test_mount_below_finds_a_mount_on_a_plain_path(tmp_path):
    target_dir = str(tmp_path / "t")
    mp = os.fsencode(target_dir) + b"/dev"
    os.makedirs(mp)

    assert BuildBoxTarget._mount_below(
        target_dir, mounts_file(tmp_path, mp)
    ) == os.fsdecode(mp)


def test_mount_below_ignores_mounts_elsewhere_and_the_target_itself(tmp_path):
    target_dir = str(tmp_path / "t")
    os.makedirs(target_dir)
    other = os.fsencode(str(tmp_path / "t2" / "dev"))
    os.makedirs(other)
    mounts = mounts_file(tmp_path, b"/proc", other, os.fsencode(target_dir))

    assert BuildBoxTarget._mount_below(target_dir, mounts) is None


def test_mount_below_survives_a_name_that_is_not_utf8(tmp_path):
    target_dir = str(tmp_path / "t")
    mp = os.fsencode(target_dir) + b"/bad\xffbyte"
    os.makedirs(mp)

    assert BuildBoxTarget._mount_below(
        target_dir, mounts_file(tmp_path, mp)
    ) == os.fsdecode(mp)


# ── delete ───────────────────────────────────────────────────────────

def test_delete_refuses_when_something_is_mounted_below(
        tmp_path, quiet_sysroot, rmtree_recorder, monkeypatch):
    target_dir = tmp_path / "t"
    target_dir.mkdir()
    monkeypatch.setattr(
        BuildBoxTarget, "_mount_below",
        classmethod(lambda cls, d, mounts="/proc/mounts": d + "/dev/pts")
    )

    with pytest.raises(BuildBoxError, match="something mounted"):
        BuildBoxTarget._delete("t", target_prefix=str(tmp_path))

    assert rmtree_recorder == []
    assert quiet_sysroot.calls == ["terminate", "umount"]


def test_delete_refuses_a_populated_special_directory(
        tmp_path, quiet_sysroot, rmtree_recorder):
    (tmp_path / "t" / "proc" / "1").mkdir(parents=True)

    with pytest.raises(BuildBoxError, match="'proc' subdirectory is not empty"):
        BuildBoxTarget._delete("t", target_prefix=str(tmp_path))

    assert rmtree_recorder == []


def test_delete_removes_a_clean_target(
        tmp_path, quiet_sysroot, rmtree_recorder, monkeypatch):
    for d in ["dev", "proc", "sys", "usr/bin"]:
        (tmp_path / "t" / d).mkdir(parents=True)
    monkeypatch.setattr(
        BuildBoxTarget, "_mount_below",
        classmethod(lambda cls, d, mounts="/proc/mounts": None)
    )

    BuildBoxTarget._delete("t", target_prefix=str(tmp_path))

    assert rmtree_recorder == [str(tmp_path / "t")]


def test_delete_of_a_missing_target_is_an_error(tmp_path, quiet_sysroot):
    with pytest.raises(BuildBoxError, match="not found"):
        BuildBoxTarget._delete("nope", target_prefix=str(tmp_path))


def test_delete_validates_every_name_before_touching_anything(
        tmp_path, quiet_sysroot, rmtree_recorder, monkeypatch):
    (tmp_path / "good").mkdir()
    monkeypatch.setattr(
        BuildBoxTarget, "_mount_below",
        classmethod(lambda cls, d, mounts="/proc/mounts": None)
    )

    with pytest.raises(BuildBoxError):
        BuildBoxTarget.delete(["good", "../etc"], target_prefix=str(tmp_path))

    assert rmtree_recorder == []


# ── list and info ────────────────────────────────────────────────────

def test_list_marks_targets_without_a_shell_as_defunct(tmp_path, capsys):
    (tmp_path / "alive" / "usr" / "bin").mkdir(parents=True)
    (tmp_path / "alive" / "usr" / "bin" / "sh").touch()
    (tmp_path / "tools" / "tools" / "bin").mkdir(parents=True)
    (tmp_path / "tools" / "tools" / "bin" / "sh").touch()
    (tmp_path / "dead").mkdir()
    (tmp_path / "afile").touch()

    BuildBoxTarget.list(target_prefix=str(tmp_path))

    assert capsys.readouterr().out.splitlines() == [
        "alive", "dead (defunct)", "tools"
    ]


def test_list_of_a_missing_prefix_prints_nothing(tmp_path, capsys):
    BuildBoxTarget.list(target_prefix=str(tmp_path / "none"))
    assert capsys.readouterr().out == ""


@pytest.fixture
def described_target(tmp_path):
    t = tmp_path / "t"
    (t / "etc").mkdir(parents=True)
    (t / "usr" / "share" / "misc").mkdir(parents=True)
    (t / "etc" / "target").write_text(
        "TARGET_ID=t\nTARGET_MACHINE=s390x\n\n# a comment\n"
    )
    (t / "etc" / "os-release").write_text('NAME="Aeltra"\nVERSION_ID=1\n')
    (t / "usr" / "share" / "misc" / "libc.name").write_text("musl\n")
    return tmp_path


def test_info_as_json(described_target, capsys):
    BuildBoxTarget.info(
        "t", target_prefix=str(described_target), format="json"
    )

    assert json.loads(capsys.readouterr().out) == {
        "sysroot": str(described_target / "t"),
        "target_id": "t",
        "target_machine": "s390x",
        "os_name": "Aeltra",
        "os_version_id": "1",
        "libc": "musl",
    }


def test_info_prints_one_key_bare(described_target, capsys):
    BuildBoxTarget.info("t", target_prefix=str(described_target), key="libc")
    assert capsys.readouterr().out == "musl\n"


def test_info_rejects_an_unknown_key(described_target):
    with pytest.raises(BuildBoxError, match="unknown key"):
        BuildBoxTarget.info("t", target_prefix=str(described_target), key="x")


def test_info_needs_the_target_configuration(tmp_path):
    (tmp_path / "t").mkdir()
    with pytest.raises(BuildBoxError, match="target configuration not found"):
        BuildBoxTarget.info("t", target_prefix=str(tmp_path))


# ── init and create ──────────────────────────────────────────────────

def test_init_runs_the_setuid_helper_and_relays_its_error(monkeypatch):
    calls = []

    def run(cmd, **kwargs):
        calls.append(cmd)
        raise target_module.subprocess.CalledProcessError(
            2, cmd, stderr="build-box-do init: error: no group\n"
        )

    monkeypatch.setattr(target_module.subprocess, "run", run)
    monkeypatch.setattr(target_module.sys, "argv", ["/usr/bin/build-box"])

    with pytest.raises(BuildBoxError, match="no group"):
        BuildBoxTarget.init()

    assert calls == [["/usr/bin/build-box", "init"]]


class RecordingGenerator:
    calls = []

    def __init__(self, **kwargs):
        self.kwargs = kwargs

    def prepare(self, target_dir, target_name):
        self.calls.append(("prepare", target_dir, target_name))

    def customize(self, target_dir, specfile):
        self.calls.append(("customize", target_dir, specfile))
        if specfile == "boom":
            raise RuntimeError("customize failed")


@pytest.fixture
def create_stubs(monkeypatch, quiet_sysroot):
    RecordingGenerator.calls = []
    monkeypatch.setattr(target_module, "BuildBoxGenerator", RecordingGenerator)
    monkeypatch.setattr(BuildBoxTarget, "init", classmethod(lambda cls: None))
    deleted = []

    def delete(cls, name, **kw):
        deleted.append(name)
        target_module.shutil.rmtree(
            os.path.join(kw.get("target_prefix"), name), ignore_errors=True
        )

    monkeypatch.setattr(BuildBoxTarget, "_delete", classmethod(delete))
    return deleted


def test_create_prepares_then_customizes_inside_the_mounted_sysroot(
        tmp_path, create_stubs):
    prefix = tmp_path / "targets"

    BuildBoxTarget.create("t", "one.spec", "two.spec", target_prefix=str(prefix))

    target_dir = str(prefix / "t")
    assert os.path.isdir(target_dir)
    assert RecordingGenerator.calls == [
        ("prepare", target_dir, "t"),
        ("customize", target_dir, "one.spec"),
        ("customize", target_dir, "two.spec"),
    ]
    assert QuietSysroot.calls == ["enter", "exit"]
    assert create_stubs == []


def test_create_refuses_an_existing_populated_target(tmp_path, create_stubs):
    (tmp_path / "t" / "usr").mkdir(parents=True)

    with pytest.raises(BuildBoxError, match="already exists"):
        BuildBoxTarget.create("t", "one.spec", target_prefix=str(tmp_path))

    assert RecordingGenerator.calls == []


def test_create_with_force_deletes_the_old_target_first(tmp_path, create_stubs):
    (tmp_path / "t" / "usr").mkdir(parents=True)

    BuildBoxTarget.create(
        "t", "one.spec", target_prefix=str(tmp_path), force=True
    )

    assert create_stubs == ["t"]
    assert RecordingGenerator.calls[0] == ("prepare", str(tmp_path / "t"), "t")
    assert not os.path.exists(tmp_path / "t" / "usr")


def test_create_cleans_up_and_reraises_when_a_step_fails(tmp_path, create_stubs):
    with pytest.raises(RuntimeError, match="customize failed"):
        BuildBoxTarget.create("t", "boom", target_prefix=str(tmp_path))

    assert create_stubs == ["t"]


def test_create_rejects_a_bad_name_before_doing_anything(tmp_path, create_stubs):
    with pytest.raises(BuildBoxError):
        BuildBoxTarget.create("../t", "one.spec", target_prefix=str(tmp_path))

    assert not os.path.exists(tmp_path / ".." / "t")
    assert RecordingGenerator.calls == []
