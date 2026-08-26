# Phase 5 — Renderer: textures and sprites

**Estimate:** 5-6 days · **Depends on:** Phase 4 (loosely — could run in parallel)

> **Second design pass done 2026-08-26.** The first pass was sound about *what* to build and
> wrong about *where it can be tested*, which changed the shape of the phase. Eight corrections
> are folded in below and marked ⟳. The estimate moved 4-5 → 5-6 days to cover the library
> split that makes the acceptance criteria runnable at all.

## Goal

Pixel-art sprites on screen. Today there are **no textures at any level** — not in the
`Renderer` API, the `Vertex` format, the shader, or the GL loader's entry-point list. All four
change together because none works alone.

## ⟳ The correction that reshapes the phase

Five of the six acceptance criteria test `engine/` code. **`engine/` is not testable today and
the first pass did not notice.** `tests/CMakeLists.txt:18` links `ktd_tests` against `ktd_game`
alone, and the `core` preset sets `KTD_TESTS_ONLY=ON`, which makes the root `CMakeLists.txt:54`
skip `add_subdirectory(engine)` outright. Writing `atlas_packer_places_without_overlap` into
`tests/` as specified produces a link error, and the obvious fix — link `ktd_engine` — drags
SDL3 into the 0.7s preset and makes `ctest --preset core` a 94s configure.

So the phase starts by splitting the renderer in two:

- **`ktd_render_core`** (new, `engine/renderer/`) — the skyline atlas packer, the sort key, the
  batch-state machine that decides *when* to flush, the projection math. Pure C++, no SDL, no
  GL, no headers from `backends/`. Built and linked by `ktd_tests` **under `KTD_TESTS_ONLY`
  too**, so the packer and the sorter stay in the 5-second loop.
- **`ktd_engine`** — unchanged in role: `GLRenderer` becomes the thing that *executes* the
  decisions `ktd_render_core` makes, and owns every GL call.

This is not bookkeeping. It is the difference between a batching design that is checked and one
that is only looked at, and it forces the packer and the sorter to be pure functions over plain
data, which is the form they should have had anyway. See
[ADR 0013](../decisions/0013-render-core-is-testable-without-a-gpu.md).

The purity guards do **not** extend to `ktd_render_core` — it is presentation, not simulation,
and it is allowed `<cmath>` and floats that never touch the hash. It gets one new guard of its
own: no `backends/` include, no SDL, no GL.

## Acceptance criteria

Runnable under `ctest --preset core` after the split:

- [x] `atlas_packer_places_without_overlap` — over randomized sprite sets.
- [x] `atlas_reserves_padding_between_sprites` — 1px, or pixel art bleeds at non-integer zoom.
- [x] `atlas_packer_reports_failure_when_full` — ⟳ new. A packer that silently drops the sprite
      that did not fit renders a blank tower and no error, which is a bug that looks like an art
      problem. It must fail loudly.
- [x] `sort_key_orders_by_layer_then_texture` — the batching property.
- [x] `sort_within_camera_span_never_reorders_across_it` — ⟳ new, see correction 3.
- [x] `draw_calls_stay_under_20_for_a_full_frame` — measured via a stat counter on the batch
      state machine, driven by a **recorded command list from a populated board**, not by a live
      GL context.
- [x] `camera_change_mid_frame_flushes_batch` — proves world + HUD in one frame works.
- [x] `color_pack_clamps_out_of_range_channels` — added: the packed vertex colour is a
      float-to-int narrowing over *computed* tints, which is undefined behaviour the moment one
      goes out of range.
- [x] `sprite_forward_rotates_without_trig` — added, see [ADR 0015](../decisions/0015-sprites-take-a-direction-not-an-angle.md).
      `{1, 0}` must be bit-exactly the identity or axis-aligned tiles seam.
- [x] `orthographic_maps_camera_corners_to_clip_space` — added: the projection moved out of the
      backend, so it became something a test can state.
- [ ] Manual (owner): the game runs and looks correct. **I cannot verify this** — sprites
      placed, oriented, and layered correctly is a judgment only the owner can make.

## Work

### ⟳ 1. `GLLoader.h` — and the entry point that would break every 3.3 driver

Add `glGenTextures`, `glBindTexture`, `glTexImage2D`, `glTexParameteri`, `glActiveTexture`,
`glUniform1i`, `glDrawElements`, `glBufferSubData`, `glDeleteTextures`, `glPixelStorei`.
Missing entries here become null function pointers at runtime, not compile errors.

**`glDebugMessageCallback` must not go in that list.** It is OpenGL 4.3 / `KHR_debug`, not 3.3.
`loadFunctions` fails hard and returns false on the first unresolved symbol
(`GLLoader.cpp:14-17`), so adding it to `KTD_GL_FUNCS` means the renderer refuses to start on
exactly the older drivers the debug output would have helped diagnose. It needs a separate
optional resolve that tolerates null.

