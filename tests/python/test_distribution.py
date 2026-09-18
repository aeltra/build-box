# -*- encoding: utf-8 -*-

import pytest

from aeltra.distro.config.distroinfo import DistroInfo
from aeltra.distro.config.error import DistroInfoError
from aeltra.buildbox.error import BuildBoxError
from aeltra.buildbox.misc.distribution import Distribution


def test_imports_come_from_the_tree():
    import aeltra.buildbox
    assert "/lib/aeltra/buildbox/" in aeltra.buildbox.__file__


def test_valid_release_raises_when_distro_info_fails(monkeypatch):
    def boom(self, **kwargs):
        raise DistroInfoError("no release information")

    monkeypatch.setattr(DistroInfo, "list", boom)

    with pytest.raises(BuildBoxError, match="no release information"):
        Distribution.valid_release("ollie")


def test_valid_release_checks_the_codename(monkeypatch):
    monkeypatch.setattr(
        DistroInfo, "list",
        lambda self, **kwargs: [{"version_codename": "ollie"}]
    )

    assert Distribution.valid_release("ollie") is True
    assert Distribution.valid_release("nope") is False


def releases(*codenames):
    return lambda self, **kwargs: [{"version_codename": c} for c in codenames]


def test_latest_release_is_the_second_newest_when_there_are_several(monkeypatch):
    # The first entry is the unstable one; "latest" means latest stable.
    monkeypatch.setattr(DistroInfo, "list", releases("unstable", "ollie", "nemo"))
    assert Distribution.latest_release() == "ollie"


def test_latest_release_is_the_only_one_when_there_is_one(monkeypatch):
    monkeypatch.setattr(DistroInfo, "list", releases("ollie"))
    assert Distribution.latest_release() == "ollie"


def test_latest_release_raises_when_distro_info_fails(monkeypatch):
    def boom(self, **kwargs):
        raise DistroInfoError("no release information")

    monkeypatch.setattr(DistroInfo, "list", boom)
    with pytest.raises(BuildBoxError, match="no release information"):
        Distribution.latest_release()


@pytest.fixture
def ollie(monkeypatch):
    info = {"supported-architectures": {"musl": ["x86_64", "s390x"], "glibc": ["x86_64"]}}

    def find(self, release=None):
        if release != "ollie":
            raise DistroInfoError("no such release: {}".format(release))
        return info

    monkeypatch.setattr(DistroInfo, "find", find)


def test_valid_libc(ollie):
    assert Distribution.valid_libc("ollie", "musl") is True
    assert Distribution.valid_libc("ollie", "glibc") is True
    assert Distribution.valid_libc("ollie", "uclibc") is False


def test_valid_arch_defaults_to_musl(ollie):
    assert Distribution.valid_arch("ollie", "s390x") is True
    assert Distribution.valid_arch("ollie", "s390x", libc="glibc") is False
    assert Distribution.valid_arch("ollie", "vax") is False


def test_valid_libc_and_arch_raise_for_an_unknown_release(ollie):
    with pytest.raises(BuildBoxError, match="no such release"):
        Distribution.valid_libc("nope", "musl")
    with pytest.raises(BuildBoxError, match="no such release"):
        Distribution.valid_arch("nope", "x86_64")
