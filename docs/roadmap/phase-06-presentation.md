# Phase 6 — Text, input, filesystem, interpolation

**Estimate:** 4 days · **Depends on:** Phase 5 (text needs textures)

> ⚠️ Design has had only one pass — see Phase 5.

Four largely independent unblocks.

## Acceptance criteria

- [ ] `font_metrics_advance_correctly` — kerning and advance over known strings.
- [ ] `text_measure_matches_rendered_width`.
- [ ] `action_map_roundtrips_through_settings` — rebinding survives save/load.
- [ ] `input_edge_flags_clear_each_frame` — pressed vs held vs released.
- [ ] `interpolation_at_alpha_zero_and_one_matches_endpoints` — the correctness property that
      keeps interpolation from introducing drift.
- [ ] `pointer_to_cell_roundtrips` — the first `app/` test; nothing there is covered today.
- [ ] Manual (owner): motion is smooth on a high-refresh display. **I cannot verify this.**

## Work

**Text — bitmap font atlas.** Not stb_truetype, not SDF. For pixel art this is correct:
pixel-perfect at integer scales, no hinting or gamma problems, matches the aesthetic. Ship a
font PNG plus a metrics file. This finally kills the window-title HUD (`main.cpp:14-27`).

**Input overhaul.** Replace the three hardcoded booleans (`Platform.h:22-23`,
`SDLPlatform.cpp:88-90`) with a `Key` enum plus held/pressed/released bitsets, wheel, mouse
buttons, modifiers, text input (search fields in a 300-node tree), and gamepad. Add an
`ActionMap` loaded from settings for rebinding. **Steam Deck verification requires full gamepad
support**, so this is not optional.

**Filesystem on `Platform`** — currently absent, and it blocks assets, saves, settings, and
logging simultaneously:

```cpp
virtual bool readFile(const char* path, std::vector<uint8_t>& out) const = 0;
virtual bool writeFile(const char* path, std::span<const uint8_t> data) = 0;
virtual std::string basePath() const = 0;   // SDL_GetBasePath — assets
virtual std::string prefPath() const = 0;   // SDL_GetPrefPath — saves/settings
```

Also window mode, vsync control, and focus-loss handling (pause on focus loss).

**Render interpolation.** `main.cpp:94` computes the leftover accumulator and never uses it, so
motion is quantized to 60 Hz and visibly judders on the 120/144/165 Hz monitors most Steam
users have. Pass `alpha` to `GameView::draw` and lerp against Phase 4's `previousPosition`.
Cheap fix, large perceived-quality win.

**Decouple `GameView`** — it reads `World` internals directly, walks the flow field to draw the
route (`GameView.cpp:95-105`), and runs a full flood fill inside `draw()`. Move it to consuming
a render snapshot plus the event queue.
