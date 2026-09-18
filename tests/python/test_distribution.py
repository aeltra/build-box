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
