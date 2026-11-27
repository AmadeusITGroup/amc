#!/usr/bin/env python3
"""Verify tools/gdb/amc_printers.py against test/gdb_printers_test."""

import os
import re
import subprocess
import sys


VARIABLES = [
    "emptyVector",
    "intVector",
    "stringVector",
    "emptySmallVector",
    "smallVectorInline",
    "smallVectorHeap",
    "smallVectorBackInline",
    "charVector",
    "fixedVector",
    "emptyFixedVector",
    "inplaceVector",
    "nestedVector",
    "largeVector",
    "emptySortedSet",
    "sortedSet",
    "sortedStringSet",
    "sortedSetInline",
    "sortedSetStdVector",
    "emptyInlineSet",
    "inlineSet",
    "largeSet",
    "largeFlatSet",
    "inlineStringSet",
    "backInlineSet",
    "itInlineSet",
    "itLargeSet",
    "emptyNode",
    "setNode",
    "stringNode",
]


def run_gdb(gdb_bin, printers_py, test_bin):
    cmds = [
        "set pagination off",
        "set width 0",
        "set print elements 0",
        "set print repeats 0",
        f"source {printers_py}",
        "echo ===PRINTERS_BEGIN===\\n",
        "info pretty-printer",
        "echo ===PRINTERS_END===\\n",
        "break amc_gdb_test_breakpoint",
        "run",
        "up",
    ]
    for var in VARIABLES:
        cmds.append(f"echo ===VAR:{var}===\\n")
        cmds.append(f"print {var}")
    # Also verify that GDB truncation ('set print elements 6') works lazily.
    cmds.append("set print elements 6")
    cmds.append("echo ===TRUNC_VECTOR===\\n")
    cmds.append("print largeVector")
    cmds.append("echo ===PREFIX_CHECK===\\n")
    cmds.append(
        "python "
        "assert _VECTOR_PREFIX_RE.match(_template_prefix('amc::Vector<int, int, int, int, 0>')); "
        "assert not _VECTOR_PREFIX_RE.match(_template_prefix('amc::vec::SmallVectorBase<int, int, int>')); "
        "assert _FLAT_SET_PREFIX_RE.match(_template_prefix('amc::FlatSet<int>')); "
        "assert _template_prefix('amc::FlatSet<int>::CompareHolder<int>') is None; "
        "assert _SMALL_SET_PREFIX_RE.match(_template_prefix('amc::SmallSet<int, 4>')); "
        "assert _SMALL_SET_ITER_PREFIX_RE.match(_template_prefix('amc::SmallSetIterator<int, int>')); "
        "assert _NODE_HANDLE_PREFIX_RE.match(_template_prefix('amc::set_details::NodeHandle<int, int>')); "
        "print('PREFIX_OK')"
    )
    cmds.append("echo ===DONE===\\n")
    cmds.append("quit")

    args = [gdb_bin, "-nx", "-batch"]
    for cmd in cmds:
        args.extend(["-ex", cmd])
    args.append(test_bin)

    env = os.environ.copy()
    # LeakSanitizer uses ptrace, which conflicts with GDB attaching to the inferior.
    asan_opts = env.get("ASAN_OPTIONS", "")
    if "detect_leaks=" not in asan_opts:
        env["ASAN_OPTIONS"] = (asan_opts + ":" if asan_opts else "") + "detect_leaks=0"

    proc = subprocess.run(args, capture_output=True, text=True, env=env, check=False)
    if proc.returncode != 0:
        sys.stderr.write(f"GDB exited with code {proc.returncode}\n")
        sys.stderr.write("--- stdout ---\n" + proc.stdout + "\n")
        sys.stderr.write("--- stderr ---\n" + proc.stderr + "\n")
        sys.exit(1)
    return proc.stdout


