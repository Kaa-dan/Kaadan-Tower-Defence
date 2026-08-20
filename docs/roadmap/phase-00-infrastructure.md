# Phase 0 — Infrastructure ✅

**Shipped:** commit `340f0a7`, branch `build/phase-0-infrastructure` (pushed, PR not yet
opened). 13 files, +433/−54. No simulation or engine behavior change.

## What landed

- `CMakePresets.json` — `dev`, `release`, `core`, `core-release`, `asan`.
- `cmake/KtdWarnings.cmake` — `-Werror`/`/WX` plus `-Wshadow`, `-Wold-style-cast`,
  `-Wfloat-equal` on all four targets. The tree was already clean.
- `KTD_TESTS_ONLY` — builds the simulation and tests without configuring `engine/`, so CI
  never fetches SDL. **Measured: 0.7s vs 94.4s to configure.** This is what makes a per-push
  3-OS matrix affordable.
- SDL pinned to commit `147a8ee32dbf9ac02f3794964490687b6bbda1bc` (was the mutable tag
  `release-3.4.14`); `GIT_SHALLOW FALSE`.
- Dropped the needless `PUBLIC SDL3::Headers` link.
- `ktd_tests --list` / `--filter`; CTest registers **24 entries instead of 2**. `--filter`
  exits non-zero on an unknown name, so the CMake-scraped list cannot drift from the runtime
  registry.
- `check_purity.cmake` split into `purity_platform` / `purity_determinism` / `purity_stdlib`.
  Case-insensitive, strips comments before token matching, and **verified to fail on injected
  violations** rather than only passing on clean code.
- `.clang-format`, `.clang-tidy`, `.editorconfig`, `.github/workflows/ci.yml`,
  `project(kaadanTD VERSION 0.1.0)`.

## Deliberately not done

`LICENSE`. Commercial title — the license choice has real consequences and is the owner's.

## Carried risk

**The CI workflow has never executed.** It passed locally on macOS only. The Linux and Windows
legs are unproven, and the `app` job's X11/Wayland package list was written from knowledge
rather than from a green run. Expect the first PR to need a fixup there.

## Lessons

The first run of the new determinism guard failed on `Types.h` — it matched `rand(` inside the
doc comment that says *"Never use rand()"*. Fixed by stripping comments before token matching.
Worth remembering: **test the tests.** A guard that never fails proves nothing, so injected
violations were used to confirm all three rules actually fire.
