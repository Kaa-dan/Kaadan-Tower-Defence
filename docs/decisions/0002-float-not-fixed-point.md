# 0002 — Float with discipline, not fixed-point

**Status:** accepted · 2026-08-20

## Context

The simulation must be bit-identical across macOS, Windows, and Linux so replays work. The
classic solution in RTS and lockstep games is fixed-point arithmetic.

## Decision

**Keep `float`, and enumerate and eliminate the specific hazards.**

## Why

IEEE-754 binary32 `+ - * /` and `sqrt` are **correctly rounded and exactly specified**. Every
targeted compiler emits `addss`/`mulss`/`sqrtss` on x86-64 or `fadd`/`fsqrt` on ARM64, and
both give bit-identical results.

The historical reason games went fixed-point was x87's 80-bit excess precision. That hazard is
**dead** on x86-64 (SSE2 is mandatory) and ARM64. This project does not ship 32-bit x86.

What actually diverges is a short, enumerable, mechanically enforceable list:

1. Transcendental libm — `pow`, `sin`, `exp`, `atan2`. Not correctly rounded; differs between
   glibc, MSVC, and Apple libm. **This is the one confirmed live bug** (`World.cpp:74-75`).
2. FMA contraction — needs `-ffp-contract=off` / `/fp:precise`.
3. `-ffast-math` / `/fp:fast`.
4. Float-to-int conversion of out-of-range values (UB, differs per architecture).
5. Non-default rounding mode.

Fixed-point would cost a full rewrite of all simulation math, permanent readability damage, a
hand-rolled `sqrt`, a new class of overflow bugs, and no performance gain.

## Consequences

- `game/Math.h` becomes the only file permitted to include `<cmath>`, enforced by
  `purity_determinism`.
- `ktd_game` compiles with `-ffp-contract=off` / `/fp:precise`, and `-ffast-math` is denied at
  configure time.
- Integer tick counters replace float timers where exactness matters.
- A cross-OS hash comparison in CI is **mandatory** — the discipline is only trustworthy if a
  machine checks it. Without that job, this decision is a hope rather than a property.
