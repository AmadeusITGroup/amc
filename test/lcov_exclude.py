#!/usr/bin/env python3
"""Removes from an lcov tracefile the lines that the sources exclude from the coverage, with the markers of lcov (which
llvm-cov does not know):
  - a line containing LCOV_EXCL_LINE,
  - the lines from one containing LCOV_EXCL_START to the next one containing LCOV_EXCL_STOP, both included.
Their records (DA, BRDA, and FN with its FNDA) are removed, and subtracted from the counts of the file (LF, LH, BRF,
BRH, FNF, FNH), which are those of llvm-cov: they count each line by its best instantiation, and the lines of a macro
in the file of its use only, so they cannot be recomputed from the records. Meant for the lines which no test can
execute, like the branch of a __builtin_unreachable.

Usage: lcov_exclude.py <input.lcov> <output.lcov> (they may be the same file)
"""

import sys


def excluded_lines(path):
    """The numbers of the excluded lines of the source file 'path' (none if it cannot be read)."""
    lines = set()
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            start = None
            for number, text in enumerate(f, 1):
                if start is None and "LCOV_EXCL_START" in text:
                    start = number
                if start is not None:
                    lines.add(number)
                    if "LCOV_EXCL_STOP" in text:
                        start = None
                elif "LCOV_EXCL_LINE" in text:
                    lines.add(number)
    except OSError:
        return lines
    if start is not None:
        sys.exit(f"{path}:{start}: LCOV_EXCL_START without LCOV_EXCL_STOP")
    return lines


def line_of(record):
    """The source line of a DA, BRDA or FN record ('DA:12,3' -> 12)."""
    return int(record.split(":", 1)[1].split(",", 1)[0])


def filter_file(records, excluded):
    """The records of one source file (from SF to end_of_record) without those of the 'excluded' lines."""
    removed_functions = {r.split(",", 1)[1] for r in records if r.startswith("FN:") and line_of(r) in excluded}
    kept = []
    removed = []
    for r in records:
        if (r.startswith(("DA:", "BRDA:", "FN:")) and line_of(r) in excluded) or (
            r.startswith("FNDA:") and r.split(",", 1)[1] in removed_functions
        ):
            removed.append(r)
        else:
            kept.append(r)

    def values(tag, field):
        return [r.split(":", 1)[1].split(",")[field] for r in removed if r.startswith(tag)]

    lines = values("DA:", 1)
    branches = values("BRDA:", 3)
    functions = values("FNDA:", 0)
    minus = {
        "LF": len(lines),
        "LH": sum(1 for c in lines if int(c) > 0),
        "BRF": len(branches),
        "BRH": sum(1 for c in branches if c != "-" and int(c) > 0),
        "FNF": len(functions),
        "FNH": sum(1 for c in functions if int(c) > 0),
    }
    result = []
    for r in kept:
        tag, _, value = r.partition(":")
        result.append(f"{tag}:{max(0, int(value) - minus[tag])}" if tag in minus else r)
    return result


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    with open(sys.argv[1], encoding="utf-8") as f:
        records = f.read().splitlines()

    output = []
    nb_removed = 0
    file_records = []
    for r in records:
        file_records.append(r)
        if r != "end_of_record":
            continue
        source = file_records[0][3:] if file_records[0].startswith("SF:") else None
        excluded = excluded_lines(source) if source else set()
        if excluded:
            filtered = filter_file(file_records, excluded)
            nb_removed += len(file_records) - len(filtered)
            file_records = filtered
        output.extend(file_records)
        file_records = []
    output.extend(file_records)

    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write("\n".join(output) + "\n")
    print(f"lcov_exclude: {nb_removed} records of excluded lines removed")


if __name__ == "__main__":
    main()
