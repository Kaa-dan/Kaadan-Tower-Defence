# Phase 5 — Renderer: textures and sprites

**Estimate:** 4-5 days · **Depends on:** Phase 4 (loosely — could run in parallel)

> ⚠️ **This design has had only one pass.** The second design review died mid-run. Re-run it
> before starting — this is the largest single chunk of engine work in the roadmap and the
> most expensive one to get wrong.

## Goal

Pixel-art sprites on screen. Today there are **no textures at any level** — not in the
`Renderer` API, the `Vertex` format, the shader, or the GL loader's entry-point list. All four
change together because none works alone.

## Acceptance criteria

- [ ] `atlas_packer_places_without_overlap` — over randomized sprite sets.
- [ ] `atlas_reserves_padding_between_sprites` — 1px, or pixel art bleeds at non-integer zoom.
- [ ] `sort_key_orders_by_layer_then_texture` — the batching property.
- [ ] `draw_calls_stay_under_20_for_a_full_frame` — measured via a renderer stat counter, with
      a populated board.
- [ ] `camera_change_mid_frame_flushes_batch` — proves world + HUD in one frame works.
- [ ] Manual (owner): the game runs and looks correct. **I cannot verify this** — sprites
      placed, oriented, and layered correctly is a judgment only the owner can make.

## Work

- **`GLLoader.h`** — add `glGenTextures`, `glBindTexture`, `glTexImage2D`, `glTexParameteri`,
  `glActiveTexture`, `glUniform1i`, `glDrawElements`, `glBufferSubData`, `glDeleteTextures`.
  Missing entries here become null function pointers at runtime, not compile errors.
- **`Vertex`** → `{ float x, y; float u, v; uint32_t rgba; }` — 20 bytes with packed color,
  down from 24, with room for UVs.
- **Indexed drawing** — 4 vertices + a preallocated quad IBO instead of 6 (`GLRenderer.cpp:173-180`).
- **`Texture` handle**, `loadTexture()`, `drawSprite(pos, size, rotation, uv, tint)`. Rotation
  matters: tower barrels are currently drawn as *lines* (`GameView.cpp:124`) purely because the
  renderer cannot rotate a quad.
- **Atlas** — runtime skyline packer at load. `GL_NEAREST` and 1px padding for pixel art.
- **Sort key + layers** — `uint64` of `layer << 32 | textureId`, sorted before flush. Target
  5-15 draw calls per frame, not one.
- **Fix the mid-frame camera limitation.** The projection uploads once in `endFrame`
  (`GLRenderer.cpp:159`), so `setCamera` cannot change mid-frame — which makes a world-space
  plus screen-space HUD split impossible. Make camera part of batch state; flush on change.
- **Static tile layer** — bake the board into a VBO rebuilt only when the grid changes.
  `GameView.cpp:77-87` submits every tile every frame: 384 today, 40,000 on a 200×200 level.
- **Housekeeping** — `glDebugMessageCallback` in debug builds; remove the
  `static const Platform* sActivePlatform` singleton hack (`GLRenderer.cpp:191`); fix the
  vertex-shader leak when fragment compilation fails (`GLRenderer.cpp:74-75`); RAII wrappers
  for GL objects.

## Dependency

Needs an image decoder. `stb_image.h` is the intended compromise — single-header, and the
alternative is writing a PNG decoder.
