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

import logging

from aeltra.distro.config.distroinfo import DistroInfo
from aeltra.distro.config.error import DistroInfoError
from aeltra.error import AeltraError
from aeltra.buildbox.error import BuildBoxError

LOGGER = logging.getLogger(__name__)

class Distribution:

    @staticmethod
    def valid_release(name):
        try:
            releases = list(
                map(
                    lambda x: x["version_codename"],
                        DistroInfo().list(unstable=True)
                )
            )
            return name in releases
        except DistroInfoError as e:
            raise BuildBoxError(str(e))
    #end function

    @staticmethod
    def latest_release():
        try:
            releases = list(
                map(
                    lambda x: x["version_codename"],
                        DistroInfo().list(unstable=True)
                )
            )
            if len(releases) > 1:
                return releases[1]
            return releases[0]
        except DistroInfoError as e:
            raise BuildBoxError(str(e))
    #end function

    @staticmethod
    def refresh():
        """Fetch the release and mirror lists afresh. Cached copies may lack
        a release or a repository, or name a location the archive has left.
        When that fails, the cached copies are used."""
        try:
            DistroInfo().refresh(releases=True, mirrors=True)
        except AeltraError as e:
            LOGGER.warning(
                "could not refresh the release and mirror lists, using the "
                "cached ones: {}".format(e)
            )
    #end function

    @staticmethod
    def repository_names(release):
        try:
            return DistroInfo().repository_names(release=release)
        except AeltraError as e:
            raise BuildBoxError(str(e))
    #end function

    @staticmethod
    def valid_libc(release, libc):
        try:
            info = DistroInfo().find(release=release)
        except DistroInfoError as e:
            raise BuildBoxError(str(e))

        return info.get("supported-architectures", {}).get(libc) is not None
    #end function

    @staticmethod
    def valid_arch(release, arch, libc="musl"):
        try:
            info = DistroInfo().find(release=release)
        except DistroInfoError as e:
            raise BuildBoxError(str(e))

        return arch in info \
            .get("supported-architectures", {}) \
            .get(libc, [])
    #end function

#end class
