#!/usr/bin/env python3
"""Compare ktd_hashdump traces from several platforms.

Exits non-zero and names the first tick where any two disagree.

The reason this is not `diff`: with three traces, diff tells you that two files
differ somewhere. What you need is the earliest tick where the platforms stopped
agreeing, and which platform is the odd one out -- everything after the first
divergence is downstream noise, and reading it wastes the afternoon.

Usage:
    compare_traces.py ubuntu=ubuntu.tsv macos=macos.tsv windows=windows.tsv
"""

import sys


def load(path):
    """Return {tick: hash}, tolerating CRLF from a Windows runner."""
    rows = {}
    with open(path, "r", encoding="utf-8") as handle:
        for lineno, line in enumerate(handle, start=1):
            line = line.strip()
            if not line:
                continue
            parts = line.split("\t")
            if len(parts) != 2:
                sys.exit(f"{path}:{lineno}: expected 'tick<TAB>hash', got {line!r}")
            rows[int(parts[0])] = parts[1].lower()
    return rows


def main(argv):
    if len(argv) < 3:
        sys.exit(f"usage: {argv[0]} name=trace.tsv name=trace.tsv [name=trace.tsv ...]")

    traces = {}
    for arg in argv[1:]:
        if "=" not in arg:
            sys.exit(f"expected name=path, got {arg!r}")
        name, path = arg.split("=", 1)
        traces[name] = load(path)

    names = sorted(traces)
    lengths = {name: len(traces[name]) for name in names}
    if len(set(lengths.values())) != 1:
        print("Traces have different lengths, so at least one run ended early:")
        for name in names:
            print(f"  {name}: {lengths[name]} ticks")

    # Compare over the ticks every platform actually reached, so a truncated
    # run still reports where the values diverged rather than only that it
    # was short.
    common = set(traces[names[0]])
    for name in names[1:]:
        common &= set(traces[name])

    for tick in sorted(common):
        values = {name: traces[name][tick] for name in names}
        if len(set(values.values())) == 1:
            continue
        print(f"\nDIVERGENCE at tick {tick}:")
        for name in names:
            print(f"  {name:<12} {values[name]}")
        # Point at the odd one out when there is a clear majority; with three
        # platforms this is usually enough to know where to start looking.
        counts = {}
        for value in values.values():
            counts[value] = counts.get(value, 0) + 1
        majority = max(counts, key=lambda v: counts[v])
        if counts[majority] > 1:
            odd = [n for n in names if values[n] != majority]
            print(f"\n  {', '.join(odd)} disagree(s) with the others.")
        print(f"\n  Reproduce: build ktd_hashdump and inspect tick {tick} on each platform.")
        return 1

    if len(set(lengths.values())) != 1:
        return 1

    print(f"All {len(names)} traces agree over {len(common)} ticks: {', '.join(names)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
