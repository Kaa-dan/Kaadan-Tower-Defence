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

## Renderer backend (`backends/gl/`)

- OpenGL 3.3 core. `GLLoader.h` resolves entry points through an X-macro list — adding a GL
  call means adding it there first, or you get a null function pointer at runtime.
- No glad, no GLEW. The hand-rolled loader is deliberate and keeps the dependency count at one.
- Shaders are `#version 330 core`. Mobile would need `#version 300 es` plus precision
  qualifiers — roughly a day's work, deliberately deferred.

## Fixed timestep

The simulation advances in exact fixed steps regardless of render rate; this is what makes
replays and cross-platform consistency possible. The accumulator lives in `app/main.cpp` and
is clamped so a stall cannot trigger a catch-up spiral. **Never feed a variable frame delta
into `World::step()`.**

## Testing

Nothing in `engine/` or `app/` is currently covered. Pure functions here — camera fitting,
`pointerToCell`, projection math, atlas packing — can and should be tested without a window.
Anything requiring a GPU is verified by running the app and looking at it, which means asking
the owner.
