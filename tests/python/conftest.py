# -*- encoding: utf-8 -*-
#
# Make the tests import aeltra.buildbox from lib/ in this tree.
#
# The installed aeltra package is a regular package with an __init__.py,
# and a regular package beats a namespace portion wherever it sits on
# sys.path, so PYTHONPATH=lib alone still imports the installed copy of
# build-box.  Putting lib/aeltra in front of the installed package's
# __path__ makes submodule lookup find the tree first, while the sibling
# packages build-box depends on keep coming from the installation.

import os
import sys

LIB = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib")
)

# Run from anywhere but the source tree, the fallback would be the
# installed package, and a green run against that says nothing about the
# tree.  Refuse rather than measure the wrong thing.
if not os.path.isdir(os.path.join(LIB, "aeltra", "buildbox")):
    raise RuntimeError(
        "no lib/aeltra/buildbox next to these tests; run them from the "
        "source tree, or through make check"
    )

try:
    import aeltra
    aeltra.__path__.insert(0, os.path.join(LIB, "aeltra"))
    for name in [m for m in sys.modules if m.startswith("aeltra.buildbox")]:
        del sys.modules[name]
except ImportError:
    sys.path.insert(0, LIB)
