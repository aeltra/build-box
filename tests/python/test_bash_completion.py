# -*- encoding: utf-8 -*-

import os
import shutil
import subprocess

import pytest

SCRIPT = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "scripts", "bash-completion.sh"
))

# Two releases with different repositories, so a test can tell whether the
# release on the command line was taken into account.
DISTRO_INFO = """#!/bin/sh
case "$1" in
    list)
        printf 'ollie\\nnemo\\n'
        ;;
    repositories)
        case "$2" in
            ollie) printf 'core\\nextended\\n' ;;
            nemo)  printf 'core\\nraspi\\n'    ;;
        esac
        ;;
esac
"""

pytestmark = pytest.mark.skipif(
    shutil.which("bash") is None, reason="needs bash"
)


@pytest.fixture
def complete(tmp_path):
    stub = tmp_path / "aeltra-distro-info"
    stub.write_text(DISTRO_INFO)
    stub.chmod(0o755)

    def run(*words):
        """Complete the last of words, as typed after "build-box"."""
        words = ("build-box",) + words
        # compopt only works inside real completion; the stub records that
        # file name completion was asked for, which leaves COMPREPLY empty.
        script = (
            'source "$1"; shift; compopt() { echo "<files>"; }; '
            'COMP_WORDS=("$@"); '
            'COMP_CWORD=$((${#COMP_WORDS[@]} - 1)); '
            '_build_box_complete; printf "%s\\n" "${COMPREPLY[@]}"'
        )
        out = subprocess.run(
            ["bash", "-c", script, "bash", SCRIPT] + list(words),
            capture_output=True, text=True, check=True,
            env=dict(os.environ, PATH="{}:{}".format(
                tmp_path, os.environ.get("PATH", "")
            ))
        )
        return sorted(line for line in out.stdout.splitlines() if line)

    return run


def test_create_offers_repo_and_not_repo_base(complete):
    options = complete("create", "--re")
    assert "--repo" in options
    assert "--release" in options
    assert "--repo-base" not in options


def test_release_names_are_completed(complete):
    assert complete("create", "-r", "") == ["nemo", "ollie"]


def test_repo_names_come_from_the_given_release(complete):
    assert complete("create", "-r", "ollie", "--repo", "") \
        == ["core", "extended"]
    assert complete("create", "--release", "=", "nemo", "--repo", "") \
        == ["core", "raspi"]


def test_repo_names_of_all_releases_without_one(complete):
    assert complete("create", "--repo", "") == ["core", "extended", "raspi"]


def test_repo_names_are_filtered_by_what_was_typed(complete):
    assert complete("create", "-r", "ollie", "--repo", "e") == ["extended"]


def test_a_repo_does_not_count_as_a_positional_argument(complete):
    # The word after the target name is a spec, which completes file
    # names; the target name itself completes nothing. Counted as a
    # positional argument, "core" would be taken for the target name.
    assert complete("create", "t", "") == ["<files>"]
    assert complete("create", "--repo", "core", "") == []
    assert complete("create", "--repo", "core", "t", "") == ["<files>"]
