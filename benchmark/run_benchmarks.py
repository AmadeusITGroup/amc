#!/usr/bin/env python3
r"""Runs the benchmarks the way docs/benchmarks.md describes them (Method), then refreshes its charts and tables:

  python3 benchmark/run_benchmarks.py

It
  1. builds the benchmarks of every compiler (g++ and clang++-23 by default) before running any, so that a build failure
     (a compilation error, no network for the download of Google Benchmark...) stops it within minutes, not after the
     benchmarks of the first compiler. Each compiler builds in Release in a temporary directory: vectors_benchmark and
     sets_benchmark in C++17, and smallsets_benchmark in C++20, from which only the search of a SmallSet compares the
     looked up key once with each element. The directory is in $XDG_RUNTIME_DIR when set (a RAM disk on Linux
     desktops, to spare the SSD: a tree is about 30 MB), and removed once the executables, linked statically, are
     copied into the outputs directory. Unless it is installed, Google Benchmark is downloaded by the first build tree
     only, whose sources the next ones reuse;
  2. runs each benchmark in its own process, as the state in which a benchmark leaves the heap changes the times of the
     next ones (by up to 20 %), pinned to a single core with 'taskset', keeping the median of 5 repetitions. By default
     one benchmark runs at a time, on a core picked among the fastest ones of the machine (see --cpu).
Then 'benchmark/plot_benchmarks.py --import' stores the medians in docs/benchmarks/, draws docs/bench_*.svg and rewrites
the tables of docs/benchmarks.md.

A run which stops after the builds (Ctrl-C, a failed benchmark or import, a killed process...) keeps its outputs
directory and prints the command which continues it: --resume DIR only runs the benchmarks missing from DIR, with the
executables and the options of the stopped run, then imports the results as the stopped run would have. The default
outputs directory is in $XDG_RUNTIME_DIR though, which a reboot wipes.

The machine should be idle. --jobs N runs N benchmarks at the same time, each on its own core, almost N times faster.
The cores share the L3 cache and the memory bandwidth though: with 4 jobs on the 4 fastest cores of a Ryzen AI 9 HX PRO
370 (GCC 13, 58 benchmarks), lookups in sets of up to 10 000 elements and the vector benchmarks did not move beyond
the noise, but lookups in sets of 1 million elements and more were 8 % slower at the median and up to 28 % (and one
lookup in 100 000 elements was 2.1 times slower, which was not investigated). So the numbers of docs/ should come from
a run with a single job: --jobs is for quick checks and before / after comparisons, and does not import its results
(like --filter, which runs a part of the benchmarks).

The compiler tag of the stored results, such as 'gcc13' or 'clang23', comes from the version of the compiler: a new
major version adds results beside the stored ones, delete docs/benchmarks/*-<old tag>.json to replace them.

Only needs the Python standard library, CMake, a C++ compiler and (optionally) Ninja and taskset.
"""

import argparse
import glob
import json
import os
import queue
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import traceback
from concurrent.futures import ThreadPoolExecutor, as_completed

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLOT_SCRIPT = os.path.join(ROOT, "benchmark", "plot_benchmarks.py")

# (C++ standard, benchmarks built in it): the default of the project is C++20
BUILDS = ((17, ("vectors", "sets")), (20, ("smallsets",)))
KINDS = tuple(kind for _, kinds in BUILDS for kind in kinds)

# What --resume needs, kept in the outputs directory until the run is finished: the executables of the benchmarks (a
# directory per compiler tag) and the options of the run. Neither matches the <benchmark>-<compiler tag>-<n>.json
# outputs that plot_benchmarks.py --import reads.
BIN_DIR = "bin"
STATE_FILE = "run.json"


def run(cmd):
    print("+ " + shlex.join(cmd), flush=True)
    if subprocess.run(cmd).returncode != 0:
        sys.exit(f"failed: {shlex.join(cmd)}")


