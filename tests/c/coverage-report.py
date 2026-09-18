#!/usr/bin/env python3
# coverage-report.py - aggregate gcov data into a per-file coverage report.
#
# Copyright (c) 2026 Tobias Koch <tobias.koch@gmail.com>
# SPDX-License-Identifier: MIT
#
# Driven by "make coverage" and "make coverage-lines"; runnable by hand.
# Adapted from aept's tests/coverage-report.py, minus the tiers, the
# baseline and the gate: build-box reports a figure and enforces nothing.
#
# Why this exists rather than lcov or gcovr: gcov ships with gcc, which is
# already required, so a build that can compile build-box can measure it
# with no further dependency.  The whole job is running gcov, summing
# what it prints and printing the totals, which is a smaller thing than
# either of those tools.
#
# Three properties of this tree shape the numbers, and the last two put a
# ceiling on them that no test can lift:
#
#   * every test program compiles the c-src files it needs into itself,
#     so one source file has counters in several objects, one per
#     program.  All of them are real coverage and all are summed.  gcov
#     names its output after the source, so running it on every object
#     into one directory would silently overwrite; hence one output
#     directory per object.
#
#   * gcov writes its counters when the process ends normally.  A process
#     that ends in exec never writes them: the image is replaced before
#     the flush.  bbox_login_sh_chrooted() and the run path end in the
#     exec of a shell, so most of login.c and run.c can never appear
#     here however hard the shell tests drive them.
#
#   * a process that has chrooted cannot write them either: the .gcda
#     path is absolute and belongs to the host tree, which the chroot
#     does not contain.  The parent of an isolated session ends that
#     way.  And anything that leaves by _exit() skips the flush.
#
#   Do not add a __gcov_dump() to production code to make those lines
#   appear.  The shell tests are the coverage those paths have.

import argparse
import glob
import gzip
import json
import os
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor

SOURCES = "c-src/"


# ── collecting ───────────────────────────────────────────────────────────

class FileCov:
    """Merged coverage for one source file."""

    def __init__(self):
        self.lines = defaultdict(int)      # line number   -> summed count
        self.branches = defaultdict(int)   # (line, index)  -> summed count
        self.functions = defaultdict(int)  # function name  -> summed count

    def line_total(self):
        return len(self.lines)

    def line_hit(self):
        return sum(1 for c in self.lines.values() if c > 0)

    def branch_total(self):
        return len(self.branches)

    def branch_hit(self):
        return sum(1 for c in self.branches.values() if c > 0)

    def unentered(self):
        return sorted(n for n, c in self.functions.items() if c == 0)


def find_gcda(build_dir):
    out = []
    for root, _dirs, files in os.walk(build_dir):
        for f in files:
            if f.endswith(".gcda"):
                out.append(os.path.join(root, f))
    return sorted(out)


def run_gcov(index, gcda, workdir, gcov):
    """Run gcov for one object into its own directory; return the JSON paths.

    The output directory is per object and the index comes from the
    caller rather than a counter here, because these run in parallel:
    two objects sharing a directory is the collision this whole
    arrangement exists to avoid.

    Both paths handed to gcov are absolute.  gcov runs with its cwd set
    to that output directory, so a relative -o would resolve against the
    output directory instead of the build tree -- which fails as
    "cannot open notes file", quietly, into a report of zeroes.
    """
    d = os.path.join(workdir, "%06d" % index)
    os.makedirs(d, exist_ok=True)
    gcda = os.path.abspath(gcda)
    r = subprocess.run([gcov, "-i", "-b", "-o", os.path.dirname(gcda), gcda],
                       cwd=d, capture_output=True, text=True)
    if r.returncode != 0:
        first = (r.stderr.strip().splitlines() or ["(no message)"])[0]
        print("warning: gcov failed for %s: %s" % (gcda, first),
              file=sys.stderr)
    return glob.glob(os.path.join(d, "*.gcov.json.gz"))


def normalise(path, cwd, srcdir):
    """Map a path as the compiler saw it to one relative to srcdir."""
    p = path if os.path.isabs(path) else os.path.join(cwd, path)
    p = os.path.realpath(p)
    rel = os.path.relpath(p, srcdir)
    if rel.startswith(os.pardir):
        return None
    return rel.replace(os.sep, "/")


