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

from aeltra.miscellaneous.userinfo import UserInfo
from aeltra.buildbox.error import BuildBoxError

class Paths:

    @staticmethod
    def homedir():
        home = UserInfo.homedir()
        if not home:
            raise BuildBoxError("unable to determine user home directory.")
        return home
    #end function

    @staticmethod
    def cache_dir():
        cache_dir = UserInfo.cache_dir()
        if not cache_dir:
            raise BuildBoxError("unable to determine cache directory.")
        return cache_dir
    #end function

    @staticmethod
    def target_prefix():
        return os.path.join(
            "/var/lib/build-box/users", "{}".format(os.getuid()), "targets"
        )
    #end function

#end class