The constants are `inline constexpr` in the same header and the first pass listed none of them.
Needed: `GL_ELEMENT_ARRAY_BUFFER` 0x8893, `GL_UNSIGNED_INT` 0x1405, `GL_TEXTURE_2D` 0x0DE1,
`GL_TEXTURE0` 0x84C0, `GL_RGBA` 0x1908, `GL_RGBA8` 0x8058, `GL_UNSIGNED_BYTE` 0x1401,
`GL_NEAREST` 0x2600, `GL_TEXTURE_MIN_FILTER` 0x2801, `GL_TEXTURE_MAG_FILTER` 0x2800,
`GL_TEXTURE_WRAP_S` 0x2802, `GL_TEXTURE_WRAP_T` 0x2803, `GL_CLAMP_TO_EDGE` 0x812F,
`GL_STATIC_DRAW` 0x88E4, `GL_UNPACK_ALIGNMENT` 0x0CF5, `GL_DEBUG_OUTPUT` 0x92E0,
`GL_DEBUG_OUTPUT_SYNCHRONOUS` 0x8242.

### ⟳ 2. `Vertex` → `{ float x, y; float u, v; uint32_t rgba; }`

20 bytes with packed color, down from 24 (`GLRenderer.cpp:33-36` is six floats), with room for
UVs.

The packing step is a float→int conversion and the public API is still `Color`'s four floats,
so **it must clamp before converting**. An out-of-range float→int narrowing is UB, it is the
exact class the `asan` preset exists to catch, and a tint computed from a health fraction is
one divide-by-zero away from producing one.

### 3. ⟳ Sort key, layers, and the interaction the first pass missed

`uint64` of `layer << 32 | textureId`, sorted before flush. Target 5-15 draw calls per frame.

**Sorting is per camera span, not per frame.** This phase also makes the camera part of batch
state so a world-space board and a screen-space HUD can share a frame — and a frame-global sort
would reorder HUD quads in among world quads, drawing the HUD under the board in whatever order
the texture ids happened to fall. The batch is a sequence of spans delimited by camera changes;
each span sorts within itself and spans keep their submission order.

**`std::stable_sort`, not `std::sort`.** The `purity_determinism` guard only covers `game/`, so
nothing mechanically stops `std::sort` here — but equal keys are exactly the alpha-blended
sprites whose draw order is visible, and an unstable sort makes that order differ between
libstdc++, libc++ and MSVC. That is a screenshot that differs per platform, which is a bug even
though it is not a desync.

### ⟳ 4. `Texture` handle, `loadTexture()`, and `drawSprite` takes a direction, not an angle

`drawSprite(Vec2 center, Vec2 size, Vec2 forward, UvRect uv, Color tint)`.

The first pass specified `rotation` as an angle. That is the wrong parameter. Tower barrels are
drawn as lines today (`GameView.cpp:129`) from `tower.aim`, which **is already a unit direction
vector** the simulation maintains. An angle API forces the caller to `atan2` a vector the sim
already had and the renderer to `sin`/`cos` it straight back — two transcendental round trips to
recover the input — and it invites someone to add trig to `game/Math.h`, where
`purity_determinism` bans it and where Phase 1 deliberately declined to add it.

A unit `forward` is a 2x2 rotation with no trig anywhere: `(x·f.x − y·f.y, x·f.y + y·f.x)`.
`{1, 0}` is unrotated. Callers that genuinely have an angle can build the vector themselves.

### 5. Indexed drawing

4 vertices + a preallocated quad IBO instead of 6 (`GLRenderer.cpp:172-179`). The element array
binding is VAO state, so bind the IBO once while the VAO is bound at init and never again.

### 6. Atlas

Runtime skyline packer at load. `GL_NEAREST` and 1px padding for pixel art. Lives in
`ktd_render_core` and returns placements as data — it never touches GL. `GLRenderer` uploads
what it returns.

### 7. Fix the mid-frame camera limitation

The projection uploads once in `endFrame` (`GLRenderer.cpp:161`), so `setCamera` cannot change
mid-frame — the last call before `endFrame` silently wins for the whole frame. Make camera part
of batch state; flush on change.

### 8. ⟳ Static tile layer

Bake the board into a VBO rebuilt only when the grid changes. `GameView.cpp:76-86` submits every
tile every frame: 384 today, 40,000 on a 200x200 level.

This VBO is drawn outside the sorted batch, so **its position in the layer order has to be
explicit**: it draws first, before the batch's first span flushes. Otherwise "sorted by layer"
quietly means "sorted by layer, except the tiles".

### ⟳ 9. Housekeeping

- `glDebugMessageCallback` in debug builds — via the optional resolve from correction 1.
- Remove the `static const Platform* sActivePlatform` singleton (`GLRenderer.cpp:190`). It
  exists because `GetProcAddressFn` is a plain function pointer and cannot capture. The fix is
  to have `loadFunctions` take `const Platform&` directly — `GLLoader.cpp` is under
  `backends/`, which the layering rule already permits to see `Platform.h`, and `Platform.h` is
  itself OS-free. No userdata parameter needed.
- Fix the vertex-shader leak when fragment compilation fails (`GLRenderer.cpp:74-75`) — and the
  one the first pass missed at `GLRenderer.cpp:85-90`, where a link failure returns without
  deleting either shader *or* the program.
- RAII wrappers for GL objects.

## Dependency

Needs an image decoder — the one thing in this phase that is the owner's call, because
`CLAUDE.md` opens with "One dependency: SDL3" and this would make it two.
[ADR 0014](../decisions/0014-image-decoding.md) lays out the three options and recommends
vendoring `stb_image.h`. Not blocking: everything above except `loadTexture`'s file path can be
built and tested against in-memory pixel buffers first.
