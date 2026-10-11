# -*- encoding: utf-8 -*-
#
# The MIT License (MIT)
#
# Copyright (c) 2019-2021 Tobias Koch <tobias.koch@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#

import fcntl
import json
import os
import re
import subprocess
import shutil
import signal
import stat
import sys
import time

from aeltra.buildbox.error import BuildBoxError
from aeltra.buildbox.generator import BuildBoxGenerator
from aeltra.buildbox.misc.paths import Paths
from aeltra.buildbox.sysroot import Sysroot

class BuildBoxTarget:

    LOCK_WAIT_SECS  = 5
    LOCK_POLL_MSECS = 200

    READY_MARKER = os.path.join("etc", "build-box", "target-ready")
    CREATING_ENV = "AELTRA_BBOX_CREATING"

    @classmethod
    def init(cls):
        cmd = [sys.argv[0], "init"]
        try:
            subprocess.run(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True,
                check=True
            )
        except subprocess.CalledProcessError as e:
            raise BuildBoxError(e.stderr.strip())
    #end function

    @classmethod
    def create(cls, target_name, *specs, **kwargs):
        cls._target_name_valid_or_raise(target_name)

        cls.init()

        target_prefix = kwargs.get("target_prefix", Paths.target_prefix())

        if not os.path.isdir(target_prefix):
            try:
                os.makedirs(target_prefix)
            except OSError as e:
                raise BuildBoxError(
                    "failed to create target prefix '{}': {}"
                    .format(target_prefix, str(e))
                )
        #end if

        target_dir = os.path.join(target_prefix, target_name)

        if os.path.lexists(target_dir):
            if kwargs.get("force"):
                cls.delete([target_name], **kwargs)
            else:
                raise BuildBoxError(
                    "target '{}' already exists, aborting."
                    .format(target_name)
                )
            #end if
        #end if

        try:
            os.mkdir(target_dir)
        except OSError as e:
            raise BuildBoxError(
                'failed to create target folder "{}": {}'
                .format(target_dir, e)
            )

        old_creating = os.environ.get(cls.CREATING_ENV)
        os.environ[cls.CREATING_ENV] = os.path.realpath(target_dir)

        try:
            image_gen = BuildBoxGenerator(**kwargs)
            image_gen.prepare(target_dir, target_name)

            with Sysroot(target_dir):
                for specfile in specs:
                    image_gen.customize(target_dir, specfile)

            # Every spec ran with its own sources; the target keeps them all,
            # and the repositories asked for on the command line besides.
            image_gen.finalize_aept_config(
                target_dir, repositories=kwargs.get("repositories")
            )

            cls._mark_ready(target_dir)
        except (KeyboardInterrupt, Exception):
            old_sig_handler = signal.signal(signal.SIGINT, signal.SIG_IGN)

            try:
                cls._delete(target_name, **dict(kwargs, force=True))
            except Exception:
                pass
            finally:
                signal.signal(signal.SIGINT, old_sig_handler)

            raise
        finally:
            if old_creating is None:
                os.environ.pop(cls.CREATING_ENV, None)
            else:
                os.environ[cls.CREATING_ENV] = old_creating
        #end try
    #end function

    @classmethod
    def list(cls, **kwargs):
        target_prefix = kwargs.get("target_prefix", Paths.target_prefix())
        if not os.path.isdir(target_prefix):
            return

        for entry in sorted(os.listdir(target_prefix)):
            target_dir = os.path.join(target_prefix, entry)

            if not os.path.isdir(target_dir):
                continue

            if not cls._is_ready(target_dir):
                print("{} (incomplete)".format(entry))
                continue

            shell_found = False

            for prefix in ["usr", "tools"]:
                shell = os.path.join(target_dir, prefix, "bin", "sh")
                if os.path.exists(shell):
                    shell_found = True
                    break
            #end for

            print("{}{}".format(entry, "" if shell_found else " (defunct)"))
        #end for
    #end function

    @classmethod
    def info(cls, target_name, **kwargs):
        target_prefix = kwargs.get("target_prefix", Paths.target_prefix())

        target_dir = os.path.normpath(os.path.join(target_prefix, target_name))
        if not os.path.isdir(target_dir):
            raise BuildBoxError("target '{}' not found.".format(target_name))

        info_dict = {
            "sysroot": target_dir
        }

        etc_target = os.path.join(target_dir, "etc", "target")
        if not os.path.exists(etc_target):
            raise BuildBoxError(
                "target configuration not found at: {}".format(etc_target)
            )

        os_release = os.path.join(target_dir, "etc", "os-release")
        if not os.path.exists(os_release):
            raise BuildBoxError(
                "release info not found at: {}".format(os_release)
            )

        for index, file_ in enumerate([etc_target, os_release]):
            with open(file_, "r", encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith("#"):
                        continue
                    try:
                        key, value = [k.strip(' "') for k in line.split("=")]

                        key = key.lower()
                        if index == 1:
                            key = "os_" + key

                        info_dict[key] = value
                    except ValueError:
                        continue

        libc_name = os.path.join(
            target_dir, "usr", "share", "misc", "libc.name"
        )

        with open(libc_name, "r", encoding="utf-8") as f:
            info_dict["libc"] = f.read().strip()

        key = kwargs.get("key")
        if key:
            try:
                info_dict = { key: info_dict[key] }
            except KeyError:
                raise BuildBoxError("unknown key: {}".format(key))

        if kwargs.get("format", "text") == "json":
            print(json.dumps(info_dict, indent=4))
        else:
            if key:
                print(info_dict[key])
            else:
                longest  = max(len(k) for k in info_dict.keys())
                template = "{:>%d}: {}" % longest

                for key, value in sorted(info_dict.items()):
                    print(template.format(key, value))
            #end if
        #end if
    #end function

    @classmethod
    def delete(cls, targets: list, **kwargs):
        for target_name in set(targets):
            cls._target_name_valid_or_raise(target_name)
        for target_name in set(targets):
            cls._delete(target_name, **kwargs)

    @classmethod
    def _delete(cls, target_name, **kwargs):
        target_prefix = kwargs.get("target_prefix", Paths.target_prefix())

        target_dir = os.path.normpath(
            os.path.join(os.path.realpath(target_prefix), target_name)
        )
        if not os.path.isdir(target_dir):
            raise BuildBoxError("target '{}' not found.".format(target_name))

        cls._unlock_dir(target_dir)

        if not kwargs.get("force") and not cls._is_ready(target_dir):
            raise BuildBoxError(
                "target '{}' is not ready, it is still being created or its "
                "creation failed. Use --force to delete it anyway."
                .format(target_name)
            )
        #end if

        sysroot = Sysroot(target_dir)
        sysroot.terminate_processes()
        sysroot.umount_all()

        lock_fd = cls._lock_target(target_dir)

        try:
            try:
                same = os.path.samestat(
                    os.fstat(lock_fd), os.lstat(target_dir)
                )
            except FileNotFoundError:
                same = False

            if not same:
                raise BuildBoxError(
                    "target '{}' was removed or replaced while waiting for "
                    "its lock, aborting.".format(target_name)
                )
            #end if

            mountpoint = cls._mount_below(target_dir)
            if mountpoint:
                raise BuildBoxError(
                    "there is something mounted at '{}', aborting."
                    .format(mountpoint)
                )
            #end if

            try:
                cls._refuse_populated_mountpoints(target_dir)
            except PermissionError:
                cls._unlock_tree(target_dir)
                cls._refuse_populated_mountpoints(target_dir)
            #end try

            old_sig_handler = signal.signal(signal.SIGINT, signal.SIG_IGN)
            try:
                try:
                    shutil.rmtree(target_dir)
                except PermissionError:
                    cls._unlock_tree(target_dir)
                    shutil.rmtree(target_dir)
                #end try
            finally:
                signal.signal(signal.SIGINT, old_sig_handler)
        finally:
            os.close(lock_fd)
        #end try
    #end function

    @classmethod
    def _refuse_populated_mountpoints(cls, target_dir):
        """Raise a BuildBoxError if one of the directories build-box-do
        mounts on is not empty. Raises PermissionError if one of them
        cannot be looked into."""
        subdirs = ["dev", "proc", "sys"] + [
            os.path.join("home", user, "RealHome")
            for user in sorted(
                cls._list_dir(os.path.join(target_dir, "home"))
            )
        ]

        for subdir in subdirs:
            if cls._list_dir(os.path.join(target_dir, subdir)):
                raise BuildBoxError(
                    "the '{}' subdirectory is not empty, aborting."
                    .format(subdir)
                )
            #end if
        #end for
    #end function

    @classmethod
    def _list_dir(cls, path):
        try:
            return os.listdir(path)
        except (FileNotFoundError, NotADirectoryError):
            return []
    #end function

    @classmethod
    def _unlock_tree(cls, target_dir):
        """Unlock target_dir and every directory below it, see
        _unlock_dir(). Goes through descriptors and never follows a
        symlink, so it does not leave the tree."""
        cls._unlock_dir(target_dir)
        for _, dirnames, _, dirfd in os.fwalk(target_dir):
            for name in dirnames:
                cls._unlock_dir(name, dir_fd=dirfd)
            #end for
        #end for
    #end function

    @classmethod
    def _unlock_dir(cls, path, dir_fd=None):
        """Give a directory of the caller's the owner permissions rwx if
        it lacks any of them, so that it can be listed and emptied.
        Anything else, a symlink in particular, is left alone."""
        st = os.stat(path, dir_fd=dir_fd, follow_symlinks=False)

        if not stat.S_ISDIR(st.st_mode) or st.st_uid != os.getuid():
            return
        if st.st_mode & stat.S_IRWXU == stat.S_IRWXU:
            return

        os.chmod(
            path, stat.S_IMODE(st.st_mode) | stat.S_IRWXU,
            dir_fd=dir_fd, follow_symlinks=False
        )
    #end function

    @classmethod
    def _mark_ready(cls, target_dir):
        """Write the marker without which build-box-do refuses to log into
        the target, see bbox_target_check_ready() in c-src/util.c."""
        marker = os.path.join(target_dir, cls.READY_MARKER)
        os.makedirs(os.path.dirname(marker), exist_ok=True)
        with open(marker, "w", encoding="utf-8"):
            pass
    #end function

    @classmethod
    def _is_ready(cls, target_dir):
        try:
            st = os.lstat(os.path.join(target_dir, cls.READY_MARKER))
        except OSError:
            return False
        return stat.S_ISREG(st.st_mode)
    #end function

    @classmethod
    def _lock_target(cls, target_dir):
        """Take the lock build-box-do takes on a target before it mounts or
        unmounts below it, see bbox_lock_dir() in c-src/util.c. Returns the
        descriptor that holds it; closing it releases the lock."""
        try:
            fd = os.open(
                target_dir, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
            )
        except OSError as e:
            raise BuildBoxError(
                "could not open '{}': {}.".format(target_dir, e.strerror)
            )

        tries = cls.LOCK_WAIT_SECS * 1000 // cls.LOCK_POLL_MSECS

        while True:
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                return fd
            except BlockingIOError:
                if tries == 0:
                    break
                tries -= 1
                time.sleep(cls.LOCK_POLL_MSECS / 1000)
            except OSError as e:
                os.close(fd)
                raise BuildBoxError(
                    "could not lock '{}': {}.".format(target_dir, e.strerror)
                )
        #end while

        os.close(fd)
        raise BuildBoxError(
            "could not lock '{}': another process has been holding it for "
            "{} seconds.".format(target_dir, cls.LOCK_WAIT_SECS)
        )
    #end function

    @classmethod
    def _mount_below(cls, target_dir, mounts="/proc/mounts"):
        """Return the first mount point below target_dir, or None."""
        # Paths in /proc/mounts are bytes, not text: a file name need not
        # be valid UTF-8, and the kernel writes space, tab, newline and
        # backslash as a backslash and three octal digits. Read the file
        # as bytes, undo the escapes and decode the way Python decodes
        # every other path, or a target whose path contains a space is
        # never matched and the check is silent.
        with open(mounts, "rb") as f:
            buf = f.read()

        for line in buf.splitlines():
            _, mountpoint, _, _, _, _ = line.strip().split()

            mountpoint = os.fsdecode(cls._unescape_mount_path(mountpoint))
            mountpoint = os.path.normpath(os.path.realpath(mountpoint))
            if mountpoint.startswith(target_dir + os.sep):
                return mountpoint
        #end for

        return None
    #end function

    @staticmethod
    def _unescape_mount_path(path):
        return re.sub(
            rb"\\([0-7]{3})", lambda m: bytes([int(m.group(1), 8)]), path
        )
    #end function

    @classmethod
    def _target_name_valid_or_raise(cls, target_name):
        if target_name in (".", ".."):
            raise BuildBoxError(
                "a target name must not be '.' or '..'."
            )
        if not re.match(r"^[-_a-zA-Z0-9.]+$", target_name):
            raise BuildBoxError(
                "a target name must consist of characters "
                "matching [-_a-zA-Z0-9.]."
            )
    #end function

#end class
