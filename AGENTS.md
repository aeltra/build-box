# AGENTS.md

This file provides guidance to coding agents when working with code in
this repository.

## Project Overview

Build Box manages Aeltra OS build chroots, called targets. It bootstraps a
target from packages and lets unprivileged users log into it and run
commands inside it. See `README.md` for usage.

The project has two halves:

- `bin/build-box` and `lib/aeltra/buildbox/`: the Python front end. It
  implements `create`, `list`, `info` and `delete` itself and execs
  `build-box-do` for `init`, `login`, `mount`, `umount` and `run`.
- `c-src/`: `build-box-do`, a small C program that is installed setuid
  root. It makes the mounts a target needs and enters the target.

`build-box-do` is the security boundary. Every member of the group
`build-box` can run it with arguments, an environment, resource limits and
open descriptors of their choice, and several times at once. Read a change
to `c-src/` with that caller in mind.

## Build Commands

The generated autotools files are not in git. Build in a directory of its
own:

```bash
autoreconf -i          # once, and after a configure.ac / Makefile.am change
mkdir -p build && cd build
../configure
make                   # builds c-src/build-box-do
make check             # C unit tests, shell tests and the Python tests
make check VERBOSE=1   # ... and print the log of a failing test
make -C tests/c check TESTS=test_mount.sh    # one C or shell test
```

From the top of the source tree:

```bash
python3 -m pytest tests/python                        # the Python tests
python3 -m pytest tests/python/test_target.py -k <expr>
scripts/musl-build.sh  # build and test against musl, in an Alpine container
```

Coverage needs a build directory of its own, configured with
`--enable-coverage`:

```bash
make coverage                          # run the suite, report per file
make coverage-lines FILE=c-src/util.c  # which lines no test reached
```

It is a report. There are no thresholds, and the package build does not
run it.

The tree builds without compiler warnings at the default flags. Keep it
that way.

## Layout

- `bin/build-box`: the entry point. It sets the token without which
  `build-box-do` refuses to run and dispatches the command.
- `lib/aeltra/buildbox/cli.py`: option parsing for the Python commands.
- `lib/aeltra/buildbox/target.py`: `BuildBoxTarget`, which creates, lists
  and deletes targets.
- `lib/aeltra/buildbox/generator.py`: `BuildBoxGenerator`, a subclass of
  the image generator in `aeltra.osimage`. It prepares the sysroot and
  writes the aept configuration.
- `lib/aeltra/buildbox/sysroot.py`: a context manager that mounts and
  unmounts through `build-box mount` and `build-box umount`.
- `lib/aeltra/buildbox/misc/`: release, architecture and libc checks
  (`distribution.py`) and the per-user paths (`paths.py`).
- `c-src/bbox-do.c`: `main()`, with the checks every command has to pass.
- `c-src/bbox-do.h`: the only header.
- `c-src/init.c`, `login.c`, `mount.c`, `umount.c`, `run.c`: one file per
  command, each with its usage text and its option parser.
- `c-src/config.c`: `bbox_conf_t`, the settings of one invocation.
- `c-src/util.c`: everything the commands share.
- `tests/c/`, `tests/python/`: the test suite, see "Testing".
- `scripts/bash-completion.sh`: installed as the bash completion.
- `scripts/musl-build.sh`: see "Portability". Nothing in the build calls
  it.
- `debian/`: the packaging. The package is `build-box-ng`.

## How it works

- A user's targets live in `/var/lib/build-box/users/<uid>/targets`.
  `build-box init` creates the per-user directory. Both halves take
  `-t <dir>` to work on targets elsewhere. The option is not in the help
  texts. `sysroot.py` and the tests use it.
- `build-box-do` refuses to run without the wrapper's token in the
  environment, for root, and for a user who is not in the group
  `build-box`.