def collect(build_dir, srcdir, gcov, jobs):
    build_dir = os.path.realpath(build_dir)
    gcdas = find_gcda(build_dir)
    if not gcdas:
        sys.exit("no .gcda files under %s\n"
                 "  Nothing has been measured.  Build with coverage first:\n"
                 "    ./configure --enable-coverage && make && make check"
                 % build_dir)

    srcdir = os.path.realpath(srcdir)
    files = defaultdict(FileCov)
    version = None

    workdir = tempfile.mkdtemp(prefix="bbox-coverage.")
    try:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            batches = list(pool.map(
                lambda ig: run_gcov(ig[0], ig[1], workdir, gcov),
                enumerate(gcdas)))

        for jsons in batches:
            for j in jsons:
                with gzip.open(j) as fh:
                    data = json.load(fh)
                version = version or data.get("gcc_version")
                cwd = data.get("current_working_directory", "")
                for entry in data.get("files", []):
                    rel = normalise(entry["file"], cwd, srcdir)
                    if rel is None or not rel.startswith(SOURCES):
                        continue
                    cov = files[rel]
                    for ln in entry.get("lines", []):
                        n = ln["line_number"]
                        cov.lines[n] += ln["count"]
                        for i, br in enumerate(ln.get("branches", [])):
                            cov.branches[(n, i)] += br.get("count", 0)
                    for fn in entry.get("functions", []):
                        cov.functions[fn["name"]] += fn["execution_count"]
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    return files, version, len(gcdas)


# ── reporting ────────────────────────────────────────────────────────────

def pct(hit, total):
    return 100.0 * hit / total if total else 0.0


def bar(value, width=22):
    """A coarse visual, for scanning the table."""
    filled = int(round(value / 100.0 * width))
    return "█" * filled + "·" * (width - filled)


def fmt_pct(hit, total):
    if total == 0:
        return "     -"
    return "%5.1f%%" % pct(hit, total)


# One format for the header and the data rows, so the words sit over the
# numbers they label whatever the counts widen to.
ROW = "    %-22s %11s %6s  %11s %6s"


def row(name, lh, lt, bh, bt):
    return ROW % (name, "%d/%d" % (lh, lt), fmt_pct(lh, lt),
                  "%d/%d" % (bh, bt), fmt_pct(bh, bt))


def report(files, srcdir, version, nprofiles, show_uncovered):
    print()
    print("build-box coverage  --  gcc %s, %d profiles merged"
          % (version or "?", nprofiles))
    print()
    print(ROW % ("", "lines", "", "branches", ""))

    grand = [0, 0, 0, 0]  # line hit, line total, branch hit, branch total

    for src in sorted(files):
        cov = files[src]
        print(row(src[len(SOURCES):], cov.line_hit(), cov.line_total(),
                  cov.branch_hit(), cov.branch_total()))
        grand[0] += cov.line_hit()
        grand[1] += cov.line_total()
        grand[2] += cov.branch_hit()
        grand[3] += cov.branch_total()

    # A source that is in the tree but in no profile: never compiled
    # into a test program, or its process never got to write counters.
    silent = sorted(
        os.path.basename(p) for p in glob.glob(os.path.join(srcdir, SOURCES, "*.c"))
        if SOURCES + os.path.basename(p) not in files)
    for name in silent:
        print(ROW % (name, "-", "", "-", "") + "   (no profile)")

    print(row("--- total", grand[0], grand[1], grand[2], grand[3]))
    print("    %s %s" % (" " * 22, bar(pct(grand[0], grand[1]))))
    print()
    print("  Line coverage is the headline; branch coverage is the honest")
    print("  number.  A file whose lines climb while its branches do not is")
    print("  a file whose tests assert success and nothing else.")
    print()
    print("  A process that ends in exec, or ends inside a chroot, writes no")
    print("  counters.  login.c and run.c do both, so the shell tests that")
    print("  drive them are the coverage they have; see the note in this")
    print("  script.")
    print()

    if show_uncovered:
        print("  never entered:")
        print()
        for src in sorted(files):
            un = files[src].unentered()
            if un:
                print("    %s (%d/%d)" % (src, len(un),
                                          len(files[src].functions)))
                for name in un:
                    print("      %s" % name)
        print()


# ── which lines (rather than which files) ────────────────────────────────

