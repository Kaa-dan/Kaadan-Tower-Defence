---
paths:
  - "game/**"
  - "tests/**"
---

# Rules for the simulation core

`game/` is pure, deterministic C++ that compiles unchanged on every target including
consoles. Three CTest guards enforce this mechanically (`tests/check_purity.cmake`). They are
extended when they get in the way, never relaxed.

## Purity — `purity_platform`

No SDL, `windows.h`, GL, Cocoa, X11, Vulkan, D3D, Metal, Wayland, JNI, or Android headers.
Not in a comment you plan to uncomment. Not "temporarily".

## Determinism — `purity_determinism`

**Replays are a shipping feature.** Same seed + same input log must produce a bit-identical
run on macOS, Windows, and Linux. That makes the following rules load-bearing rather than
stylistic:

- **Banned:** `<random>`, `<chrono>`, `<thread>`, `<mutex>`, `<atomic>`, `<future>`,
  `<execution>`, `<ctime>`, `rand()`, `srand()`, `clock()`, `long double`, `-ffast-math`.
- **`std::sort` is banned — use `std::stable_sort`.** libstdc++, libc++, and MSVC use
  different introsort pivots, so equal elements land in different orders on different
  platforms. That is a silent cross-platform desync, not a style preference.
- **No transcendental libm.** `pow`, `exp`, `sin`, `cos`, `atan2` are *not* correctly rounded
  and differ between glibc, MSVC, and Apple libm. `sqrt` and `floor` are IEEE-exact and fine.
  Once `game/Math.h` exists it is the only file permitted to include `<cmath>`.
- **No unordered containers** — iteration order is implementation-defined.
- **No pointer identity in gameplay decisions.** Tie-breaks resolve on entity handle
  ordering, never on array position, or the outcome silently changes when storage changes.
- **RNG comes from a named substream**, never a shared stream. Adding a consumer to an
  existing stream desyncs every recorded replay. Cosmetic randomness must never draw from a
  simulation stream.

## Allocation

`step()` should not allocate. Reserve up front; reuse scratch buffers as members rather than
constructing them per frame.

## When adding behavior

Add the test that proves it, in `tests/`. The simulation is headless and every one of its
properties is checkable without a window — there is no excuse for an untested rule. Tests are
registered one-per-CTest-entry automatically by scraping `TEST(` from the source, so a new
test needs no CMake edit.
