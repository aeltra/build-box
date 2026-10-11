# -*- encoding: utf-8 -*-

import fcntl
import json
import os
import subprocess
import sys
import time

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


def enter_userns(request):
    """Run the calling test again inside a user and mount namespace, the
    way enter_userns in ../c/bboxlib.sh does for the shell tests. Returns
    True in the inner run, which does the work. The outer run asserts that
    the inner one passed and returns False, or skips without namespaces."""
    if os.environ.get("BBOX_TEST_INNER") == "1":
        return True

    # Output to /dev/null and a time limit, for the same reasons as
    # userns_flags in ../c/bboxlib.sh.
    probe = subprocess.run(
        ["timeout", "-k", "5", "10", "unshare", "-Urm", "true"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    )
    if probe.returncode != 0:
        pytest.skip("user namespaces are not available")

    inner = subprocess.run(
        ["timeout", "-k", "5", "120", "unshare", "-Urm",
         sys.executable, "-m", "pytest", "-p", "no:cacheprovider",
         request.node.nodeid],
        cwd=str(request.config.rootpath),
        env=dict(os.environ, BBOX_TEST_INNER="1"),
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        universal_newlines=True
    )
    if inner.returncode != 0:
        pytest.fail(inner.stdout, pytrace=False)
    return False


def hold_lock(path):
    """Take the lock build-box-do takes on a target, see bbox_lock_dir()."""
    fd = os.open(str(path), os.O_RDONLY | os.O_DIRECTORY)
    fcntl.flock(fd, fcntl.LOCK_EX)
    return fd


def mark_ready(target_dir):
    """Leave the marker a create leaves behind once it is done."""
    marker = target_dir / "etc" / "build-box" / "target-ready"
    marker.parent.mkdir(parents=True, exist_ok=True)
    marker.touch()


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
    mark_ready(target_dir)
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
    mark_ready(tmp_path / "t")

    with pytest.raises(BuildBoxError, match="'proc' subdirectory is not empty"):
        BuildBoxTarget._delete("t", target_prefix=str(tmp_path))

    assert rmtree_recorder == []


def test_delete_removes_a_clean_target(
        tmp_path, quiet_sysroot, rmtree_recorder, monkeypatch):
    for d in ["dev", "proc", "sys", "usr/bin"]:
        (tmp_path / "t" / d).mkdir(parents=True)
    mark_ready(tmp_path / "t")
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


def test_delete_refuses_a_populated_real_home_of_any_user(
        tmp_path, quiet_sysroot, rmtree_recorder):
    # build-box-do unmounts the RealHome of the user name it runs under;
    # one left behind by a renamed account counts all the same.
    (tmp_path / "t" / "home" / "old" / "RealHome" / "f").mkdir(parents=True)
    (tmp_path / "t" / "home" / "new" / "RealHome").mkdir(parents=True)
    mark_ready(tmp_path / "t")

    with pytest.raises(BuildBoxError,
            match="'home/old/RealHome' subdirectory is not empty"):
        BuildBoxTarget._delete("t", target_prefix=str(tmp_path))

    assert rmtree_recorder == []


def test_delete_looks_for_mounts_below_the_resolved_prefix(
        tmp_path, quiet_sysroot, rmtree_recorder, monkeypatch):
    # /proc/mounts names mount points by their resolved path, so a guard
    # given the path through a symlink -- /var/lib/build-box moved to /srv,
    # say -- would never see one.
    (tmp_path / "real" / "t").mkdir(parents=True)
    mark_ready(tmp_path / "real" / "t")
    (tmp_path / "link").symlink_to(tmp_path / "real")
    looked_below = []
    monkeypatch.setattr(
        BuildBoxTarget, "_mount_below",
        classmethod(lambda cls, d, mounts="/proc/mounts":
            looked_below.append(d))
    )

    BuildBoxTarget._delete("t", target_prefix=str(tmp_path / "link"))

    resolved = str(tmp_path.resolve() / "real" / "t")
    assert looked_below == [resolved]
    assert rmtree_recorder == [resolved]


def test_delete_refuses_a_target_that_is_a_symlink(
        tmp_path, quiet_sysroot, rmtree_recorder):
    # Only the prefix is resolved. A target that points elsewhere must not
    # take the delete with it.
    (tmp_path / "elsewhere").mkdir()
    mark_ready(tmp_path / "elsewhere")
    (tmp_path / "t").symlink_to(tmp_path / "elsewhere")

    with pytest.raises(BuildBoxError, match="could not open"):
        BuildBoxTarget._delete("t", target_prefix=str(tmp_path))

    assert rmtree_recorder == []
    assert (tmp_path / "elsewhere").is_dir()


def test_delete_gives_up_on_a_lock_held_by_someone_else(
        tmp_path, quiet_sysroot, rmtree_recorder, monkeypatch):
    (tmp_path / "t").mkdir()
    mark_ready(tmp_path / "t")
    fd = hold_lock(tmp_path / "t")
    naps = []
    monkeypatch.setattr(time, "sleep", naps.append)

    try:
        with pytest.raises(BuildBoxError,
                match="another process has been holding it for 5 seconds"):
            BuildBoxTarget._delete("t", target_prefix=str(tmp_path))
    finally:
        os.close(fd)

    assert rmtree_recorder == []
    assert naps == [0.2] * 25


def test_delete_leaves_alone_a_target_that_was_replaced_while_it_waited(
        tmp_path, quiet_sysroot, rmtree_recorder, monkeypatch):
    target_dir = tmp_path / "t"
    target_dir.mkdir()
    mark_ready(target_dir)
    fd = hold_lock(target_dir)

    def replace_target(seconds):
        # While the delete waits for the lock: whoever holds it deletes
        # the target, a create makes a new one, and the lock is released.
        # The lock the delete then gets is the old target's.
        nonlocal fd
        if fd != -1:
            marker = target_dir / "etc" / "build-box" / "target-ready"
            marker.unlink()
            for d in [marker.parent, marker.parent.parent, target_dir]:
                d.rmdir()
            (target_dir / "new").mkdir(parents=True)
            os.close(fd)
            fd = -1

    monkeypatch.setattr(time, "sleep", replace_target)

    with pytest.raises(BuildBoxError, match="replaced while waiting"):
        BuildBoxTarget._delete("t", target_prefix=str(tmp_path))

    assert rmtree_recorder == []
    assert (target_dir / "new").is_dir()


def test_delete_holds_the_lock_a_concurrent_mount_needs(
        tmp_path, quiet_sysroot, monkeypatch, request):
    if not enter_userns(request):
        return

    target_dir = tmp_path / "t"
    real_home_mp = target_dir / "home" / "u" / "RealHome"
    for d in ["dev", "proc", "sys", "usr/bin"]:
        (target_dir / d).mkdir(parents=True)
    real_home_mp.mkdir(parents=True)
    mark_ready(target_dir)
    real_home = tmp_path / "home"
    real_home.mkdir()
    (real_home / "canary").write_text("precious\n")

    mount_below = BuildBoxTarget._mount_below

    def mount_below_then_login(cls, d, mounts="/proc/mounts"):
        found = mount_below(d, mounts)

        # Right after the guard looked, a `build-box login t` comes along.
        # build-box-do mounts the real home only once it has the target's
        # lock; while somebody else holds it, it waits and mounts nothing.
        fd = os.open(str(target_dir), os.O_RDONLY | os.O_DIRECTORY)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            subprocess.run(
                ["mount", "--bind", str(real_home), str(real_home_mp)],
                check=True
            )
        except BlockingIOError:
            pass
        finally:
            os.close(fd)

        return found

    monkeypatch.setattr(
        BuildBoxTarget, "_mount_below", classmethod(mount_below_then_login)
    )

    try:
        BuildBoxTarget._delete("t", target_prefix=str(tmp_path))
    finally:
        assert (real_home / "canary").read_text() == "precious\n"

    assert not target_dir.exists()


def test_delete_refuses_a_target_that_is_not_ready(
        tmp_path, quiet_sysroot, rmtree_recorder):
    # A create that is still running: its processes must not be killed,
    # nor its mounts taken away.
    (tmp_path / "t" / "usr").mkdir(parents=True)

    with pytest.raises(BuildBoxError, match="is not ready.*--force"):
        BuildBoxTarget._delete("t", target_prefix=str(tmp_path))

    assert rmtree_recorder == []
    assert quiet_sysroot.calls == []


def test_delete_with_force_removes_a_target_that_is_not_ready(
        tmp_path, quiet_sysroot, rmtree_recorder, monkeypatch):
    for d in ["dev", "proc", "sys", "usr/bin"]:
        (tmp_path / "t" / d).mkdir(parents=True)
    monkeypatch.setattr(
        BuildBoxTarget, "_mount_below",
        classmethod(lambda cls, d, mounts="/proc/mounts": None)
    )

    BuildBoxTarget._delete("t", target_prefix=str(tmp_path), force=True)

    assert rmtree_recorder == [str(tmp_path / "t")]
    assert quiet_sysroot.calls == ["terminate", "umount"]


def test_delete_with_force_still_refuses_a_populated_special_directory(
        tmp_path, quiet_sysroot, rmtree_recorder):
    (tmp_path / "t" / "proc" / "1").mkdir(parents=True)

    with pytest.raises(BuildBoxError, match="'proc' subdirectory is not empty"):
        BuildBoxTarget._delete("t", target_prefix=str(tmp_path), force=True)

    assert rmtree_recorder == []


# A build in a session owns every file in its target and may leave
# directories behind that their owner cannot list or empty, by accident
# (Go's module cache is read-only) or on purpose. delete has to remove
# them all the same. Run as root, as in a package build, these pass
# without exercising anything: root ignores the permissions.

@pytest.fixture
def no_mounts(monkeypatch):
    monkeypatch.setattr(
        BuildBoxTarget, "_mount_below",
        classmethod(lambda cls, d, mounts="/proc/mounts": None)
    )


def clean_target(target_dir):
    for d in ["dev", "proc", "sys", "home/u/RealHome"]:
        (target_dir / d).mkdir(parents=True)
    mark_ready(target_dir)


def test_delete_removes_directories_without_permissions(
        tmp_path, quiet_sysroot, no_mounts):
    target_dir = tmp_path / "t"
    clean_target(target_dir)

    # Nested, so the inner one cannot be seen until the outer one is open.
    locked = target_dir / "usr" / "a" / "b"
    locked.mkdir(parents=True)
    (locked / "f").write_text("x")
    locked.chmod(0o000)
    locked.parent.chmod(0o000)

    # Readable, but nothing in it can be removed.
    read_only = target_dir / "home" / "u" / "go" / "pkg" / "mod"
    (read_only / "sub").mkdir(parents=True)
    (read_only / "f").write_text("x")
    (read_only / "sub" / "f").write_text("x")
    (read_only / "sub").chmod(0o555)
    read_only.chmod(0o555)

    BuildBoxTarget._delete("t", target_prefix=str(tmp_path))

    assert not target_dir.exists()


def test_delete_removes_a_target_whose_root_is_locked(
        tmp_path, quiet_sysroot, no_mounts):
    # "chmod 000 /" in a session.
    target_dir = tmp_path / "t"
    clean_target(target_dir)
    target_dir.chmod(0o000)

    BuildBoxTarget._delete("t", target_prefix=str(tmp_path))

    assert not (tmp_path / "t").exists()


def test_delete_removes_a_target_whose_home_is_locked(
        tmp_path, quiet_sysroot, no_mounts):
    target_dir = tmp_path / "t"
    clean_target(target_dir)
    (target_dir / "home").chmod(0o000)

    BuildBoxTarget._delete("t", target_prefix=str(tmp_path))

    assert not target_dir.exists()


def test_delete_still_refuses_a_populated_real_home_behind_a_locked_dir(
        tmp_path, quiet_sysroot, no_mounts):
    target_dir = tmp_path / "t"
    clean_target(target_dir)
    (target_dir / "home" / "u" / "RealHome" / "canary").write_text("x")
    (target_dir / "home" / "u").chmod(0o000)

    with pytest.raises(BuildBoxError, match="RealHome' subdirectory is not empty"):
        BuildBoxTarget._delete("t", target_prefix=str(tmp_path))

    (target_dir / "home" / "u").chmod(0o755)
    assert (target_dir / "home" / "u" / "RealHome" / "canary").exists()


def test_delete_never_unlocks_a_real_home_that_is_still_mounted(
        tmp_path, quiet_sysroot, request):
    # The mount check reads the mount table, which needs no permission
    # on the home, and comes before anything that unlocks directories.
    if not enter_userns(request):
        return

    target_dir = tmp_path / "t"
    clean_target(target_dir)
    real_home = tmp_path / "home"
    (real_home / "private").mkdir(parents=True)
    (real_home / "private").chmod(0o000)
    real_home_mp = target_dir / "home" / "u" / "RealHome"
    subprocess.run(
        ["mount", "--bind", str(real_home), str(real_home_mp)], check=True
    )
    real_home.chmod(0o000)

    try:
        with pytest.raises(BuildBoxError, match="something mounted"):
            BuildBoxTarget._delete("t", target_prefix=str(tmp_path))

        assert real_home.stat().st_mode & 0o777 == 0o000
    finally:
        real_home.chmod(0o700)
        subprocess.run(["umount", str(real_home_mp)], check=True)

    assert (real_home / "private").stat().st_mode & 0o777 == 0o000
    (real_home / "private").chmod(0o700)


def test_delete_leaves_a_locked_directory_outside_the_target_alone(
        tmp_path, quiet_sysroot, no_mounts):
    outside = tmp_path / "outside"
    (outside / "inner").mkdir(parents=True)
    (outside / "inner" / "f").write_text("precious\n")
    (outside / "inner").chmod(0o000)
    outside.chmod(0o500)

    target_dir = tmp_path / "t"
    clean_target(target_dir)
    (target_dir / "link").symlink_to(outside)
    (target_dir / "locked").mkdir()
    (target_dir / "locked").chmod(0o000)

    BuildBoxTarget._delete("t", target_prefix=str(tmp_path))

    assert not target_dir.exists()
    assert outside.stat().st_mode & 0o777 == 0o500
    assert (outside / "inner").stat().st_mode & 0o777 == 0o000
    outside.chmod(0o700)
    (outside / "inner").chmod(0o700)
    assert (outside / "inner" / "f").read_text() == "precious\n"


# ── list and info ────────────────────────────────────────────────────

def test_list_marks_targets_without_a_shell_as_defunct(tmp_path, capsys):
    (tmp_path / "alive" / "usr" / "bin").mkdir(parents=True)
    (tmp_path / "alive" / "usr" / "bin" / "sh").touch()
    (tmp_path / "tools" / "tools" / "bin").mkdir(parents=True)
    (tmp_path / "tools" / "tools" / "bin" / "sh").touch()
    (tmp_path / "dead").mkdir()
    (tmp_path / "afile").touch()
    for name in ["alive", "tools", "dead"]:
        mark_ready(tmp_path / name)

    BuildBoxTarget.list(target_prefix=str(tmp_path))

    assert capsys.readouterr().out.splitlines() == [
        "alive", "dead (defunct)", "tools"
    ]


def test_list_marks_targets_that_are_not_ready_as_incomplete(
        tmp_path, capsys):
    # A create that is still running may have installed a shell already.
    (tmp_path / "building" / "usr" / "bin").mkdir(parents=True)
    (tmp_path / "building" / "usr" / "bin" / "sh").touch()
    (tmp_path / "empty").mkdir()

    BuildBoxTarget.list(target_prefix=str(tmp_path))

    assert capsys.readouterr().out.splitlines() == [
        "building (incomplete)", "empty (incomplete)"
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

    def finalize_aept_config(self, target_dir, repositories=None):
        self.calls.append(("finalize", target_dir, repositories))


@pytest.fixture
def create_stubs(monkeypatch, quiet_sysroot):
    RecordingGenerator.calls = []
    monkeypatch.setattr(target_module, "BuildBoxGenerator", RecordingGenerator)
    monkeypatch.setattr(BuildBoxTarget, "init", classmethod(lambda cls: None))
    deleted = []

    def delete(cls, name, **kw):
        deleted.append((name, kw.get("force", False)))
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
        ("finalize", target_dir, None),
    ]
    assert QuietSysroot.calls == ["enter", "exit"]
    assert create_stubs == []


def test_create_gives_the_target_the_repositories_asked_for(
        tmp_path, create_stubs):
    prefix = tmp_path / "targets"

    BuildBoxTarget.create(
        "t", "one.spec", target_prefix=str(prefix),
        repositories=["extended"]
    )

    assert RecordingGenerator.calls[-1] \
        == ("finalize", str(prefix / "t"), ["extended"])


def test_create_refuses_an_existing_populated_target(tmp_path, create_stubs):
    (tmp_path / "t" / "usr").mkdir(parents=True)

    with pytest.raises(BuildBoxError, match="already exists"):
        BuildBoxTarget.create("t", "one.spec", target_prefix=str(tmp_path))

    assert RecordingGenerator.calls == []


def test_create_refuses_an_existing_empty_target_directory(
        tmp_path, create_stubs):
    # What a create that is still running has at first, or what one that
    # was interrupted right after creating the directory leaves behind.
    (tmp_path / "t").mkdir()

    with pytest.raises(BuildBoxError, match="already exists"):
        BuildBoxTarget.create("t", "one.spec", target_prefix=str(tmp_path))

    assert RecordingGenerator.calls == []


def test_create_refuses_a_file_in_the_way(tmp_path, create_stubs):
    (tmp_path / "t").touch()

    with pytest.raises(BuildBoxError, match="already exists"):
        BuildBoxTarget.create("t", "one.spec", target_prefix=str(tmp_path))

    assert RecordingGenerator.calls == []


def test_create_with_force_replaces_an_empty_target_directory(
        tmp_path, create_stubs):
    (tmp_path / "t").mkdir()

    BuildBoxTarget.create(
        "t", "one.spec", target_prefix=str(tmp_path), force=True
    )

    assert create_stubs == [("t", True)]
    assert RecordingGenerator.calls[0] == ("prepare", str(tmp_path / "t"), "t")


def test_create_with_force_deletes_the_old_target_first(tmp_path, create_stubs):
    (tmp_path / "t" / "usr").mkdir(parents=True)

    BuildBoxTarget.create(
        "t", "one.spec", target_prefix=str(tmp_path), force=True
    )

    assert create_stubs == [("t", True)]
    assert RecordingGenerator.calls[0] == ("prepare", str(tmp_path / "t"), "t")
    assert not os.path.exists(tmp_path / "t" / "usr")


def test_create_cleans_up_and_reraises_when_a_step_fails(
        tmp_path, create_stubs, monkeypatch):
    monkeypatch.delenv("AELTRA_BBOX_CREATING", raising=False)

    with pytest.raises(RuntimeError, match="customize failed"):
        BuildBoxTarget.create("t", "boom", target_prefix=str(tmp_path))

    # The target never got ready, so the cleanup has to force the delete.
    assert create_stubs == [("t", True)]
    assert "AELTRA_BBOX_CREATING" not in os.environ


def test_create_rejects_a_bad_name_before_doing_anything(tmp_path, create_stubs):
    with pytest.raises(BuildBoxError):
        BuildBoxTarget.create("../t", "one.spec", target_prefix=str(tmp_path))

    assert not os.path.exists(tmp_path / ".." / "t")
    assert RecordingGenerator.calls == []


class ObservingGenerator(RecordingGenerator):
    seen = []

    def customize(self, target_dir, specfile):
        marker = os.path.join(target_dir, "etc", "build-box", "target-ready")
        self.seen.append(
            (os.path.exists(marker), os.environ.get("AELTRA_BBOX_CREATING"))
        )
        super().customize(target_dir, specfile)


def test_create_marks_the_target_ready_only_at_the_end(
        tmp_path, create_stubs, monkeypatch):
    ObservingGenerator.seen = []
    monkeypatch.setattr(target_module, "BuildBoxGenerator", ObservingGenerator)
    monkeypatch.delenv("AELTRA_BBOX_CREATING", raising=False)
    prefix = tmp_path / "targets"

    BuildBoxTarget.create("t", "one.spec", target_prefix=str(prefix))

    # While the specs run, build-box-do lets "run" into the target only
    # because the variable names it.
    target_dir = prefix / "t"
    assert ObservingGenerator.seen \
        == [(False, os.path.realpath(str(target_dir)))]
    assert (target_dir / "etc" / "build-box" / "target-ready").is_file()
    assert "AELTRA_BBOX_CREATING" not in os.environ


def test_create_names_the_target_before_it_mounts(
        tmp_path, create_stubs, monkeypatch):
    # The create's "build-box mount" inherits the variable; without it,
    # build-box-do refuses to mount below a target that is not ready.
    seen = []

    class RecordingSysroot(QuietSysroot):
        def __enter__(self):
            seen.append(os.environ.get("AELTRA_BBOX_CREATING"))
            return super().__enter__()

    monkeypatch.setattr(target_module, "Sysroot", RecordingSysroot)
    monkeypatch.delenv("AELTRA_BBOX_CREATING", raising=False)
    prefix = tmp_path / "targets"

    BuildBoxTarget.create("t", "one.spec", target_prefix=str(prefix))

    assert seen == [os.path.realpath(str(prefix / "t"))]