- It lowers its privileges first thing in `main()` and raises them only
  around the single calls that need them: `bbox_raise_privileges()`, the
  call, `bbox_lower_privileges()`. `login` and `run` drop them for good
  and set `no_new_privs` before they exec the shell.
- Nothing is mounted or unmounted by path alone. The parent directory is
  opened and checked for its owner, the mount point is looked up relative
  to that descriptor with `O_NOFOLLOW`, and the mount is addressed through
  `/proc/self/fd`. This is what keeps a symlink the user swaps in between
  check and mount from redirecting the call. A mount that cannot be
  finished with its flags and propagation is taken down again.
- Every invocation that mounts or unmounts below a target holds an
  exclusive `flock` on the target directory (`bbox_lock_dir()`).
  `delete` takes the same lock from Python.
- `dev` is a bind of the host's `/dev` with a tmpfs on `shm` and a devpts
  on `pts`, `proc` is mounted with `hidepid=invisible`, `sys` is
  read-only, and the user's home is bound on `RealHome` in the target's
  own home with `nosuid` and `nodev`.
- `login` and `run` clear the environment except for `AELTRA_*` and a
  short list of variables, see `bbox_sanitize_environment()`.
- `create` writes `etc/build-box/target-ready` into a target as its last
  step. `build-box-do` enters no target without it, except the one that
  the running `create` names in `AELTRA_BBOX_CREATING`.
- A command's option parser is either a leaf or a dispatcher, see
  `BBOX_OPTS_LEAF` and `BBOX_OPTS_DISPATCH` in `bbox-do.h`. `run` is a
  dispatcher: it stops at the target name and passes the rest on.
- `build-box-do` exits with 254 for a wrong invocation and with 255 for a
  runtime error. The wrapper exits with 1 and 2.

Some things are defined once in C and once in Python. Change both sides
together:

- the characters allowed in a target name: `validate_target_name()` in
  `util.c` and `_target_name_valid_or_raise()` in `target.py`
- the ready marker and `AELTRA_BBOX_CREATING`
- how long a target's lock is waited for
- the wrapper token `BUILD_BOX_WRAPPER_A883DAFC`

An option of a command appears in its usage text, in
`scripts/bash-completion.sh` and, where it matters to users, in
`README.md`.

## Portability

`build-box-do` has to build against musl 1.2.2, the libc in the Aeltra
targets, and not only against glibc. Interfaces that came later, `statx`
for one, are not available. Prefer what both libcs have, `/proc` or a
plain syscall. `scripts/musl-build.sh` builds the tree and runs the suite
in an Alpine container. Alpine's musl is newer than 1.2.2, so also check
a new libc interface against the headers of a target.

Two differences that have caused bugs: glibc honours `POSIXLY_CORRECT` in
`getopt` and musl does not, which is why every parser starts with
`bbox_getopt_begin()`. And musl's `getpwent()` reuses the buffer of
`getpwuid()`, so copy what you need out of a passwd entry before the next
lookup.

## Coding Conventions

Follow the style of the file you are in. No formatter is run on this
tree, so do not reformat lines you do not change.

C:

- 4 spaces, no tabs. Continuation lines are indented by 8.
- No space between `if`, `for`, `while`, `switch` and the parenthesis.
  The brace of a function goes on its own line, the brace of a block on
  the line of its statement.
- Functions carry the prefix `bbox_` and return 0, or -1 on error.
  Command entry points return the exit status.
- One exit per function: `rval` and `goto cleanup_and_exit`.
- Errors are reported where they happen, with `bbox_perror(module, ...)`.
  `bbox_pwarning()` is for what does not stop the command.
- Comments are `/* ... */` blocks that say why. In this half they are
  welcome, the reasoning behind a check is part of the code.

Python:

- Longer blocks are closed with `#end function`, `#end class`, `#end if`
  and so on.
- Strings are built with `"...".format()`, not with f-strings.
- Errors that reach the user are raised as `BuildBoxError`.
- Keep comments in `lib/` sparse. A short docstring is fine, a comment
  block that explains a function body is not: that belongs in the commit
  message. Constants are class attributes, like the templates in
  `BuildBoxGenerator`.
