# -*- encoding: utf-8 -*-
#
# The MIT License (MIT)
#
# Copyright (c) 2019 Tobias Koch <tobias.koch@gmail.com>
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

import os
import sys
import getopt
import textwrap

from aeltra.buildbox.misc.distribution import Distribution
from aeltra.buildbox.misc.paths import Paths
from aeltra.buildbox.target import BuildBoxTarget
from aeltra.buildbox.error import BuildBoxError
from aeltra.miscellaneous.switch import switch
from aeltra.osimage.util import ImageGeneratorUtils

EXIT_OK = 0
EXIT_ERROR = 1

class BuildBoxCLI:

    def execute_command(self, command, *args):
        getattr(self, command)(*args)

    def create(self, *args):
        def usage():
            print(textwrap.dedent(
                """
                USAGE:

                  build-box create [OPTIONS] <new-target-name> <spec> ...

                OPTIONS:

                  -h, --help             Print this help message and exit immediately.

                  -r, --release <name>   The name of the release to bootstrap, e.g. "ollie"
                                         (defaults to latest).
                  -a, --arch <arch>      The architecture to bootstrap
                                         (defaults to host arch).
                  -l, --libc <libc>      The C runtime to use ("musl" or "glibc").

                  --repo-base <url>      Repository base URL up to and including the
                                         "dists" folder.

                  --force                Overwrite an existing target with the same name.
                  --no-verify            Do not verify package list signatures.
                """  # noqa
            ))
        #end inline function

        kwargs = {
            "release":
                Distribution.latest_release(),
            "libc":
                "musl",
            "arch":
                "x86_64",
            "target_prefix":
                Paths.target_prefix(),
            "force":
                False,
            "repo_base":
                "http://archive.aeltra.eu/dists",
            "verify":
                True
        }

        try:
            opts, args = getopt.getopt(
                args, "a:c:hl:r:t:", [
                    "arch=",
                    "force",
                    "help",
                    "libc=",
                    "no-verify",
                    "release=",
                    "repo-base=",
                    "targets="
                ]
            )
        except getopt.GetoptError:
            usage()
            sys.exit(EXIT_ERROR)

        for o, v in opts:
            for case in switch(o):
                if case("-h", "--help"):
                    usage()
                    sys.exit(EXIT_OK)
                    break
                if case("-r", "--release"):
                    kwargs["release"] = v.strip()
                    break
                if case("-a", "--arch"):
                    kwargs["arch"] = v.strip().replace("-", "_")
                    break
                if case("-l", "--libc"):
                    kwargs["libc"] = v.strip()
                    break
                if case("-t", "--targets"):
                    kwargs["target_prefix"] = os.path.normpath(
                        os.path.realpath(v.strip())
                    )
                    break
                if case("--force"):
                    kwargs["force"] = True
                    break
                if case("--repo-base"):
                    kwargs["repo_base"] = v.strip()
                    break
                if case("--no-verify"):
                    kwargs["verify"] = False
                    break
            #end for
        #end for

        release, libc, arch = kwargs["release"], kwargs["libc"], kwargs["arch"]

        if not Distribution.valid_release(release):
            raise BuildBoxError(
                'release "{}" not found, run `aeltra-distro-info refresh -r`.'
                .format(release)
            )

        if not Distribution.valid_libc(release, libc):
            raise BuildBoxError(
                'release "{}" does not support C runtime library "{}".'
                .format(release, libc)
            )

        if not Distribution.valid_arch(release, arch, libc=libc):
            raise BuildBoxError(
                'release "{}" does not support architecture "{}".'
                .format(release, arch)
            )

        if len(args) < 2:
            usage()
            sys.exit(EXIT_ERROR)

        # We collect specfiles here so that we can error out early if there is
        # a problem with one of them.
        try:
            specs = ImageGeneratorUtils.collect_specfiles(
                release, libc, arch, *args[1:]
            )
        except ImageGeneratorUtils.Error as e:
            raise BuildBoxError(str(e))

        BuildBoxTarget.create(args[0], *specs, **kwargs)
    #end function

    def list(self, *args):
        def usage():
            print(textwrap.dedent(
                """
                USAGE:

                  build-box list [OPTIONS]

                OPTIONS:

                  -h, --help      Print this help message and exit immediately.
                """  # noqa
            ))

        kwargs = {
            "target_prefix":
                Paths.target_prefix()
        }

        try:
            opts, args = getopt.getopt(
                args, "ht:", ["help", "targets="]
            )
        except getopt.GetoptError:
            usage()
            sys.exit(EXIT_ERROR)

        for o, v in opts:
            for case in switch(o):
                if case("-h", "--help"):
                    usage()
                    sys.exit(EXIT_OK)
                    break
                if case("-t", "--targets"):
                    kwargs["target_prefix"] = os.path.normpath(
                        os.path.realpath(v.strip())
                    )
                    break
            #end for
        #end for

        if len(args) != 0:
            usage()
            sys.exit(EXIT_ERROR)

        BuildBoxTarget.list(**kwargs)
    #end function

    def info(self, *args):
        def usage():
            print(textwrap.dedent(
                """
                USAGE:

                  build-box info [OPTIONS] <target-name>

                OPTIONS:

                  -h, --help        Print this help message and exit immediately.

                  --json            Output result as JSON object.

                  -k, --key <key>   Print value for given key only.
                """  # noqa
            ))

        kwargs = {
            "format":
                "text",
            "key":
                None,
            "target_prefix":
                Paths.target_prefix(),
        }

        try:
            opts, args = getopt.getopt(
                args, "hk:t:", ["help", "json", "key=", "targets="]
            )
        except getopt.GetoptError:
            usage()
            sys.exit(EXIT_ERROR)

        for o, v in opts:
            for case in switch(o):
                if case("-h", "--help"):
                    usage()
                    sys.exit(EXIT_OK)
                    break
                if case("--json"):
                    kwargs["format"] = "json"
                    break
                if case("-k", "--key"):
                    kwargs["key"] = v.strip()
                    break
                if case("-t", "--targets"):
                    kwargs["target_prefix"] = os.path.normpath(
                        os.path.realpath(v.strip())
                    )
                    break
            #end for
        #end for

        if len(args) != 1:
            usage()
            sys.exit(EXIT_ERROR)

        BuildBoxTarget.info(args[0], **kwargs)
    #end function

    def delete(self, *args):
        def usage():
            print(textwrap.dedent(
                """
                USAGE:

                  build-box delete [OPTIONS] <target-name> ...

                OPTIONS:

                  -h, --help      Print this help message and exit immediately.
                """  # noqa
            ))

        kwargs = {
            "target_prefix":
                Paths.target_prefix()
        }

        try:
            opts, args = getopt.getopt(
                args, "ht:", ["help", "targets="]
            )
        except getopt.GetoptError:
            usage()
            sys.exit(EXIT_ERROR)

        for o, v in opts:
            for case in switch(o):
                if case("-h", "--help"):
                    usage()
                    sys.exit(EXIT_OK)
                    break
                if case("-t", "--targets"):
                    kwargs["target_prefix"] = os.path.normpath(
                        os.path.realpath(v.strip())
                    )
                    break
            #end for
        #end for

        if len(args) < 1:
            usage()
            sys.exit(EXIT_ERROR)

        BuildBoxTarget.delete(args, **kwargs)
    #end function

#end class