def resolve_source(files, srcdir, want):
    """Match one command-line path against the collected file keys.

    The keys are relative to the source tree, but the tool is normally
    run from the build directory, so a path relative to *that* would not
    resolve.  Accept the key itself, a path relative to either tree, and
    an unambiguous basename.
    """
    if want in files:
        return want

    rel = os.path.relpath(os.path.realpath(want), os.path.realpath(srcdir))
    if rel in files:
        return rel

    base = os.path.basename(want)
    hits = [k for k in files if os.path.basename(k) == base]
    if len(hits) == 1:
        return hits[0]

    return None


def source_line(src, n):
    """The nth line of src (1-based), or a placeholder past the end."""
    return src[n - 1].strip() if 0 < n <= len(src) else "?"


def list_lines(files, srcdir, paths, show_branches):
    """Print the lines of each named file that no test reached.

    Read the counts out of the same aggregated data the report uses.  The
    temptation is to run gcov again by hand and parse its *text* output,
    and that is a trap worth naming: text .gcov marks a partly-executed
    line "5*", which a naive parser reads as uncovered, and it writes one
    file per object, so the several programs sharing a source have to be
    unioned by hand or half the hits go missing.  The JSON collect()
    already parses has exact counts and is already summed across objects.
    """
    rc = 0

    for want in paths:
        rel = resolve_source(files, srcdir, want)
        cov = files.get(rel) if rel else None

        if cov is None:
            rel = rel or want
            print("%s: no coverage data -- not built with --enable-coverage, "
                  "or never reached by the suite" % rel)
            rc = 1
            continue

        try:
            with open(os.path.join(srcdir, rel), errors="replace") as fh:
                src = fh.read().splitlines()
        except OSError as err:
            sys.exit("cannot read %s: %s" % (rel, err))

        # Anything not positive counts as unreached, which is how the
        # report scores it.  A *negative* count is gcov arithmetic gone
        # wrong rather than a line no test ran -- it is called out so it
        # is not mistaken for something a test could fix.
        dead = sorted(n for n, hits in cov.lines.items() if hits <= 0)
        print("%s: %d of %d countable lines not reached"
              % (rel, len(dead), len(cov.lines)))
        for n in dead:
            mark = " [gcov count %d]" % cov.lines[n] if cov.lines[n] < 0 else ""
            print("  %s:%d: %s%s" % (rel, n, source_line(src, n), mark))

        if not show_branches:
            continue

        untaken = defaultdict(list)
        for (n, i), hits in cov.branches.items():
            if hits == 0 and cov.lines.get(n, 0) > 0:
                untaken[n].append(i)

        print("%s: %d line(s) run with a branch never taken"
              % (rel, len(untaken)))
        for n in sorted(untaken):
            print("  %s:%d: [%s] %s"
                  % (rel, n, ",".join(str(i) for i in sorted(untaken[n])),
                     source_line(src, n)))

    return rc


# ── main ─────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(
        description="Aggregate gcov data into a per-file coverage report.")
    ap.add_argument("--build-dir", default=".",
                    help="top of the build tree to search for .gcda")
    ap.add_argument("--srcdir", default=".",
                    help="top of the source tree")
    ap.add_argument("--gcov", default=os.environ.get("GCOV", "gcov"),
                    help="the gcov to run; must match the compiler")
    ap.add_argument("--jobs", type=int, default=min(8, (os.cpu_count() or 2)),
                    help="parallel gcov invocations")
    ap.add_argument("--uncovered", action="store_true",
                    help="also list functions never entered")
    ap.add_argument("--lines", nargs="+", metavar="FILE",
                    help="list the uncovered lines of these sources "
                         "instead of the report")
    ap.add_argument("--branches", action="store_true",
                    help="with --lines, also list branches never taken")
    args = ap.parse_args()

    if not shutil.which(args.gcov):
        sys.exit("%s not found; it comes with gcc, so a coverage build "
                 "implies it" % args.gcov)

    files, version, nprofiles = collect(args.build_dir, args.srcdir,
                                        args.gcov, args.jobs)
    if args.lines:
        return list_lines(files, args.srcdir, args.lines, args.branches)

    report(files, args.srcdir, version, nprofiles, args.uncovered)
    return 0


if __name__ == "__main__":
    sys.exit(main())
