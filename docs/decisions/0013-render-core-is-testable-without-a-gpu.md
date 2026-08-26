# 0013 — The renderer splits at the line a test can reach

**Status:** accepted · 2026-08-26

## Context

Phase 5's design review found that five of its six acceptance criteria could not be run. The
criteria name tests for the atlas packer, the sort key, the draw-call count, and the mid-frame
camera flush — all of them `engine/` code — but `tests/CMakeLists.txt` links `ktd_tests` against
`ktd_game` and nothing else, and the `core` preset sets `KTD_TESTS_ONLY=ON`, which makes the
root `CMakeLists.txt` skip `add_subdirectory(engine)` entirely. The tests as written would not
compile.

The two obvious repairs are both bad. Linking `ktd_engine` into `ktd_tests` pulls SDL3 into the
preset whose entire purpose is not having it: 0.7s to configure becomes 94s, per push, on three
operating systems. A second test binary built only under `dev` is worse in a quieter way — it
puts the batching logic in the tier that is compiled less often, run less often, and never run
in the fast loop anyone actually uses while working.

Underneath the build problem is a design problem. A skyline packer, a sort key, and the rule for
when a batch must flush are decisions about data. None of them needs a GPU. They were about to
be written inside `GLRenderer` next to the `glBufferData` calls purely because that is where the
first pass put them, and that placement is what made them untestable.

## Decision

**The renderer splits into the part that decides and the part that executes, and the deciding
part links without SDL or GL.**

- `ktd_render_core` holds the atlas packer, the sort key, the batch state machine, and the
  projection math. Pure C++ over plain data. No SDL, no GL, no `backends/` header.
- `ktd_engine` keeps `GLRenderer`, which owns every GL call and executes what `ktd_render_core`
  decides.
- `ktd_tests` links `ktd_render_core` **including under `KTD_TESTS_ONLY`**, so the packer and
  the batcher stay inside `ctest --preset core`.
- A fourth purity guard, `purity_render_core`, enforces the boundary the same way the other
  three do — by failing the build rather than asking politely.

The draw-call criterion is met by driving the batch state machine with a recorded command list
from a populated board and reading a counter, not by standing up a context and counting real
`glDrawElements` calls.

## Consequences

The batch state machine has to be expressible without a GL handle in it, so texture identity
inside `ktd_render_core` is an opaque `uint32` the backend maps to a GL name. That is a small
indirection and it is also the thing that would have to exist anyway for a Metal or D3D12
backend, so the cost is being paid early rather than extra.

`engine/` stops being a single uniform tier. The layering rule in `.claude/rules/` says
`backends/` is the only place that may know a graphics API exists; this adds that
`engine/renderer/` is now the place that may not know one exists *at all*, which is stricter
than "no SDL types in the interface" and needs saying out loud.

The purity guards remain scoped to `game/` for determinism. `ktd_render_core` is presentation:
it may use `<cmath>`, and its floats never reach the state hash. Its guard checks dependencies,
not determinism. Conflating the two would either ban `<cmath>` from a renderer for no reason or
weaken the guard that protects replays.

The precedent is `core_purity` from Phase 0 and the `KTD_TESTS_ONLY` split itself: on this
project the thing that keeps an architectural boundary alive is a build that breaks when it is
crossed. A boundary maintained by intention has a half-life of about five commits.
