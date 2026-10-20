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

## Image decoding (`image/`)

- **`stb_image.h` is included in exactly one file: `engine/image/Image.cpp`** ([ADR 0014](../../docs/decisions/0014-image-decoding.md)).
  `purity_platform` and `purity_render_core` fail the build if `game/` or `renderer/` include it.
- The decoder takes **bytes, never a path**. Files enter through `Platform::readFile` only;
  `STBI_NO_STDIO` is set so stb cannot open anything itself. `loadTexture` is the join.
- `ktd_image` links only `ktd_game` and builds under `KTD_TESTS_ONLY`, so decoding is tested in
  the core preset. Keep it that way: `LoadTexture.cpp` needs a `Renderer`, so `ktd_engine`
  compiles it, not `ktd_image`.
- PNG only. Enabling another format is new parser surface; it needs a caller first.
- `third_party/` is never edited and never run through clang-format. Upgrades replace the file
  and update `third_party/README.md`, then run the `asan` preset.

## Renderer backend (`backends/gl/`)

- OpenGL 3.3 core. `GLLoader.h` resolves entry points through an X-macro list — adding a GL
  call means adding it there first, or you get a null function pointer at runtime.
- **`loadFunctions` fails hard on the first unresolved symbol.** Anything newer than 3.3 —
  `glDebugMessageCallback` is 4.3 — must be resolved separately and tolerate null, or the
  renderer refuses to start on the older drivers it was meant to help diagnose.
- No glad, no GLEW. The hand-rolled loader is deliberate and keeps the dependency list at SDL3
  plus one vendored header.
- **Texture id 0 is the 1x1 white texture** every backend creates at init. That is what makes
  `drawQuad` a tinted white texel and keeps one virtual draw path instead of three — do not add
  an untextured shader variant back.
- **Vertex colours are packed bytes, and packing clamps.** `render::packRgba` is a float→int
  narrowing over tints that are *computed* (a health fraction, a damage flash), so an
  unclamped one is undefined behaviour, not a wrong colour.
- **Rotation is a unit direction, never an angle** ([ADR 0015](../../docs/decisions/0015-sprites-take-a-direction-not-an-angle.md)).
  An angle API creates a caller that wants trig in `game/Math.h`, where `purity_determinism`
  bans it.
- **GL objects are move-only RAII handles.** Two shader leaks lived in the hand-freed version;
  the failure paths are correct by construction now, so keep them that way.
- **The static layer draws before the batch's first span**, in that span's camera. Its place in
  the layer order is explicit on purpose — a baked buffer cannot participate in the sort, and
  "sorted by layer, except the tiles" is the bug that hides in not saying so.
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
