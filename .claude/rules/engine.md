---
paths:
  - "engine/**"
  - "app/**"
---

# Rules for the engine and app layers

`engine/` owns the boundary between the game and the machine. `app/` wires the two together
and owns presentation.

## Layering

- `engine/backends/` is the **only** place that may know an OS or graphics API exists.
  `Platform.h` and `Renderer.h` stay free of SDL and GL types — a console port means writing
  one new backend and changing nothing upstream.
- `engine/` may see `game/`'s public headers. **Never the reverse.**
- `app/GameView` is about pixels; `game/World` is about rules. Drawing code does not belong in
  `game/`, and simulation logic does not belong in `GameView::draw()`.

## Platform backend (`backends/sdl/`)

- Window size comes in two flavours and mixing them is a real bug source: `SDL_GetWindowSize`
  gives **logical points** (mouse coordinates live here), `SDL_GetWindowSizeInPixels` gives
  **physical pixels** (the renderer needs these). `pixelScale` is their ratio.
- Edge-triggered input flags are cleared at the top of `pumpEvents`, not by the caller.

## Renderer split (`renderer/` vs `backends/gl/`)

`engine/renderer/` is the **decision** half: atlas packing, sort keys, batch planning,
projection math. It builds as `ktd_render_core` and links without SDL or GL, which is what
lets `tests/` reach it under `KTD_TESTS_ONLY` ([ADR 0013](../../docs/decisions/0013-render-core-is-testable-without-a-gpu.md)).
`purity_render_core` fails the build if an SDL, GL or `backends/` include appears there.

That guard is a **dependency** rule, not a determinism one. Presentation may use `<cmath>`
and its floats never reach the state hash.

`backends/gl/` is the **executor**: it owns every GL call and does what the planner decided.
New batching or packing logic goes in `renderer/`, or it cannot be tested.

## Renderer backend (`backends/gl/`)

- OpenGL 3.3 core. `GLLoader.h` resolves entry points through an X-macro list — adding a GL
  call means adding it there first, or you get a null function pointer at runtime.
- **`loadFunctions` fails hard on the first unresolved symbol.** Anything newer than 3.3 —
  `glDebugMessageCallback` is 4.3 — must be resolved separately and tolerate null, or the
  renderer refuses to start on the older drivers it was meant to help diagnose.
- No glad, no GLEW. The hand-rolled loader is deliberate and keeps the dependency count at one.
- Shaders are `#version 330 core`. Mobile would need `#version 300 es` plus precision
  qualifiers — roughly a day's work, deliberately deferred.

## Fixed timestep

The simulation advances in exact fixed steps regardless of render rate; this is what makes
replays and cross-platform consistency possible. The accumulator lives in `app/main.cpp` and
is clamped so a stall cannot trigger a catch-up spiral. **Never feed a variable frame delta
into `World::step()`.**

## Testing

`engine/renderer/` is covered and runs inside `ctest --preset core`. Anything pure here —
camera fitting, `pointerToCell`, projection math, atlas packing, batch planning — belongs in
`ktd_render_core` with a test, not inside a backend where no test can reach it.

`engine/backends/` and `app/` remain uncovered. Anything genuinely requiring a GPU is
verified by running the app and looking at it, which means asking the owner.
