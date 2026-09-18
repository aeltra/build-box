# -*- encoding: utf-8 -*-

import os

import pytest

from aeltra.buildbox.error import BuildBoxError
from aeltra.buildbox.misc import paths as paths_module
from aeltra.buildbox.misc.paths import Paths


def test_target_prefix_is_per_uid():
    assert Paths.target_prefix() \
        == "/var/lib/build-box/users/{}/targets".format(os.getuid())


def test_homedir_and_cache_dir_come_from_userinfo(monkeypatch):
    monkeypatch.setattr(paths_module.UserInfo, "homedir", lambda: "/h")
    monkeypatch.setattr(paths_module.UserInfo, "cache_dir", lambda: "/h/c")

    assert Paths.homedir() == "/h"
    assert Paths.cache_dir() == "/h/c"


def test_an_unknown_home_is_an_error(monkeypatch):
    monkeypatch.setattr(paths_module.UserInfo, "homedir", lambda: None)
    monkeypatch.setattr(paths_module.UserInfo, "cache_dir", lambda: None)

    with pytest.raises(BuildBoxError):
        Paths.homedir()
    with pytest.raises(BuildBoxError):
        Paths.cache_dir()
