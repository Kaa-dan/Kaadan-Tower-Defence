# Roadmap

Ten phases from prototype to a shippable commercial architecture. Each phase leaves the build
green and the game playable, and ships as one PR the owner reviews and merges.

Start a phase with `/phase <n>`. Check state with `/status`.

## Status ledger

| Phase | Scope | Est. | State |
|---|---|---|---|
| [0](phase-00-infrastructure.md) | Infrastructure: presets, CI, `-Werror`, purity guards | ½ d | ✅ done (`340f0a7`) |
| [1](phase-01-determinism.md) | Determinism hardening | 2 d | ⬜ **next** |
| [2](phase-02-entity-storage.md) | Entity storage: handles, pools, spatial index | 3 d | ⬜ |
| [3](phase-03-content.md) | Data-driven content + hot reload | 3 d | ⬜ |
| [4](phase-04-systems.md) | Systems split, damage pipeline, event queue | 2 d | ⬜ |
| [5](phase-05-renderer.md) | Renderer: textures, atlas, sprites | 4-5 d | ⬜ ⚠️ |
| [6](phase-06-presentation.md) | Text, input, filesystem, interpolation | 4 d | ⬜ ⚠️ |
| [7](phase-07-persistence.md) | Save / replay / settings | 2 d | ⬜ |
| [8](phase-08-ui-audio.md) | UI framework, audio, scenes | 5-7 d | ⬜ |
| [9](phase-09-shipping.md) | Steam + packaging | 4-5 d | ⬜ |

⚠️ = the design for this phase has had only one pass. Phases 1-4 were reviewed by a second
independent design agent; the renderer review died mid-run and was never redone. Worth
re-running before starting Phase 5.

Roughly **6-8 weeks** of focused work to the end of Phase 6, at which point the engine can
carry a commercial game and the rest is content and polish. For calibration: Tangy TD took
about four years ([ADR 0006](../decisions/0006-reference-tangy-td.md)).

## Ordering constraints

Two dependencies are load-bearing:

- **Determinism (1) before entity storage (2)** — the state hash is what proves the storage
  refactor changed no behavior. Without it you are trusting that 24 tests cover everything.
- **Content (3) before the systems split (4)** — otherwise the damage pipeline gets written
  against hardcoded constants and immediately rewritten.

## Open items (owner decisions, not phase work)

- **Open the PR for `build/phase-0-infrastructure`** and watch the CI run. It has **never
  executed** — green locally on macOS only. The Linux and Windows legs are unproven, and the
  `app` job's X11/Wayland package list was written from knowledge, not from a green run.
- **`LICENSE`** — deliberately not chosen. This is a commercial title; the call is the
  owner's.
- **Re-run the renderer/platform design review** before Phase 5.

## Known-wrong today

Bugs in the current code, not missing features. Each names the phase that fixes it.

| # | Issue | Where | Fixed in |
|---|---|---|---|
| 1 | `std::pow` diverges across platforms — the determinism test passes only because it has never run on two OSes | `game/src/World.cpp:74-75` | 1 |
| 2 | `findTarget` returns a raw `const Enemy*` into a vector — dangles on the first AoE tower | `game/src/World.cpp:135-148` | 2 |
| 3 | O(P×E) double linear scan per frame; the second `findEnemy` call is pure waste | `game/src/World.cpp:197,202` | 2 |
| 4 | `canPlaceTower` copies the whole `Grid` and flood-fills — called **every frame** from `draw()` | `game/src/Path.cpp:94`, `app/GameView.cpp:109` | 2 |
| 5 | No render interpolation — visible judder above 60 Hz, which is most Steam users | `app/main.cpp:94` | 6 |
| 6 | Goal detection is a radius test; fast enemies can tunnel past and never register a leak | `game/src/World.cpp:113` | 2 |
| 7 | `Rng::rangeInt` has modulo bias; seeds are not decorrelated | `game/include/game/Types.h:52` | 1 |

If you find a new one, add it here rather than fixing it inside an unrelated phase.
