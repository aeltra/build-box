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

import logging
import subprocess
import sys

from aeltra.buildbox.error import BuildBoxError
from aeltra.osimage.sysroot import Sysroot as BaseSysroot

LOGGER = logging.getLogger(__name__)

class Sysroot(BaseSysroot):

    def __init__(self, sysroot):
        super().__init__(sysroot)

    def __enter__(self):
        cmd = [sys.argv[0], "mount", "-t", self.sysroot, "."]
        for mountpoint in self.MOUNTPOINTS:
            cmd.insert(2, "-m")
            cmd.insert(3, mountpoint)

        proc = subprocess.run(cmd)
        if proc.returncode != 0:
            raise BuildBoxError("failed to set up bind mounts.")

        return self
    #end function

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.terminate_processes()
        self.umount_all()
        return False
    #end function

    def umount_all(self):
        proc = subprocess.run([sys.argv[0], "umount", "-t", self.sysroot, "."])
        if proc.returncode != 0:
            raise BuildBoxError("failed to release bind mounts.")
    #end function

#end class