def parse_outputs(stdout):
    m = re.search(r"===PRINTERS_BEGIN===\n(.*?)===PRINTERS_END===", stdout, re.S)
    if not m:
        raise AssertionError(f"Missing pretty-printer listing in GDB output:\n{stdout}")
    printers_info = m.group(1)

    values = {}
    for i, var in enumerate(VARIABLES):
        start_marker = f"===VAR:{var}===\n"
        end_marker = (
            f"===VAR:{VARIABLES[i + 1]}===\n"
            if i + 1 < len(VARIABLES)
            else "===TRUNC_VECTOR===\n"
        )
        start = stdout.find(start_marker)
        end = stdout.find(end_marker, start if start >= 0 else 0)
        if start < 0 or end < 0:
            raise AssertionError(f"Missing output for {var} in GDB output:\n{stdout}")
        raw = stdout[start + len(start_marker) : end].strip()
        # Strip leading "$N = " from GDB's print output.
        raw = re.sub(r"^\$\d+\s*=\s*", "", raw)
        values[var] = raw

    for tag, next_tag in [
        ("TRUNC_VECTOR", "PREFIX_CHECK"),
        ("PREFIX_CHECK", "DONE"),
    ]:
        s_mark = f"==={tag}===\n"
        e_mark = f"==={next_tag}===\n" if next_tag != "DONE" else "===DONE==="
        start = stdout.find(s_mark)
        end = stdout.find(e_mark, start if start >= 0 else 0)
        if start < 0 or end < 0:
            raise AssertionError(f"Missing output for {tag} in GDB output:\n{stdout}")
        raw = stdout[start + len(s_mark) : end].strip()
        values[tag] = re.sub(r"^\$\d+\s*=\s*", "", raw)

    return printers_info, values


def check_exact(values, var, expected):
    actual = values[var]
    if actual != expected:
        raise AssertionError(
            f"Mismatch for {var}:\n  expected: {expected}\n  actual:   {actual}"
        )