- `tox.ini` has the flake8 call with the list of ignored checks.

Shell is POSIX `sh`, except for the bash completion.

The files in `c-src/`, `lib/` and `bin/` carry the full MIT text. The
tests and newer scripts carry a short header with a copyright line and
`SPDX-License-Identifier: MIT`. A new file gets what its neighbours have.

Commit messages are one line in the imperative, without a full stop:
`Mount sysfs read-only inside a target`.

## Testing

`make check` runs everything through Automake's test harness.

- `tests/c/test_*.c` are unit tests. They print TAP with the helpers in
  `test.h` and compile the `c-src/` files they need into themselves.
- `tests/c/test_*.sh` test the privileged code. They source `bboxlib.sh`
  and run themselves again inside a user namespace (`enter_userns`),
  where the caller is uid 0 and may mount. There they drive the harness
  programs (`mountdrv`, `umountdrv`, `rundrv`, `initdrv`, `cmddrv`,
  `groupdrv`), which call into the modules directly. `test_main.sh` runs
  the built `build-box-do` itself.
- `tests/python/` is the pytest suite for the wrapper, the library and
  the bash completion. `conftest.py` makes it import `aeltra.buildbox`
  from `lib/` instead of from the installed package.

A test that lacks what it needs skips with exit status 77 and does not
fail: without user namespaces all shell tests skip, and without pytest
or the `aeltra` Python packages on the host the Python tests do. A run
with skips has not tested those parts. Say so when you report a result.

Rules:

- Every fix comes with a test, and the test has been seen to fail without
  the fix. Run it against the old code first.
- A new test is registered in `tests/c/Makefile.am` (`unit_tests`, or
  `dist_check_SCRIPTS`; a new harness also in `check_PROGRAMS` and
  `AM_TESTS_ENVIRONMENT`) or in `EXTRA_DIST` of
  `tests/python/Makefile.am`.
- Write tests that pin down behaviour that matters. Do not build
  elaborate plumbing for a small gain in coverage, and propose larger
  test infrastructure before you build it.
- `login.c` and `run.c` end in an exec or inside a chroot, so gcov sees
  little of them. The shell tests are their coverage.

Running `bin/build-box` from the tree does not test the tree. On a
machine with the package installed, Python imports the installed
`aeltra.buildbox`, and the wrapper execs the installed, setuid
`build-box-do` from `PATH`. The `build-box-do` you built is not setuid
and cannot mount outside a user namespace. Do not install it or make it
setuid to try something out. Use the harnesses.

## Packaging and releases

- `debian/rules` builds `build-box-do` in `.build`, runs `make check`
  there and builds the Python package with pybuild. The version comes
  from `debian/changelog` by way of a generated `VERSION` file.
- The Python tests need `python3-aeltra-misc`, `python3-aeltra-distro-info`
  and `python3-aeltra-image-gen`, which is why they are build
  dependencies.
- The `postinst` makes `/usr/bin/build-box-do` setuid and creates the
  group `build-box`.
- A release is a commit of its own, `Update debian/changelog, version
  X.Y.Z`. The entry tells users what changed for them, in full sentences.
  Do not add one or change the version unless asked to.

## Related repositories (next to this one)

- `../aept`: the package manager that installs the packages of a target.
  The C test suite here follows the one there.
- `../distro-tools`: `image-gen` (`aeltra.osimage`), `distro-info`
  (`aeltra.distro`) and `misc` (`aeltra.miscellaneous`, `aeltra.error`),
  which the Python half imports.
- `../aeltra-bootstrap`: bootstraps the distribution inside build-box
  targets.
- `../build-scripts`, `../jenkins-ci`: build the Debian package in CI, in
  a Docker container. The shell tests only run there because the job
  lets the container create user namespaces.