def compiler_tag(cxx):
    """'g++' -> 'gcc13', 'clang++-23' -> 'clang23': read from the predefined macros, as 'g++ -dumpversion' is not
    reliable (Clang answers 4.2.1)."""
    try:
        out = subprocess.run([cxx, "-dM", "-E", "-x", "c++", os.devnull], capture_output=True, text=True,
                             check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        sys.exit(f"cannot run the compiler '{cxx}': {e}")
    macros = {}
    for line in out.splitlines():
        parts = line.split(" ", 2)
        macros[parts[1]] = parts[2] if len(parts) > 2 else ""
    if "__clang_major__" in macros:
        return "clang" + macros["__clang_major__"]
    if "__GNUC__" in macros:
        return "gcc" + macros["__GNUC__"]
    sys.exit(f"'{cxx}' is neither GCC nor Clang")


def read_sysfs(path):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return None


def cpu_features(cpu):
    """(max frequency in kHz, L3 cache size in KiB, threads sharing the core) of a CPU, from sysfs. What it does not
    tell (virtual machines...) is 0, and the CPU alone for its core."""
    base = f"/sys/devices/system/cpu/cpu{cpu}"
    freq = int(read_sysfs(f"{base}/cpufreq/cpuinfo_max_freq") or 0)
    l3 = 0
    for index in range(8):
        if read_sysfs(f"{base}/cache/index{index}/level") == "3":
            size = read_sysfs(f"{base}/cache/index{index}/size") or "0K"
            l3 = int(size[:-1]) * {"K": 1, "M": 1024, "G": 1024 * 1024}[size[-1]]
            break
    return freq, l3, read_sysfs(f"{base}/topology/thread_siblings_list") or str(cpu)


def pick_cores(count):
    """Picks 'count' CPUs of the fastest cores this process may use: the highest max frequency, then the biggest L3
    cache, which tells the fast cores of a hybrid CPU from the slow ones. One thread per core, as the other one slows it
    down. Returns the CPUs and a description of them.

    The first one is the middle one of the fastest cores, never the first one (CPU 0 handles most of the interrupts)
    unless there is a single fast core, and the next ones follow it, wrapping around."""
    allowed = sorted(os.sched_getaffinity(0))
    features = {cpu: cpu_features(cpu) for cpu in allowed}
    best = max(f[:2] for f in features.values())
    first_thread = {}  # core -> its first fast thread
    for cpu in allowed:
        if features[cpu][:2] == best:
            first_thread.setdefault(features[cpu][2], cpu)
    cores = sorted(first_thread.values())
    if count > len(cores):
        sys.exit(f"--jobs {count}: the machine has {len(cores)} fastest cores (CPUs {', '.join(map(str, cores))})")
    middle = len(cores) // 2
    picked = (cores[middle:] + cores[:middle])[:count]
    if best == (0, 0):
        return picked, "the CPU frequencies and caches are not readable from sysfs: any CPU"
    return picked, f"{best[0] / 1e6:.2f} GHz, L3 {best[1] // 1024} MiB"


def resolve_cores(args):
    """CPUs to run the benchmarks on, a benchmark at a time on each: [None] when they cannot be pinned."""
    if not shutil.which("taskset") or not hasattr(os, "sched_getaffinity"):
        if args.cpu != "auto" or args.jobs != 1:
            sys.exit("pinning the benchmarks to cores needs taskset, on Linux")
        print("warning: taskset not found, the benchmarks are not pinned to a core")
        return [None]
    if args.cpu == "auto":
        cores, description = pick_cores(args.jobs)
        print(f"benchmarks pinned to CPU {', '.join(map(str, cores))} ({description})", flush=True)
        return cores
    if args.jobs != 1:
        sys.exit("--jobs is for --cpu auto: with a list of CPUs, a benchmark runs on each of them at the same time")
    try:
        cores = [int(cpu) for cpu in args.cpu.split(",")]
    except ValueError:
        sys.exit(f"--cpu is 'auto' or a list of CPUs, like 2 or 2,3 (not '{args.cpu}')")
    unavailable = sorted(set(cores) - os.sched_getaffinity(0))
    if unavailable:
        sys.exit(f"CPU {', '.join(map(str, unavailable))} not available, choose others with --cpu (see 'lscpu -e')")
    if len(set(cores)) != len(cores):
        sys.exit(f"--cpu lists a CPU twice: {args.cpu}")
    return cores


def build(cxx, tag, tree, std, kinds, benchmark_src):
    """Configures (or reconfigures with another C++ standard) and builds the benchmarks 'kinds' in 'tree'. Google
    Benchmark comes from the sources in 'benchmark_src' when not None, instead of a download (unless installed)."""
    # -Wno-c2y-extensions: Google Benchmark 1.9.4 builds itself with -pedantic-errors and uses __COUNTER__, which
    # Clang 23 rejects
    flags = "-Wno-unknown-warning-option -Wno-c2y-extensions" if tag.startswith("clang") else ""
    # CMP0135=NEW: silences the warning of the download of Google Benchmark, a fresh tree has no timestamps to keep
    configure = ["cmake", "-S", ROOT, "-B", tree, "--log-level=NOTICE", "-DCMAKE_POLICY_DEFAULT_CMP0135=NEW",
                 "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_CXX_COMPILER={cxx}", f"-DCMAKE_CXX_STANDARD={std}",
                 f"-DCMAKE_CXX_FLAGS={flags}", "-DAMC_ENABLE_TESTS=OFF", "-DAMC_ENABLE_BENCHMARKS=ON"]
    if benchmark_src:
        configure.append(f"-DFETCHCONTENT_SOURCE_DIR_GOOGLEBENCHMARK={benchmark_src}")
    if shutil.which("ninja"):
        configure += ["-G", "Ninja"]
    run(configure)
    run(["cmake", "--build", tree, "--target"] + [f"{kind}_benchmark" for kind in kinds])


def build_all(compilers, out, ram):
    """Builds the benchmarks of every compiler, each in a temporary tree in 'ram', and copies the executables into
    <out>/bin/<tag>/. The first tree which downloads Google Benchmark is kept until the end, as the next trees build
    its sources (passed before their first configure: switching the sources of a configured tree rebuilds them)."""
    trees = []
    benchmark_src = None
    try:
        for cxx, tag in compilers:
            tree = tempfile.mkdtemp(prefix=f"amc-bench-{tag}-", dir=ram)
            trees.append(tree)
            bin_dir = os.path.join(out, BIN_DIR, tag)
            os.makedirs(bin_dir, exist_ok=True)
            for std, kinds in BUILDS:
                build(cxx, tag, tree, std, kinds, benchmark_src)
                for kind in kinds:
                    shutil.copy2(os.path.join(tree, "benchmark", f"{kind}_benchmark"), bin_dir)
            fetched = os.path.join(tree, "_deps", "googlebenchmark-src")
            if benchmark_src is None and os.path.isdir(fetched):
                benchmark_src = fetched
            else:
                shutil.rmtree(trees.pop(), ignore_errors=True)
    finally:
        for tree in trees:
            shutil.rmtree(tree, ignore_errors=True)


def regex_escape(name):
    """Google Benchmark filters are regular expressions: the names have '<', '>', '/', ':' and '.' (min_time)."""
    return re.sub(r"([.^$|()\[\]{}*+?\\])", r"\\\1", name)


def run_benchmarks(out, tag, args, cores):
    """Runs each benchmark of the compiler 'tag' in its own process, pinned to one of 'cores' (each runs one benchmark
    at a time), and writes <kind>-<tag>-<n>.json in 'out'. With --resume, skips the benchmarks whose output exists: an
    output only gets this name once its benchmark is finished."""
    selection = [f"--benchmark_filter={args.filter}"] if args.filter else []
    min_time = [f"--benchmark_min_time={args.min_time:g}s"] if args.min_time else []
    tasks = []
    done = 0
    for kind in KINDS:
        exe = os.path.join(out, BIN_DIR, tag, f"{kind}_benchmark")
        names = subprocess.run([exe, "--benchmark_list_tests=true"] + selection, capture_output=True, text=True,
                               check=True).stdout.splitlines()
        for i, name in enumerate(names, 1):
            path = os.path.join(out, f"{kind}-{tag}-{i}.json")
            if args.resume and os.path.exists(path):
                done += 1
            else:
                tasks.append((kind, exe, i, name, len(names), path))
    if args.resume:
        print(f"[{tag}] {done} benchmarks already done, {len(tasks)} to run", flush=True)

    free_cores = queue.Queue()
    for core in cores:
        free_cores.put(core)
    print_lock = threading.Lock()

    def run_one(task):
        kind, exe, i, name, count, path = task
        partial = path + ".part"  # an interrupted benchmark leaves no output, --resume runs it again
        core = free_cores.get()
        try:
            with print_lock:
                print(f"[{tag} {kind} {i}/{count}] {name}", flush=True)
            pin = [] if core is None else ["taskset", "-c", str(core)]
            # The console report is not needed (nor the machine header of its stderr), the JSON file has everything
            result = subprocess.run(pin + [exe, f"--benchmark_filter=^{regex_escape(name)}$",
                                           f"--benchmark_repetitions={args.repetitions}",
                                           "--benchmark_report_aggregates_only=true",
                                           f"--benchmark_out={partial}",
                                           "--benchmark_out_format=json"] + min_time, capture_output=True, text=True)
        finally:
            free_cores.put(core)
        if result.returncode != 0:
            sys.exit(f"{exe} failed with status {result.returncode}:\n{result.stdout}{result.stderr}")
        os.replace(partial, path)

    with ThreadPoolExecutor(max_workers=len(cores)) as pool:
        futures = [pool.submit(run_one, task) for task in tasks]
        try:
            for future in as_completed(futures):
                future.result()
        except BaseException:  # a failed benchmark or Ctrl-C: do not start the others
            for future in futures:
                future.cancel()
            raise


def save_state(out, args, compilers):
    """Writes what --resume needs into the outputs directory 'out': the options and the compilers of the run."""
    options = {name: value for name, value in vars(args).items() if name != "resume"}
    with open(os.path.join(out, STATE_FILE), "w") as f:
        json.dump({"options": options, "compilers": compilers}, f, indent=1)


def load_state(out, defaults):
    """The options (with 'resume' set to 'out') and the compilers of the stopped run of the outputs directory 'out'. The
    options it did not save, added to the script since, have their value of 'defaults'."""
    try:
        with open(os.path.join(out, STATE_FILE)) as f:
            state = json.load(f)
    except OSError:
        sys.exit(f"--resume {out}: no stopped run there, there is no {STATE_FILE} (a finished run removes it, a reboot "
                 "wipes the RAM disk)")
    options = {**defaults, **state["options"], "resume": out}
    return argparse.Namespace(**options), [tuple(compiler) for compiler in state["compilers"]]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cxx", action="append", metavar="COMPILER",
                        help="C++ compiler to benchmark, repeatable (default: g++ and clang++-23)")
    parser.add_argument("--cpu", default="auto", metavar="auto|CPU[,CPU...]",
                        help="CPU to pin the benchmarks to (default: auto, a core among the fastest ones of the "
                        "machine: highest frequency, then biggest L3 cache). A list runs a benchmark on each CPU at "
                        "the same time")
    parser.add_argument("--jobs", type=int, default=1, metavar="N",
                        help="with --cpu auto, number of benchmarks run at the same time, each on its own fastest "
                        "core: faster, but slows down lookups in large sets (default: 1, which the numbers of docs/ "
                        "should come from)")
    parser.add_argument("--repetitions", type=int, default=5, help="repetitions of each benchmark (default: 5)")
    parser.add_argument("--min-time", type=float, metavar="SECONDS",
                        help="minimum time of a repetition of the benchmarks without a fixed number of iterations "
                        "(default: the one of Google Benchmark, 0.5 s). A short one, like 0.01, is for quick checks")
    parser.add_argument("--out", metavar="DIR",
                        help="directory of the Google Benchmark JSON outputs, kept (default: a temporary one, "
                        "removed after the import). Also holds the executables until the end of the run")
    parser.add_argument("--no-import", action="store_true",
                        help="only run the benchmarks, do not update docs/ (the outputs are kept)")
    parser.add_argument("--filter", metavar="REGEX",
                        help="only run the benchmarks matching REGEX, for a quick check: implies --no-import, as "
                        "partial results would replace the stored ones")
    parser.add_argument("--main", metavar="TAG",
                        help="compiler tag of the vector and set charts, as plot_benchmarks.py --main "
                        "(default: gcc13)")
    parser.add_argument("--resume", metavar="DIR",
                        help="continue a run which stopped after its builds, from its outputs directory DIR (printed "
                        "when it stopped): only runs the benchmarks missing from DIR, with the executables and the "
                        "options of the stopped run (no other option is accepted), then finishes like it would have")
    args = parser.parse_args()

    if args.resume:
        others = [f"--{name.replace('_', '-')}" for name, value in vars(args).items()
                  if name != "resume" and value != parser.get_default(name)]
        if others:
            parser.error(f"--resume continues the stopped run with its own options, remove {' '.join(others)}")
        args, compilers = load_state(args.resume, {name: parser.get_default(name) for name in vars(args)})
        out = args.resume
        print(f"resuming the run of {out}", flush=True)
    else:
        compilers = [(cxx, compiler_tag(cxx)) for cxx in args.cxx or ["g++", "clang++-23"]]
        tags = [tag for _, tag in compilers]
        if len(set(tags)) != len(tags):
            sys.exit(f"several compilers have the same tag: {', '.join(tags)}")
        if args.jobs < 1:
            parser.error("--jobs must be at least 1")
        if args.repetitions < 1:
            parser.error("--repetitions must be at least 1")
        if args.min_time is not None and args.min_time <= 0:
            parser.error("--min-time must be positive")
        if args.out and os.path.exists(os.path.join(args.out, STATE_FILE)):
            sys.exit(f"{args.out} holds a stopped run: continue it with --resume {args.out}, or remove it")
    cores = resolve_cores(args)
    if hasattr(os, "getloadavg") and os.getloadavg()[0] > 2:
        print(f"warning: load average {os.getloadavg()[0]:.1f}, the machine should be idle")
    if len(cores) > 1:
        print(f"warning: {len(cores)} benchmarks at the same time: the times of the lookups in large sets are not "
              "those of a single job, so the results are not imported into docs/")
    do_import = not (args.no_import or args.filter or len(cores) > 1)

    if not args.resume:
        ram = os.environ.get("XDG_RUNTIME_DIR")  # None: the default temporary directory
        out = args.out or tempfile.mkdtemp(prefix="amc-bench-results-", dir=ram)
        os.makedirs(out, exist_ok=True)
        try:
            build_all(compilers, out, ram)
        except BaseException:  # nothing to resume: a new run only loses the builds
            shutil.rmtree(os.path.join(out, BIN_DIR) if args.out else out, ignore_errors=True)
            raise
        save_state(out, args, compilers)

    start = time.monotonic()
    try:
        for _, tag in compilers:
            run_benchmarks(out, tag, args, cores)
        print(f"benchmarks done in {(time.monotonic() - start) / 60:.0f} min", flush=True)
        if do_import:
            run([sys.executable, PLOT_SCRIPT, "--import", out] + (["--main", args.main] if args.main else []))
    except BaseException as e:
        if not isinstance(e, (KeyboardInterrupt, SystemExit)):
            traceback.print_exc()
        reason = ("interrupted" if isinstance(e, KeyboardInterrupt) else e.code if isinstance(e, SystemExit)
                  else "failed")
        done = len(glob.glob(os.path.join(out, "*-*.json")))
        sys.exit(f"{reason}\n{done} benchmark outputs are kept in {out}, continue the run with:\n  "
                 + shlex.join(["python3", sys.argv[0], "--resume", os.path.abspath(out)]))

    # The run is finished, it cannot be resumed anymore
    shutil.rmtree(os.path.join(out, BIN_DIR), ignore_errors=True)
    os.remove(os.path.join(out, STATE_FILE))
    if not do_import:
        print(f"results in {out}, import them with: python3 benchmark/plot_benchmarks.py --import {out}")
    elif not args.out:
        shutil.rmtree(out, ignore_errors=True)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit("interrupted")