def main():
    if len(sys.argv) != 4:
        sys.stderr.write(
            f"Usage: {sys.argv[0]} <gdb> <amc_printers.py> <gdb_printers_test>\n"
        )
        sys.exit(2)

    gdb_bin, printers_py, test_bin = sys.argv[1:4]
    stdout = run_gdb(gdb_bin, printers_py, test_bin)
    printers_info, values = parse_outputs(stdout)

    if "amc" not in printers_info:
        raise AssertionError(f"amc pretty-printers not registered:\n{printers_info}")
    for name in ("Vector", "FlatSet", "SmallSet", "SmallSetIterator", "NodeHandle"):
        if name not in printers_info:
            raise AssertionError(f"amc pretty-printer {name} not registered:\n{printers_info}")

    check_exact(values, "PREFIX_CHECK", "PREFIX_OK")

    # Vectors (64-bit capacities). SmallVector: the inline state has no flag, its members are encoded.
    check_exact(values, "emptyVector", "amc::vector with 0 elements, capacity 0")
    check_exact(values, "intVector", "amc::vector with 3 elements, capacity 8 = {1, 2, 3}")
    check_exact(
        values, "stringVector", 'amc::vector with 2 elements, capacity 2 = {"alpha", "beta"}'
    )
    check_exact(
        values, "emptySmallVector", "amc::SmallVector with 0 elements, capacity 4, inline"
    )
    check_exact(
        values,
        "smallVectorInline",
        "amc::SmallVector with 3 elements, capacity 4, inline = {1, 2, 3}",
    )
    check_exact(
        values,
        "smallVectorHeap",
        "amc::SmallVector with 6 elements, capacity 8 = {1, 2, 3, 4, 5, 6}",
    )
    check_exact(
        values,
        "smallVectorBackInline",
        "amc::SmallVector with 2 elements, capacity 4, inline = {1, 2}",
    )
    # char is printed as e.g. "97 'a'" by GDB: check the header and the integer values.
    actual_chars = values["charVector"]
    if not actual_chars.startswith("amc::SmallVector with 3 elements, capacity 8, inline = {"):
        raise AssertionError(f"Unexpected charVector: {actual_chars}")
    char_nums = [int(m.group(1)) for m in re.finditer(r"(\d+)\s+'[^']*'", actual_chars)]
    if char_nums != [97, 98, 99]:
        raise AssertionError(f"Unexpected charVector elements: {actual_chars}")
    check_exact(
        values, "fixedVector", "amc::FixedCapacityVector with 2 elements, capacity 5 = {4, 5}"
    )
    check_exact(
        values, "emptyFixedVector", "amc::FixedCapacityVector with 0 elements, capacity 3"
    )
    check_exact(
        values, "inplaceVector", "amc::inplace_vector with 1 element, capacity 4 = {7}"
    )
    check_exact(
        values,
        "nestedVector",
        "amc::vector with 2 elements, capacity 2 = {"
        "amc::SmallVector with 2 elements, capacity 2, inline = {1, 2}, "
        "amc::SmallVector with 1 element, capacity 2, inline = {3}}",
    )
    large_prefix = "amc::vector with 1000 elements, capacity 1024 = {"
    actual_large = values["largeVector"]
    if not actual_large.startswith(large_prefix) or not actual_large.endswith("}"):
        raise AssertionError(f"Unexpected largeVector: {actual_large[:200]}")
    if [int(item) for item in actual_large[len(large_prefix) : -1].split(", ")] != list(range(1000)):
        raise AssertionError(f"Unexpected largeVector elements: {actual_large[:200]}")
    # Truncated print ('set print elements 6'), whose format depends on the version of GDB.
    if values["TRUNC_VECTOR"] not in (
        "amc::vector with 1000 elements, capacity 1024 = {0, 1, 2, 3, 4, 5...}",
        "amc::vector with 1000 elements, capacity 1024 = {0, 1, 2, 3, 4, 5, ...}",
    ):
        raise AssertionError(f"Unexpected TRUNC_VECTOR: {values['TRUNC_VECTOR']}")

    # FlatSet, in amc vectors and in a std::vector.
    check_exact(values, "emptySortedSet", "amc::FlatSet with 0 elements")
    check_exact(values, "sortedSet", "amc::FlatSet with 3 elements = {1, 3, 5}")
    check_exact(
        values, "sortedStringSet", 'amc::FlatSet with 2 elements = {"alpha", "beta"}'
    )
    check_exact(values, "sortedSetInline", "amc::FlatSet with 2 elements = {1, 2}")
    check_exact(values, "sortedSetStdVector", "amc::FlatSet with 2 elements = {8, 9}")

    # SmallSet: inline in insertion order, then in its set (std::set or FlatSet), inline again once emptied.
    check_exact(values, "emptyInlineSet", "amc::SmallSet with 0 elements, inline")
    check_exact(values, "inlineSet", "amc::SmallSet with 3 elements, inline = {3, 1, 2}")
    check_exact(values, "largeSet", "amc::SmallSet with 3 elements = {4, 5, 6}")
    check_exact(values, "largeFlatSet", "amc::SmallSet with 3 elements = {4, 5, 6}")
    check_exact(
        values, "inlineStringSet", 'amc::SmallSet with 1 element, inline = {"one"}'
    )
    check_exact(values, "backInlineSet", "amc::SmallSet with 0 elements, inline")
    check_exact(values, "itInlineSet", "amc::SmallSetIterator (inline) = {value = 1}")
    # the element of an iterator of the std::set is printed by the libstdc++ printer of this iterator
    check_exact(values, "itLargeSet", "amc::SmallSetIterator = {value = 5}")

    # Node handles.
    check_exact(values, "emptyNode", "amc::NodeHandle (empty)")
    check_exact(values, "setNode", "amc::NodeHandle = {value = 10}")
    check_exact(values, "stringNode", 'amc::NodeHandle = {value = "x"}')

    print("All GDB pretty-printer checks passed.")


if __name__ == "__main__":
    main()
