# -*- encoding: utf-8 -*-
#
# The MIT License (MIT)
#
# Copyright (c) 2021 Tobias Koch <tobias.koch@gmail.com>
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
import textwrap

from aeltra.buildbox.misc.paths import Paths
from aeltra.osimage.generator import ImageGenerator
from aeltra.osimage.util import ImageGeneratorUtils

class BuildBoxGenerator(ImageGenerator):

    AEPT_CONFIG_TEMPLATE = textwrap.dedent(
        """\
        src/gz main {repo_base}/{release}/core/{arch}/{libc}/main
        src/gz tools {repo_base}/{release}/core/{arch}/{libc}/tools/{host_arch}
        src/gz cross-tools {repo_base}/{release}/core/{arch}/{libc}/cross-tools/{host_arch}

        arch {arch}
        arch all
        arch tools

        option cache_dir /.pkg-cache
        {opt_check_sig}
        """  # noqa
    )

    ETC_TARGET_TEMPLATE = textwrap.dedent(
        """\
        TARGET_ID={target_id}
        TARGET_MACHINE={machine}
        TARGET_TYPE={target_type}
        TOOLS_TYPE={tools_type}
        """
    )

    def prepare(self, sysroot, target_id):
        super().prepare(sysroot)

        etc_target = os.path.join(sysroot, "etc", "target")
        with open(etc_target, "w+", encoding="utf-8") as f:
            f.write(
                self.ETC_TARGET_TEMPLATE.format(
                    target_id=target_id, **self.context
                )
            )

        package_cache = os.path.join(
            Paths.cache_dir(), "aeltra", "pkg-cache", self._release, self._arch,
                self._libc
        )

        if not os.path.exists(package_cache):
            os.makedirs(package_cache, exist_ok=True)

        package_cache_symlink = os.path.join(sysroot, ".pkg-cache")
        if not os.path.exists(package_cache_symlink):
            os.symlink(package_cache, package_cache_symlink)
    #end function

#end class
