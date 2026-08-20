# 0005 — Hand-rolled data format, not JSON or TOML

**Status:** accepted · 2026-08-20

## Context

Towers, enemies, waves, items, and skill nodes move out of `Config.h` into data files. The
obvious choices are JSON or TOML via a library.

## Decision

**A documented TOML subset with a hand-written parser (~300 lines), including a deterministic
float parser.**

## Why

The dependency argument is real but secondary. The stronger argument is **determinism of
number parsing**: `std::stof` / `strtof` / `std::from_chars` are correctly rounded *per spec*,
but implementation quality varies across libstdc++, libc++, and MSVC. That is not something to
make load-bearing for bit-identical cross-platform balance.

Hand-rolling lets the behavior be defined exactly — accumulate an integer mantissa in a
`uint64`, then perform one `double` division by an exact power of ten, then narrow. A single
IEEE division of two exactly-representable doubles is correctly rounded everywhere. ~40 lines
that remove a whole category of "why does Windows balance differently".

Against the alternatives: JSON has no comments, which is fatal for a balance file you will live
in for two years. Full TOML is a large spec — datetimes, inline tables, dotted keys — most of
which will never be used.

## Consequences

- The subset hard-errors on anything outside it rather than guessing. Errors carry
  `file:line:col` and are collected rather than thrown at the first one.
- `game/` owns the parser and operates on `std::string_view`. **Parsing a string is not file
  I/O**, so purity holds and the parser is unit-testable from string literals without touching
  disk.
- The platform layer reads the bytes and hands them in.
