# -*- encoding: utf-8 -*-

import os
import pwd
import stat

import pytest

from aeltra.buildbox import generator as generator_module
from aeltra.buildbox.generator import BuildBoxGenerator
from aeltra.buildbox.misc.paths import Paths


@pytest.fixture
def generator(monkeypatch, tmp_path):
    """A generator whose base class does nothing and caches under tmp."""
    monkeypatch.setattr(
        generator_module.ImageGenerator, "prepare", lambda self, sysroot: None
    )
    monkeypatch.setattr(
        Paths, "cache_dir", staticmethod(lambda: str(tmp_path / "cache"))
    )
    return BuildBoxGenerator(release="ollie", arch="s390x", libc="musl")


@pytest.fixture
def sysroot(tmp_path):
    root = tmp_path / "sysroot"
    (root / "etc").mkdir(parents=True)
    return root


def mode(path):
    return stat.S_IMODE(os.lstat(path).st_mode)


def test_prepare_writes_the_target_description(generator, sysroot):
    generator.prepare(str(sysroot), "mybox")

    lines = (sysroot / "etc" / "target").read_text().splitlines()
    assert lines[0] == "TARGET_ID=mybox"
    assert "TARGET_MACHINE=s390x" in lines
    assert any(l.startswith("TARGET_TYPE=") for l in lines)
    assert any(l.startswith("TOOLS_TYPE=") for l in lines)


def test_prepare_creates_a_private_per_target_home(generator, sysroot):
    generator.prepare(str(sysroot), "mybox")

    user = pwd.getpwuid(os.getuid()).pw_name
    assert mode(sysroot / "home") == 0o755
    assert mode(sysroot / "home" / user) == 0o700


def test_prepare_points_the_package_cache_into_the_real_home(
        generator, sysroot, tmp_path):
    generator.prepare(str(sysroot), "mybox")

    user = pwd.getpwuid(os.getuid()).pw_name
    link = sysroot / ".pkg-cache"
    assert os.readlink(link) == "/home/{}/RealHome/.aeltra/cache/aeltra/pkg-cache/ollie/s390x/musl".format(user)
    assert os.path.isdir(tmp_path / "cache" / "aeltra" / "pkg-cache" / "ollie" / "s390x" / "musl")


def test_prepare_leaves_an_existing_package_cache_link_alone(generator, sysroot):
    (sysroot / ".pkg-cache").symlink_to("/somewhere/else")

    generator.prepare(str(sysroot), "mybox")

    assert os.readlink(sysroot / ".pkg-cache") == "/somewhere/else"


def test_aept_is_told_where_the_cache_is(generator, tmp_path):
    cache = str(tmp_path / "cache" / "aeltra" / "pkg-cache" / "ollie" / "s390x" / "musl")
    assert generator._aept_options("/unused") == ["--cache-dir", cache]
    assert generator._host_env("/unused") == {"AEPT_CACHE_DIR": cache}


def test_the_aept_configuration_template_uses_the_shared_cache():
    conf = BuildBoxGenerator.AEPT_CONFIG_TEMPLATE
    assert "option cache_dir /.pkg-cache" in conf
    assert "option clean_cache no" in conf
    assert "option ignore_ownership 1" in conf
