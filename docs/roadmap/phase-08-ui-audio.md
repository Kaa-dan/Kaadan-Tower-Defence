# Phase 8 — UI framework, audio, scenes

**Estimate:** 5-7 days · **Depends on:** Phases 5-6

The largest phase, and the one most likely to expand. Scope carefully.

## Acceptance criteria

- [ ] `ui_hit_testing_respects_z_order` — topmost panel wins.
- [ ] `skill_tree_pan_zoom_maps_screen_to_graph_coords` — the inverse-transform property.
- [ ] `audio_voice_limiter_caps_concurrent_sounds` — 500 enemies dying at once must not blow
      the mixer.
- [ ] `scene_stack_push_pop_preserves_game_state` — opening settings mid-run does not reset it.
- [ ] Manual (owner): the UI is usable and the audio mix is right. **Both are judgment calls I
      cannot make.**

## Work

**UI — custom immediate-mode, ~1,000 lines.** For a 300-node skill tree the requirement is
mostly a pan/zoom canvas with nodes, edges, and tooltips — narrower than general UI, and
IMGUI-style suits rapidly-changing game UI better than retained. Needs buttons, panels,
tooltips, scroll, drag, and the screen-space camera Phase 5 enables.

Be realistic: this is a lot of UI for one person, and it is where scope creep will happen.
Build what the game needs, not a framework.

**Audio — SDL3's audio API directly.** `SDL_AudioStream` handles mixing and format conversion
well enough for a TD and keeps the dependency count honest. Add `SDL_INIT_AUDIO`
(`SDLPlatform.cpp:17` is video-only). Buses for music/SFX/UI, plus voice limiting. Reconsider
miniaudio only if SDL3's mixing proves insufficient — not preemptively.

**Scene stack** — Title → Menu → Game → Results → Settings, replacing the single hardcoded
`World` in `main.cpp`.

**Localization** — do not translate anything, but route user-facing strings through a string
table now. Cheap today, miserable to retrofit.
